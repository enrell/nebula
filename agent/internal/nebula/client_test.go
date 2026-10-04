package nebula_test

import (
	"context"
	"encoding/json"
	"strings"
	"testing"

	"github.com/enrell/nebula/agent/internal/nebula"
	"github.com/enrell/nebula/agent/internal/nebula/nebulatest"
)

func TestCall(t *testing.T) {
	path := nebulatest.Fake(t, func(method string, params json.RawMessage) (any, string) {
		switch method {
		case "pane.list":
			return []map[string]any{{"id": 1, "agent": "claude", "state": "working", "cwd": "/src"}}, ""
		case "ping":
			return "pong", ""
		}
		return nil, "unknown method " + method
	})
	c := nebula.New(path)
	ctx := context.Background()

	var pong string
	if err := c.Call(ctx, "ping", nil, &pong); err != nil || pong != "pong" {
		t.Fatalf("ping: %q %v", pong, err)
	}
	panes, err := c.Panes(ctx)
	if err != nil || len(panes) != 1 || panes[0].Agent != "claude" || panes[0].State != "working" {
		t.Fatalf("panes: %+v %v", panes, err)
	}
	if err := c.Call(ctx, "bogus", nil, nil); err == nil || !strings.Contains(err.Error(), "unknown method bogus") {
		t.Fatalf("want the API error, got %v", err)
	}
}

func TestSocketPath(t *testing.T) {
	t.Setenv("NEBULA_SOCKET", "")
	t.Setenv("AINEBULA_SOCKET", "")
	t.Setenv("XDG_RUNTIME_DIR", "/run/user/1")
	if p := nebula.SocketPath(); p != "/run/user/1/nebula.sock" {
		t.Fatalf("default: %s", p)
	}
	t.Setenv("NEBULA_SOCKET", "/tmp/dev.sock")
	if p := nebula.SocketPath(); p != "/tmp/dev.sock" {
		t.Fatalf("env: %s", p)
	}
}
