// Package agent is nebula's own agent, served over ACP. This is the skeleton: it answers the protocol, keeps
// sessions and reads the workspace through nebula's socket. Memory and the model loop come next (docs/vision.md).
package agent

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/enrell/nebula/agent/internal/acp"
	"github.com/enrell/nebula/agent/internal/nebula"
)

type session struct {
	id     string
	cwd    string
	nebula *nebula.Client

	mu     sync.Mutex
	cancel context.CancelFunc // of the running prompt, nil when idle
}

// Agent serves one ACP connection.
type Agent struct {
	conn    *acp.Conn
	version string

	mu       sync.Mutex
	sessions map[string]*session
}

func New(conn *acp.Conn, version string) *Agent {
	return &Agent{conn: conn, version: version, sessions: map[string]*session{}}
}

// Handle is the acp.Handler for this agent.
func (a *Agent) Handle(ctx context.Context, method string, params json.RawMessage) (any, error) {
	switch method {
	case "initialize":
		return a.initialize(params)
	case "session/new":
		return a.newSession(params)
	case "session/prompt":
		return a.prompt(ctx, params)
	case "session/cancel":
		return nil, a.cancel(params)
	default:
		return nil, acp.MethodNotFound(method)
	}
}

func decode[T any](raw json.RawMessage) (T, error) {
	var v T
	if len(raw) == 0 {
		return v, nil
	}
	if err := json.Unmarshal(raw, &v); err != nil {
		return v, &acp.Error{Code: acp.CodeInvalidParams, Message: err.Error()}
	}
	return v, nil
}

func (a *Agent) initialize(raw json.RawMessage) (any, error) {
	if _, err := decode[acp.InitializeParams](raw); err != nil {
		return nil, err
	}
	return acp.InitializeResult{
		ProtocolVersion: acp.ProtocolVersion,
		AgentInfo:       &acp.Implementation{Name: "nebula-agent", Version: a.version},
		AuthMethods:     []acp.AuthMethod{},
	}, nil
}

func (a *Agent) newSession(raw json.RawMessage) (any, error) {
	p, err := decode[acp.NewSessionParams](raw)
	if err != nil {
		return nil, err
	}
	cwd := p.Cwd
	if cwd == "" {
		cwd, _ = os.Getwd()
	}
	s := &session{id: newID(), cwd: cwd, nebula: nebula.New(socketFrom(p.McpServers))}
	a.mu.Lock()
	a.sessions[s.id] = s
	a.mu.Unlock()
	return acp.NewSessionResult{SessionID: s.id}, nil
}

// socketFrom picks nebula's socket from the "nebula" MCP server the client offers: the operator passes
// NEBULA_SOCKET there, which also covers a dev instance whose socket is not the default one.
func socketFrom(servers []acp.McpServer) string {
	for _, s := range servers {
		for _, e := range s.Env {
			if e.Name == "NEBULA_SOCKET" && e.Value != "" {
				return e.Value
			}
		}
	}
	return ""
}

func (a *Agent) session(id string) (*session, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	s := a.sessions[id]
	if s == nil {
		return nil, &acp.Error{Code: acp.CodeInvalidParams, Message: "unknown session " + id}
	}
	return s, nil
}

func (a *Agent) cancel(raw json.RawMessage) error {
	p, err := decode[acp.CancelParams](raw)
	if err != nil {
		return err
	}
	s, err := a.session(p.SessionID)
	if err != nil {
		return err
	}
	s.mu.Lock()
	if s.cancel != nil {
		s.cancel()
	}
	s.mu.Unlock()
	return nil
}

func (a *Agent) update(sessionID string, u acp.SessionUpdate) {
	_ = a.conn.Notify("session/update", acp.SessionNotification{SessionID: sessionID, Update: u})
}

func (a *Agent) say(sessionID, text string) {
	c := acp.Text(text)
	a.update(sessionID, acp.SessionUpdate{Kind: "agent_message_chunk", Content: &c})
}

func (a *Agent) prompt(ctx context.Context, raw json.RawMessage) (any, error) {
	p, err := decode[acp.PromptParams](raw)
	if err != nil {
		return nil, err
	}
	s, err := a.session(p.SessionID)
	if err != nil {
		return nil, err
	}
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	s.mu.Lock()
	if s.cancel != nil {
		s.mu.Unlock()
		return nil, &acp.Error{Code: acp.CodeInvalidRequest, Message: "a prompt is already running in this session"}
	}
	s.cancel = cancel
	s.mu.Unlock()
	defer func() { s.mu.Lock(); s.cancel = nil; s.mu.Unlock() }()

	// One real tool call through nebula's socket, so the whole path (operator -> ACP -> agent -> socket) is exercised.
	const toolID = "workspace-1"
	a.update(s.id, acp.SessionUpdate{Kind: "tool_call", ToolCallID: toolID, Title: "Read the workspace", ToolKind: "read",
		Status: "in_progress", RawInput: map[string]any{"method": "pane.list"}})
	callCtx, done := context.WithTimeout(ctx, 5*time.Second)
	panes, perr := s.nebula.Panes(callCtx)
	done()
	if ctx.Err() != nil {
		a.update(s.id, acp.SessionUpdate{Kind: "tool_call_update", ToolCallID: toolID, Status: "failed"})
		return acp.PromptResult{StopReason: acp.StopCancelled}, nil
	}
	status := "completed"
	if perr != nil {
		status = "failed"
	}
	a.update(s.id, acp.SessionUpdate{Kind: "tool_call_update", ToolCallID: toolID, Status: status})

	a.say(s.id, reply(s.cwd, panes, perr))
	return acp.PromptResult{StopReason: acp.StopEndTurn}, nil
}

// reply is the skeleton's answer: what it can already see. The model loop replaces it.
func reply(cwd string, panes []nebula.Pane, err error) string {
	var b strings.Builder
	b.WriteString("nebula-agent is connected (skeleton: no model yet; memory and the agent loop come next).\n\n")
	if root := gitRoot(cwd); root != "" {
		fmt.Fprintf(&b, "Project: %s (%s)\n", filepath.Base(root), root)
	} else {
		fmt.Fprintf(&b, "Directory: %s (not a git project)\n", cwd)
	}
	if err != nil {
		fmt.Fprintf(&b, "Workspace: could not reach nebula: %v\n", err)
		return b.String()
	}
	var agents []string
	for _, p := range panes {
		if p.Agent != "" {
			agents = append(agents, fmt.Sprintf("%s (%s)", p.Agent, p.State))
		}
	}
	fmt.Fprintf(&b, "Workspace: %d panes", len(panes))
	if len(agents) > 0 {
		fmt.Fprintf(&b, ", agents: %s", strings.Join(agents, ", "))
	}
	b.WriteString("\n")
	return b.String()
}

// gitRoot walks up from dir to the nearest directory holding .git (a directory, or a file in a worktree).
func gitRoot(dir string) string {
	if dir == "" {
		return ""
	}
	dir = filepath.Clean(dir)
	for {
		if _, err := os.Stat(filepath.Join(dir, ".git")); err == nil {
			return dir
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			return ""
		}
		dir = parent
	}
}

func newID() string {
	var b [8]byte
	_, _ = rand.Read(b[:])
	return hex.EncodeToString(b[:])
}
