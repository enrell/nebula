package agent

import (
	"bufio"
	"context"
	"encoding/json"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/enrell/nebula/agent/internal/acp"
	"github.com/enrell/nebula/agent/internal/nebula/nebulatest"
)

// client drives an Agent over in-memory pipes like the operator does over stdio.
type client struct {
	t   *testing.T
	in  io.Writer
	out *bufio.Reader
	id  int
}

func start(t *testing.T) *client {
	inR, inW := io.Pipe()
	outR, outW := io.Pipe()
	conn := acp.NewConn(outW)
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	go func() { _ = conn.Serve(ctx, inR, New(conn, "test").Handle); close(done) }()
	t.Cleanup(func() { cancel(); inW.Close(); outR.Close(); <-done })
	return &client{t: t, in: inW, out: bufio.NewReader(outR)}
}

func (c *client) next() map[string]any {
	c.t.Helper()
	ch := make(chan map[string]any, 1)
	go func() {
		line, err := c.out.ReadBytes('\n')
		var m map[string]any
		if err == nil {
			_ = json.Unmarshal(line, &m)
		}
		ch <- m
	}()
	select {
	case m := <-ch:
		if m == nil {
			c.t.Fatal("connection closed")
		}
		return m
	case <-time.After(3 * time.Second):
		c.t.Fatal("timed out")
		return nil
	}
}

// call sends a request and collects the session/update notifications that arrive before its reply.
func (c *client) call(method string, params any) (result map[string]any, updates []map[string]any) {
	c.t.Helper()
	c.id++
	b, _ := json.Marshal(map[string]any{"jsonrpc": "2.0", "id": c.id, "method": method, "params": params})
	io.WriteString(c.in, string(b)+"\n")
	for {
		m := c.next()
		if m["method"] == "session/update" {
			updates = append(updates, m["params"].(map[string]any)["update"].(map[string]any))
			continue
		}
		if e, ok := m["error"]; ok {
			c.t.Fatalf("%s failed: %v", method, e)
		}
		return m["result"].(map[string]any), updates
	}
}

func TestSessionPrompt(t *testing.T) {
	sock := nebulatest.Fake(t, func(method string, _ json.RawMessage) (any, string) {
		if method == "pane.list" {
			return []map[string]any{{"id": 1, "agent": "claude", "state": "blocked"}, {"id": 2}}, ""
		}
		return nil, "unexpected " + method
	})
	project := t.TempDir()
	if err := os.Mkdir(filepath.Join(project, ".git"), 0o755); err != nil {
		t.Fatal(err)
	}
	cwd := filepath.Join(project, "src")
	if err := os.Mkdir(cwd, 0o755); err != nil {
		t.Fatal(err)
	}

	c := start(t)
	ini, _ := c.call("initialize", map[string]any{"protocolVersion": 1})
	if ini["protocolVersion"].(float64) != acp.ProtocolVersion {
		t.Fatalf("initialize: %v", ini)
	}
	sess, _ := c.call("session/new", map[string]any{"cwd": cwd, "mcpServers": []any{map[string]any{
		"name": "nebula", "command": "nebula", "args": []string{"mcp"},
		"env": []any{map[string]any{"name": "NEBULA_SOCKET", "value": sock}}}}})
	id, _ := sess["sessionId"].(string)
	if id == "" {
		t.Fatalf("no session id: %v", sess)
	}

	res, updates := c.call("session/prompt", map[string]any{"sessionId": id, "prompt": []any{map[string]any{"type": "text", "text": "hi"}}})
	if res["stopReason"] != "end_turn" {
		t.Fatalf("stopReason: %v", res)
	}
	var kinds []string
	var text string
	for _, u := range updates {
		kinds = append(kinds, u["sessionUpdate"].(string))
		if u["sessionUpdate"] == "agent_message_chunk" {
			text += u["content"].(map[string]any)["text"].(string)
		}
		if u["sessionUpdate"] == "tool_call_update" && u["status"] != "completed" {
			t.Fatalf("tool call did not complete: %v", u)
		}
	}
	if strings.Join(kinds, ",") != "tool_call,tool_call_update,agent_message_chunk" {
		t.Fatalf("updates: %v", kinds)
	}
	for _, want := range []string{"Project: " + filepath.Base(project), "2 panes", "claude (blocked)"} {
		if !strings.Contains(text, want) {
			t.Fatalf("reply %q lacks %q", text, want)
		}
	}
}

func TestUnknownMethodAndSession(t *testing.T) {
	c := start(t)
	c.id++
	io.WriteString(c.in, `{"jsonrpc":"2.0","id":1,"method":"session/load","params":{}}`+"\n")
	if m := c.next(); m["error"].(map[string]any)["code"].(float64) != acp.CodeMethodNotFound {
		t.Fatalf("want method not found: %v", m)
	}
	io.WriteString(c.in, `{"jsonrpc":"2.0","id":2,"method":"session/prompt","params":{"sessionId":"nope","prompt":[]}}`+"\n")
	if m := c.next(); m["error"].(map[string]any)["code"].(float64) != acp.CodeInvalidParams {
		t.Fatalf("want invalid params: %v", m)
	}
}

func TestReplyWhenNebulaIsUnreachable(t *testing.T) {
	text := reply("/nonexistent/dir", nil, os.ErrNotExist)
	if !strings.Contains(text, "could not reach nebula") || !strings.Contains(text, "not a git project") {
		t.Fatalf("reply: %q", text)
	}
}
