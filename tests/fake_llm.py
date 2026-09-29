#!/usr/bin/env python3
"""Tiny fake of the Anthropic / OpenAI-compatible / Gemini chat endpoints. Logs each request to $1 (JSON lines)."""
import json, sys
from http.server import BaseHTTPRequestHandler, HTTPServer

LOG = sys.argv[1]
PORT = int(sys.argv[2])

def plan(body):
    """Scripted operator: calls a tool chosen by the latest user prompt; answers once the last message is a tool result."""
    if "tools" not in body and "system_instruction" not in body:
        # Code Assist wraps the real request under "request"
        body = body.get("request", {})
        if "tools" not in body: return None
    if "input" in body:   # OpenAI Responses API (codex backend)
        msgs = body["input"]
        def has_result(m): return m.get("type") == "function_call_output"
        def text_of(m):
            return next((b.get("text", "") for b in m.get("content", [])), "")
        if msgs and has_result(msgs[-1]): return None
        prompt = next((text_of(m) for m in reversed(msgs) if m.get("type") == "message" and m.get("role") == "user"), "")
    else:
        msgs = body.get("messages") or body.get("contents") or []
        def has_result(m):
            c = m.get("content") if "content" in m else m.get("parts")
            if m.get("role") == "tool": return True
            return isinstance(c, list) and any(isinstance(b, dict) and (b.get("type") == "tool_result" or "functionResponse" in b) for b in c)
        if msgs and has_result(msgs[-1]): return None
        prompt = ""
        for m in reversed(msgs):
            if m.get("role") != "user" or has_result(m): continue
            c = m.get("content") if "content" in m else m.get("parts")
            if isinstance(c, str): prompt = c
            elif isinstance(c, list):
                for b in c:
                    if isinstance(b, dict) and "text" in b: prompt = b["text"]; break
            if prompt: break
    if "launch a shell" in prompt: return ("launch_agent", {"agent": "shell"})
    if "close pane" in prompt: return ("close_pane", {"pane": int(prompt.split("close pane ")[1].split()[0])})
    return None

class H(BaseHTTPRequestHandler):
    def plan(self, body): return plan(body)
    def log_message(self, *a): pass
    def do_POST(self):
        raw = self.rfile.read(int(self.headers["Content-Length"])) or b"{}"
        try: body = json.loads(raw)
        except json.JSONDecodeError: body = {"_raw": raw.decode(errors="replace")}
        with open(LOG, "a") as f:
            f.write(json.dumps({"path": self.path, "headers": {k.lower(): v for k, v in self.headers.items()}, "body": body}) + "\n")
        if self.headers.get("x-api-key") == "bad" or self.headers.get("Authorization") == "Bearer bad":
            self.send_response(401); self.end_headers(); self.wfile.write(b'{"error":{"message":"invalid api key"}}'); return
        text = "echo fake-reply"
        tool = self.plan(body)
        if self.path.endswith("/messages"):
            out = {"content": [{"type": "text", "text": text}]}
            if tool: out = {"content": [{"type": "text", "text": "on it"}, {"type": "tool_use", "id": "tu1", "name": tool[0], "input": tool[1]}]}
            elif "tools" in body: out = {"content": [{"type": "text", "text": "operator-done"}]}
        elif self.path.endswith("/chat/completions"):
            out = {"choices": [{"message": {"content": text}}]}
            if tool: out = {"choices": [{"message": {"content": "on it", "tool_calls": [{"id": "tc1", "type": "function", "function": {"name": tool[0], "arguments": json.dumps(tool[1])}}]}}]}
            elif "tools" in body: out = {"choices": [{"message": {"content": "operator-done"}}]}
        else:
            out = {"candidates": [{"content": {"parts": [{"text": text}]}}]}
            if tool: out = {"candidates": [{"content": {"role": "model", "parts": [{"text": "on it"}, {"functionCall": {"name": tool[0], "args": tool[1]}}]}}]}
            elif "tools" in body: out = {"candidates": [{"content": {"parts": [{"text": "operator-done"}]}}]}
        data = json.dumps(out).encode()
        self.send_response(200); self.send_header("Content-Type", "application/json"); self.end_headers(); self.wfile.write(data)

HTTPServer(("127.0.0.1", PORT), H).serve_forever()
