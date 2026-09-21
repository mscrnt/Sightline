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

# Node and an SSH client for the runner, not for the toolchain. act runs
# JavaScript actions (actions/checkout@v4 is one) with the job container's
# own `node`; without it every job dies at checkout with "node: executable
# file not found". publish-public pushes to the staging repository over
# ssh:// with GIT_SSH_COMMAND, and git needs a real `ssh` for that; without
# it the first master publication under this image failed with "ssh: not
# found" (run 1006). A separate layer so the toolchain layer above keeps its
# digest - the emulator and compiler versions the trace fingerprint depends
# on do not move when these are added. Ubuntu's packages, no third-party
# repository.
RUN apt-get update && apt-get install -y --no-install-recommends \
        nodejs \
        openssh-client \
    && rm -rf /var/lib/apt/lists/*

# act checks out into the workspace; nothing is baked in.
WORKDIR /workspace

# A real gfx plugin is required for the game to advance at all, so the harness
# needs a display even headless. Hashes are identical under Xvfb (verified).
ENV SL_CI=1
