#!/bin/bash
# Run a trace-harness command inside the canonical CI environment.
#
# Traces are only comparable within one environment (see emu_fingerprint), so
# this is how a developer produces traces that CI can actually verify against.
# The ROM is mounted READ-ONLY from outside the workspace and is never copied
# into the image, archived, or logged - see ROADMAP.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ROM="${BASEROM:-$REPO/baserom.u.z64}"
IMAGE="${SL_CI_IMAGE:-sightline-ci:24.04}"

[ -f "$ROM" ] || { echo "base ROM not found: $ROM" >&2; exit 1; }

exec docker run --rm \
    -v "$REPO":/workspace \
    -v "$ROM":/rom/baserom.u.z64:ro \
    -e SL_ENV=container \
    -w /workspace "$IMAGE" \
    bash -lc "
        test -x /workspace/.venv-ci/bin/python3 || python3 -m venv /workspace/.venv-ci >/dev/null
        xvfb-run -a --server-args='-screen 0 640x480x24' \
          /workspace/.venv-ci/bin/python3 tools/trace/trace.py $* --rom /rom/baserom.u.z64
    "
