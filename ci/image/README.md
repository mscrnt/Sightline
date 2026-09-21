# Sightline CI toolchain image

`Dockerfile` here is the one environment CI builds and tests in. It mirrors
`docs/toolchain.md` (Ubuntu 24.04, the MIPS binutils, capstone,
libmupen64plus-dev, Xvfb), plus Ubuntu's `nodejs` and `openssh-client` in
their own layer because act runs JavaScript actions such as
`actions/checkout@v4` with the job container's own `node`, and
publish-public pushes to the staging repository over `ssh://`, which git can
only do with an `ssh` binary present. It contains **no ROM and no game assets** — the ROM
is bind-mounted read-only at run time by whoever owns it, and nothing
ROM-derived is ever baked in, archived or logged.

## The `sightline-ci` label is a contract

Every first-party workflow in `.gitea/workflows/` that says
`runs-on: sightline-ci` is promising to run inside **this** image. The
runner (`ci/runner/docker-compose.yml`) maps the label to it:

    sightline-ci:docker://sightline-ci:24.04

`preflight.sh` enforces the contract: it is the first step of each of those
workflows and fails in one line — "sightline-ci job is not running in the
Sightline CI toolchain image" — unless `SL_CI=1` (set only by this
Dockerfile), `/usr/include/mupen64plus/m64p_types.h`, `gcc` and
`mips-linux-gnu-ld` are all present. It needs no ROM and no network, and the
workflows carry no apt lists of their own.

The image is local only: it is not pushed to any registry. act_runner uses a
local image when one exists (`force_pull` is off by default), so it must be
built on **the Docker daemon the runner's socket reaches** — see below — and
a fresh runner host has to build it before it can pick up a job.

## Rebuilding the image

Do this when `Dockerfile` changes, or when a runner host has no image yet.

1. Build on the daemon the runner uses. The runner container mounts
   `/var/run/docker.sock`; on the WSL2 host that is Docker Desktop's engine,
   the same daemon `docker images` shows from a Windows shell or from the
   WSL distro. Build from the repository root:

       docker build -t sightline-ci:24.04 ci/image

   `24.04` is the tag the runner mapping names; keep it. Add an immutable
   tag with the short hash of the Dockerfile's last commit so a running job
   can be traced to the definition it ran under:

       docker tag sightline-ci:24.04 sightline-ci:24.04-$(git log -1 --format=%h -- ci/image/Dockerfile)

2. Check what came out:

       docker image inspect sightline-ci:24.04 --format '{{.Id}} {{.RootFS.Layers}} {{.Config.Env}}'

   A build from an unchanged Dockerfile reproduces the same rootfs layer
   digests (the apt layers are cached); only the image id differs, from the
   attestation manifest. A changed toolchain layer is a new environment:
   trace fingerprints (`tools/trace/README.md`) may change with it, which
   is why additions go in their own `RUN` below it. Compare package sets
   before trusting a rebuild:

       docker run --rm sightline-ci:24.04-<old> dpkg-query -W > old.txt
       docker run --rm sightline-ci:24.04 dpkg-query -W > new.txt
       diff old.txt new.txt

   The previous image stays reachable by its per-commit tag, so a bad
   rebuild is undone with `docker tag sightline-ci:24.04-<old> sightline-ci:24.04`.

3. Prove it, with the tree mounted read-only and copied in:

       docker run --rm -v "$PWD":/src:ro sightline-ci:24.04 bash -c '
         cp -a /src/. /workspace/ && cd /workspace &&
         sh ci/image/preflight.sh &&
         python3 -m unittest discover -s tools/trace/tests -v 2>&1 | tail -3 &&
         make -C tools/trace/slinput && test -f tools/trace/slinput/slinput.so'

   Expected: `preflight PASS`, `Ran 98 tests ... OK (skipped=1)` (the skip is
   numpy, deliberately not installed), and `slinput.so` built. Note the full
   tree is required: several harness tests read `src/`.

4. No runner restart is needed for a rebuild under the same tag — each job
   starts a fresh container from whatever `sightline-ci:24.04` names at that
   moment. A restart is needed only when the **mapping** changes (next
   section).

5. Verify remotely: push a branch (harness-tests runs on every push) and read
   the job log. It must show `Start image=sightline-ci:24.04`, the preflight
   line, the Python result and the plugin build.

## Changing the label → image mapping

The mapping lives in `runner.labels` in the inline config at the bottom of
`ci/runner/docker-compose.yml`. `GITEA_RUNNER_LABELS` in the same file is
**registration-time only** (the act_runner image's entrypoint passes it to
`act_runner register` when `/data/.runner` does not exist yet, and never
again); it is kept identical so a fresh registration agrees with a running
one.

To apply a change, from `ci/runner/`:

    docker compose up -d

This recreates the runner container over the same named volume
(`runner_runner-data`). The registration credential in it is untouched, so
there is no re-registration and no token is needed. On start the daemon
logs `labels updated to: [...]`, rewrites `/data/.runner` with the new
labels, and declares them to Gitea; the runner keeps its id.

Read it back:

    docker logs sightline-runner 2>&1 | grep -E 'labels updated|declare successfully'

Gitea's API (`GET /repos/{owner}/{repo}/actions/runners`) shows only the
label *name* (`sightline-ci`); the `docker://` half is runner-side, which is
why the log line and the preflight exist.

Never `docker compose down -v`: that deletes the volume and de-registers the
runner.

## act_runner version

The runner is `gitea/act_runner:0.6.1`, pinned in the compose file on
2026-09-21. `0.6.1` and `latest` resolved to the same image index digest
(`sha256:b5c35d6b...`) that day, so the pin changed nothing about the
running runner; it only stops a future pull from moving it. Upgrading is a
deliberate step: check the release notes for label and config changes, then
change the tag and `docker compose up -d`.

The label semantics above were verified against that version's source
(`internal/app/cmd/daemon.go`, `scripts/run.sh`,
`internal/pkg/config/config.go` at tag v0.6.1), not assumed.

## Producing traces in this environment

`run.sh` runs a harness command inside the image with the ROM mounted
read-only from outside the workspace; see `docs/toolchain.md` section 8.
