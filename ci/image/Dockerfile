# Sightline CI toolchain image.
#
# Mirrors docs/toolchain.md so a CI job builds in the same environment the
# reproduction guide describes. Ubuntu 24.04 to match the verified dev host.
#
# Deliberately contains NO ROM and no game assets. The base ROM is bind-mounted
# read-only at run time from the machine that owns it, per ROADMAP: nothing
# ROM-derived may be baked into an image, archived, or logged.

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Build toolchain (matching build) + emulator and virtual display (trace harness).
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        binutils-mips-linux-gnu \
        pkg-config \
        libcapstone-dev \
        python3 \
        python3-venv \
        git \
        make \
        ca-certificates \
        mupen64plus-ui-console \
        mupen64plus-data \
        libmupen64plus-dev \
        mupen64plus-rsp-hle \
        mupen64plus-video-all \
        xvfb \
    && rm -rf /var/lib/apt/lists/*

# act checks out into the workspace; nothing is baked in.
WORKDIR /workspace

# A real gfx plugin is required for the game to advance at all, so the harness
# needs a display even headless. Hashes are identical under Xvfb (verified).
ENV SL_CI=1
