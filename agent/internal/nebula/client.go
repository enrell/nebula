// Package nebula talks to a running nebula through its automation socket: one JSON object per line,
// {"id", "method", "params"} answered by {"id", "ok", "result"} or {"id", "ok": false, "error"} (see docs/automation.md).
package nebula

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"net"
	"os"
	"path/filepath"
)

// SocketPath is where nebula listens, resolved like the C++ side (src/paths.h): NEBULA_SOCKET, the pre-rename
// AINEBULA_SOCKET, then $XDG_RUNTIME_DIR/nebula.sock.
func SocketPath() string {
	for _, k := range []string{"NEBULA_SOCKET", "AINEBULA_SOCKET"} {
		if v := os.Getenv(k); v != "" {
			return v
		}
	}
	dir := os.Getenv("XDG_RUNTIME_DIR")
	if dir == "" {
		dir = os.TempDir()
	}
	return filepath.Join(dir, "nebula.sock")
}

// Client calls nebula's automation API. Each call uses its own connection, so a Client is safe for concurrent use.
type Client struct {
	Path string
}

func New(path string) *Client {
	if path == "" {
		path = SocketPath()
	}
	return &Client{Path: path}
}

type response struct {
	OK     bool            `json:"ok"`
	Result json.RawMessage `json:"result"`
	Error  string          `json:"error"`
}

// Call runs one API method and decodes its result into out (when not nil).
func (c *Client) Call(ctx context.Context, method string, params, out any) error {
	var d net.Dialer
	conn, err := d.DialContext(ctx, "unix", c.Path)
	if err != nil {
		return err
	}
	defer conn.Close()
	if dl, ok := ctx.Deadline(); ok {
		_ = conn.SetDeadline(dl)
	}
	stop := context.AfterFunc(ctx, func() { _ = conn.Close() })
	defer stop()

	if params == nil {
		params = struct{}{}
	}
	req, err := json.Marshal(map[string]any{"id": 1, "method": method, "params": params})
	if err != nil {
		return err
	}
	if _, err := conn.Write(append(req, '\n')); err != nil {
		return err
	}
	line, err := bufio.NewReaderSize(conn, 1<<16).ReadBytes('\n')
	if err != nil {
		if ctx.Err() != nil {
			return ctx.Err()
		}
		return err
	}
	var r response
	if err := json.Unmarshal(line, &r); err != nil {
		return err
	}
	if !r.OK {
		if r.Error == "" {
			r.Error = "request failed"
		}
		return errors.New(method + ": " + r.Error)
	}
	if out != nil && len(r.Result) > 0 {
		return json.Unmarshal(r.Result, out)
	}
	return nil
}

// Pane is one entry of pane.list.
type Pane struct {
	ID      int    `json:"id"`
	Space   int    `json:"space"`
	Tab     int    `json:"tab"`
	Label   string `json:"label"`
	Title   string `json:"title"`
	Cwd     string `json:"cwd"`
	Branch  string `json:"branch"`
	Agent   string `json:"agent"`
	State   string `json:"state"`
	Focused bool   `json:"focused"`
	Summary string `json:"summary"`
}

func (c *Client) Panes(ctx context.Context) ([]Pane, error) {
	var p []Pane
	return p, c.Call(ctx, "pane.list", nil, &p)
}
