package acp

import (
	"bufio"
	"context"
	"encoding/json"
	"io"
	"testing"
	"time"
)

// pipeConn wires a Conn to an in-memory client: the test writes requests to in and reads replies from out.
func pipeConn(t *testing.T, h Handler) (*Conn, io.Writer, *bufio.Reader) {
	t.Helper()
	inR, inW := io.Pipe()
	outR, outW := io.Pipe()
	c := NewConn(outW)
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	go func() { _ = c.Serve(ctx, inR, h); close(done) }()
	t.Cleanup(func() { cancel(); inW.Close(); outR.Close(); <-done })
	return c, inW, bufio.NewReader(outR)
}

func readMsg(t *testing.T, r *bufio.Reader) map[string]any {
	t.Helper()
	ch := make(chan map[string]any, 1)
	go func() {
		line, err := r.ReadBytes('\n')
		if err != nil {
			ch <- nil
			return
		}
		var m map[string]any
		_ = json.Unmarshal(line, &m)
		ch <- m
	}()
	select {
	case m := <-ch:
		if m == nil {
			t.Fatal("connection closed")
		}
		return m
	case <-time.After(2 * time.Second):
		t.Fatal("timed out waiting for a message")
		return nil
	}
}

func TestRequestReplyAndErrors(t *testing.T) {
	_, in, out := pipeConn(t, func(ctx context.Context, method string, params json.RawMessage) (any, error) {
		if method == "echo" {
			return json.RawMessage(params), nil
		}
		return nil, MethodNotFound(method)
	})

	io.WriteString(in, `{"jsonrpc":"2.0","id":7,"method":"echo","params":{"x":1}}`+"\n")
	m := readMsg(t, out)
	if m["id"].(float64) != 7 || m["result"].(map[string]any)["x"].(float64) != 1 {
		t.Fatalf("bad echo reply: %v", m)
	}

	io.WriteString(in, `{"jsonrpc":"2.0","id":"a","method":"nope"}`+"\n")
	m = readMsg(t, out)
	if m["id"] != "a" || m["error"].(map[string]any)["code"].(float64) != CodeMethodNotFound {
		t.Fatalf("bad error reply: %v", m)
	}

	io.WriteString(in, "not json\n")
	if m = readMsg(t, out); m["error"].(map[string]any)["code"].(float64) != CodeParse {
		t.Fatalf("bad parse error: %v", m)
	}
}

func TestCallToClient(t *testing.T) {
	c, in, out := pipeConn(t, func(context.Context, string, json.RawMessage) (any, error) { return nil, nil })

	type res struct{ Outcome string }
	got := make(chan res, 1)
	go func() {
		var r res
		if err := c.Call(context.Background(), "session/request_permission", map[string]any{"q": 1}, &r); err != nil {
			t.Error(err)
		}
		got <- r
	}()
	req := readMsg(t, out)
	if req["method"] != "session/request_permission" {
		t.Fatalf("unexpected request: %v", req)
	}
	id, _ := json.Marshal(req["id"])
	io.WriteString(in, `{"jsonrpc":"2.0","id":`+string(id)+`,"result":{"Outcome":"selected"}}`+"\n")
	select {
	case r := <-got:
		if r.Outcome != "selected" {
			t.Fatalf("bad result: %+v", r)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("call never returned")
	}
}
