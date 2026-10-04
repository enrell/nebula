// Package nebulatest is a stand-in nebula automation socket for tests.
package nebulatest

import (
	"bufio"
	"encoding/json"
	"net"
	"path/filepath"
	"testing"
)

// Fake starts a stand-in nebula socket for tests. handle returns a method's result, or an error string.
func Fake(t testing.TB, handle func(method string, params json.RawMessage) (any, string)) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "nebula.sock")
	ln, err := net.Listen("unix", path)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { ln.Close() })
	go func() {
		for {
			c, err := ln.Accept()
			if err != nil {
				return
			}
			go func() {
				defer c.Close()
				r := bufio.NewReader(c)
				for {
					line, err := r.ReadBytes('\n')
					if err != nil {
						return
					}
					var req struct {
						ID     any             `json:"id"`
						Method string          `json:"method"`
						Params json.RawMessage `json:"params"`
					}
					_ = json.Unmarshal(line, &req)
					res, errMsg := handle(req.Method, req.Params)
					resp := map[string]any{"id": req.ID, "ok": errMsg == ""}
					if errMsg != "" {
						resp["error"] = errMsg
					} else {
						resp["result"] = res
					}
					b, _ := json.Marshal(resp)
					_, _ = c.Write(append(b, '\n'))
				}
			}()
		}
	}()
	return path
}
