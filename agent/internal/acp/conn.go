// Package acp is a minimal Agent Client Protocol connection: newline-delimited JSON-RPC 2.0 over a reader and a
// writer (the agent's stdin and stdout). It serves the client's requests and notifications and lets the agent call
// the client back (session/update notifications, session/request_permission requests).
package acp

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"sync"
)

// Error is a JSON-RPC error. Handlers return it to choose the code; any other error becomes an internal error.
type Error struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
}

func (e *Error) Error() string { return fmt.Sprintf("acp error %d: %s", e.Code, e.Message) }

const (
	CodeParse          = -32700
	CodeInvalidRequest = -32600
	CodeMethodNotFound = -32601
	CodeInvalidParams  = -32602
	CodeInternal       = -32603
)

// MethodNotFound is what a handler returns for a method it does not implement.
func MethodNotFound(method string) *Error {
	return &Error{Code: CodeMethodNotFound, Message: "method not found: " + method}
}

// Handler serves one message from the client. For a notification the result is ignored. Requests run on their own
// goroutine, so a long session/prompt does not block the session/cancel that interrupts it.
type Handler func(ctx context.Context, method string, params json.RawMessage) (any, error)

type message struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      json.RawMessage `json:"id,omitempty"`
	Method  string          `json:"method,omitempty"`
	Params  json.RawMessage `json:"params,omitempty"`
	Result  json.RawMessage `json:"result,omitempty"`
	Error   *Error          `json:"error,omitempty"`
}

type outgoing struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      json.RawMessage `json:"id,omitempty"`
	Method  string          `json:"method,omitempty"`
	Params  any             `json:"params,omitempty"`
	Result  any             `json:"result,omitempty"`
	Error   *Error          `json:"error,omitempty"`
}

// Conn is one ACP connection.
type Conn struct {
	w   io.Writer
	wmu sync.Mutex

	pmu     sync.Mutex
	pending map[string]chan message
	nextID  int64
}

func NewConn(w io.Writer) *Conn {
	return &Conn{w: w, pending: map[string]chan message{}}
}

func (c *Conn) send(m outgoing) error {
	m.JSONRPC = "2.0"
	b, err := json.Marshal(m)
	if err != nil {
		return err
	}
	c.wmu.Lock()
	defer c.wmu.Unlock()
	_, err = c.w.Write(append(b, '\n'))
	return err
}

// Notify sends a notification to the client.
func (c *Conn) Notify(method string, params any) error {
	return c.send(outgoing{Method: method, Params: params})
}

// Call sends a request to the client and waits for its reply, decoded into result (when not nil).
func (c *Conn) Call(ctx context.Context, method string, params, result any) error {
	c.pmu.Lock()
	c.nextID++
	id := fmt.Sprint(c.nextID)
	ch := make(chan message, 1)
	c.pending[id] = ch
	c.pmu.Unlock()
	defer func() { c.pmu.Lock(); delete(c.pending, id); c.pmu.Unlock() }()

	if err := c.send(outgoing{ID: json.RawMessage(id), Method: method, Params: params}); err != nil {
		return err
	}
	select {
	case <-ctx.Done():
		return ctx.Err()
	case m := <-ch:
		if m.Error != nil {
			return m.Error
		}
		if result != nil && len(m.Result) > 0 {
			return json.Unmarshal(m.Result, result)
		}
		return nil
	}
}

// Serve reads messages from r until it ends or ctx is cancelled, dispatching each to h. It waits for running
// handlers before returning.
func (c *Conn) Serve(ctx context.Context, r io.Reader, h Handler) error {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	var wg sync.WaitGroup
	defer wg.Wait()

	br := bufio.NewReaderSize(r, 1<<16)
	for {
		line, err := br.ReadBytes('\n')
		if len(line) > 0 {
			c.dispatch(ctx, line, h, &wg)
		}
		if err != nil {
			if errors.Is(err, io.EOF) {
				return nil
			}
			return err
		}
		if ctx.Err() != nil {
			return nil
		}
	}
}

func (c *Conn) dispatch(ctx context.Context, line []byte, h Handler, wg *sync.WaitGroup) {
	var m message
	if err := json.Unmarshal(line, &m); err != nil {
		if len(bytes.TrimSpace(line)) > 0 {
			_ = c.send(outgoing{ID: json.RawMessage("null"), Error: &Error{Code: CodeParse, Message: "invalid JSON"}})
		}
		return
	}
	switch {
	case m.Method == "" && len(m.ID) > 0: // a reply to one of our calls
		c.pmu.Lock()
		ch := c.pending[string(m.ID)]
		c.pmu.Unlock()
		if ch != nil {
			ch <- m
		}
	case m.Method != "" && len(m.ID) == 0: // a notification: handled in order, no reply
		_, _ = h(ctx, m.Method, m.Params)
	case m.Method != "":
		wg.Add(1)
		go func() {
			defer wg.Done()
			res, err := h(ctx, m.Method, m.Params)
			out := outgoing{ID: m.ID}
			if err != nil {
				var e *Error
				if !errors.As(err, &e) {
					e = &Error{Code: CodeInternal, Message: err.Error()}
				}
				out.Error = e
			} else {
				if res == nil {
					res = struct{}{}
				}
				out.Result = res
			}
			_ = c.send(out)
		}()
	default:
		_ = c.send(outgoing{ID: json.RawMessage("null"), Error: &Error{Code: CodeInvalidRequest, Message: "invalid request"}})
	}
}
