#!/bin/sh
# The rule that does not bend (docs/vision.md): nebula-agent and every dependency are pure Go.
# Fails when any non-standard package has cgo, C, C++, Fortran or prebuilt object (.syso) files,
# or when the built binary was not linked with CGO_ENABLED=0.
# Packages are listed as cgo would see them: with CGO_ENABLED=0 Go silently drops files that import "C", so a
# dependency carrying optional C code would otherwise pass unnoticed.
set -eu
cd "$(dirname "$0")"

offenders=$(CGO_ENABLED=1 go list -deps -test -f '{{if not .Standard}}{{if or .CgoFiles .CFiles .CXXFiles .FFiles .SysoFiles}}{{.ImportPath}}{{end}}{{end}}' ./...)
if [ -n "$offenders" ]; then
    echo "pure-Go rule broken: these packages carry C/cgo/.syso code:" >&2
    echo "$offenders" >&2
    exit 1
fi

if [ "$#" -gt 0 ]; then   # optional: the built binary to verify
    go version -m "$1" | grep -q 'CGO_ENABLED=0' || { echo "pure-Go rule broken: $1 was not built with CGO_ENABLED=0" >&2; exit 1; }
fi
echo "pure Go: ok"
