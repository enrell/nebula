// nebula-agent is nebula's own agent: an ACP server on stdin/stdout, driven by the operator panel (or any ACP
// client). Pure Go by rule: always built with CGO_ENABLED=0 (see docs/vision.md).
package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"syscall"

	"github.com/enrell/nebula/agent/internal/acp"
	"github.com/enrell/nebula/agent/internal/agent"
)

// version is set at build time: -ldflags "-X main.version=..."
var version = "dev"

func main() {
	showVersion := flag.Bool("version", false, "print the version and exit")
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "usage: nebula-agent [--version]\n\nServes the Agent Client Protocol on stdin/stdout.\n")
	}
	flag.Parse()
	if *showVersion {
		fmt.Println("nebula-agent", version)
		return
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	conn := acp.NewConn(os.Stdout)
	if err := conn.Serve(ctx, os.Stdin, agent.New(conn, version).Handle); err != nil {
		fmt.Fprintln(os.Stderr, "nebula-agent:", err)
		os.Exit(1)
	}
}
