#!/bin/sh
# Sightline CI preflight: fail fast unless this job is running in the
# project's own toolchain image (ci/image/Dockerfile).
#
# The `sightline-ci` runner label is a contract: it must resolve to the image
# that installs the matching-build toolchain and libmupen64plus-dev. When the
# runner's label mapping drifts (2026-09-21: it pointed at a stock
# catthehacker/ubuntu image for a month), jobs fail late and confusingly -
# 98 Python tests pass, then a C compile dies on a missing header. This turns
# that into one line at the top of the job. It is the first step of every
# workflow that says `runs-on: sightline-ci`, so no workflow carries its own
# apt list.
#
# No ROM, no network, no side effects.
set -eu

fail() {
    echo "::error::sightline-ci job is not running in the Sightline CI toolchain image: $1"
    echo "(build it on the runner's Docker daemon: see ci/image/README.md)"
    exit 1
}

# The Dockerfile sets SL_CI=1; nothing else does.
[ "${SL_CI:-}" = "1" ] || fail "SL_CI is '${SL_CI:-}' (the image sets SL_CI=1)"

# The header the harness's input plugin compiles against (libmupen64plus-dev).
M64P_TYPES=/usr/include/mupen64plus/m64p_types.h
[ -f "$M64P_TYPES" ] || fail "$M64P_TYPES is missing (libmupen64plus-dev)"

# One compiler and one MIPS tool from the toolchain.
command -v gcc >/dev/null 2>&1 || fail "gcc is missing (build-essential)"
command -v mips-linux-gnu-ld >/dev/null 2>&1 || fail "mips-linux-gnu-ld is missing (binutils-mips-linux-gnu)"

os=$(. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME" || echo "unknown OS")
echo "preflight PASS: Sightline CI toolchain image ($os), SL_CI=1, $M64P_TYPES present, gcc $(gcc -dumpfullversion), $(mips-linux-gnu-ld --version | head -n1)"
