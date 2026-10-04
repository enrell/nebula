package acp

// The subset of the ACP schema nebula-agent speaks (https://agentclientprotocol.com/protocol/schema).

const ProtocolVersion = 1

type Implementation struct {
	Name    string `json:"name"`
	Version string `json:"version,omitempty"`
}

type InitializeParams struct {
	ProtocolVersion int             `json:"protocolVersion"`
	ClientInfo      *Implementation `json:"clientInfo,omitempty"`
}

type PromptCapabilities struct {
	Image           bool `json:"image"`
	Audio           bool `json:"audio"`
	EmbeddedContext bool `json:"embeddedContext"`
}

type AgentCapabilities struct {
	LoadSession        bool               `json:"loadSession"`
	PromptCapabilities PromptCapabilities `json:"promptCapabilities"`
}

type AuthMethod struct {
	ID   string `json:"id"`
	Name string `json:"name"`
}

type InitializeResult struct {
	ProtocolVersion   int               `json:"protocolVersion"`
	AgentCapabilities AgentCapabilities `json:"agentCapabilities"`
	AgentInfo         *Implementation   `json:"agentInfo,omitempty"`
	AuthMethods       []AuthMethod      `json:"authMethods"`
}

type EnvVariable struct {
	Name  string `json:"name"`
	Value string `json:"value"`
}

// McpServer is a stdio MCP server the client offers to the session.
type McpServer struct {
	Name    string        `json:"name"`
	Command string        `json:"command"`
	Args    []string      `json:"args"`
	Env     []EnvVariable `json:"env"`
}

type NewSessionParams struct {
	Cwd        string      `json:"cwd"`
	McpServers []McpServer `json:"mcpServers"`
}

type NewSessionResult struct {
	SessionID string `json:"sessionId"`
}

// ContentBlock is a prompt or message block. Only text is used so far.
type ContentBlock struct {
	Type string `json:"type"`
	Text string `json:"text,omitempty"`
}

func Text(s string) ContentBlock { return ContentBlock{Type: "text", Text: s} }

type PromptParams struct {
	SessionID string         `json:"sessionId"`
	Prompt    []ContentBlock `json:"prompt"`
}

type StopReason string

const (
	StopEndTurn   StopReason = "end_turn"
	StopCancelled StopReason = "cancelled"
)

type PromptResult struct {
	StopReason StopReason `json:"stopReason"`
}

type CancelParams struct {
	SessionID string `json:"sessionId"`
}

// SessionUpdate is the payload of a session/update notification. Kind is the sessionUpdate discriminator:
// agent_message_chunk, agent_thought_chunk, tool_call, tool_call_update, plan.
type SessionUpdate struct {
	Kind       string         `json:"sessionUpdate"`
	Content    *ContentBlock  `json:"content,omitempty"`
	ToolCallID string         `json:"toolCallId,omitempty"`
	Title      string         `json:"title,omitempty"`
	ToolKind   string         `json:"kind,omitempty"`
	Status     string         `json:"status,omitempty"`
	RawInput   map[string]any `json:"rawInput,omitempty"`
}

type SessionNotification struct {
	SessionID string        `json:"sessionId"`
	Update    SessionUpdate `json:"update"`
}
