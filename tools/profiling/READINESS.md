# Offline renderer readiness and regression checks

`readiness.py` prepares and checks evidence. It never launches a process, creates
a graphics context, captures the desktop, starts WPR, changes privileges or
updates a baseline. Its tests use fabricated clocks, pixels, events and files;
they do not measure optimization performance. Python's standard library is the
only dependency. The older [trace toolkit](README.md) remains available for
separately authorized CPU stack attribution.

## Supported reference and stopping point

The existing default renderer is the reference. Experimental GPU switches remain
opt-in; this toolkit promotes none of them. The latest fog extension is parked:
its twelve-attempt experiment closed inconclusive, without repeatable intact-room
gain and without the required gas event logs. Matching sampled images do not
repair missing event evidence. Earlier deferred uploads and the small state-query
cache also stay parked. See [the closeout policy](../../docs/decisions/performance-readiness.md).

The offline increment stopped at source, tooling, documentation and synthetic
verification. A subsequent [bounded native validation](../../docs/decisions/profiling-live-validation.md)
exercised the timestamp producer, event logging and frame joins in Facility on
one NVIDIA driver. It does not qualify the complete scene suite, a live frame
budget or campaign coverage. Repeatable profiling overhead and other hardware
paths remain unqualified. Historical results are not automatically adopted as a
new baseline.

## Suite and budget policy

[readiness-suite.json](readiness-suite.json) versions five workloads:

| Workload | Purpose | Timing use |
| --- | --- | --- |
| Facility tanks | Intact tank-room geometry and ordinary fog | Clean frame budget |
| Facility gas | Before destruction, gas buildup and full gas | Correctness and diagnostic timing |
| Frigate deck | Background and sky control | Clean frame budget |
| Statue dense | Dense geometry, transparency and depth fallback | Clean frame budget |
| Combat effects | Actors, muzzle flashes, particles and future lights | Clean frame budget plus separate CPU/GPU profile |

These are benchmark coverage recipes, not hand-authored per-level shipping
behavior. A pinned pose/input and explicit stage/pose/seed attestation are needed
for every run. The combat replay and dense Statue coverage are prospective until
reviewed with native evidence. Gas coverage excludes damage/death with
invincibility and the exact adjacent tank-destruction frames.

1080p60 is a **prospective** roadmap target, not a qualified result: once it is
measured on live evidence, candidate mean and nearest-rank p99 frame time must
each be at most 16.667 ms. Nothing in this toolkit has qualified it, no run here
establishes that the renderer meets it, and the regression allowances are
configured policy rather than a measured noise model. 1440p60 is an equally
prospective optional profile and not a supported-performance promise. Host
presentation intervals include work and waiting; they are neither CPU execution
time nor GPU elapsed time.

Four independently collected runs use order A, B, B, A. Each candidate must meet
both the absolute budget and the relative regression screen. For each of mean
and p99, the relative bar is the worse baseline plus the largest of baseline
spread, the configured floor and the configured percentage of the better
baseline. Defaults are 0.1 ms / 2% for mean and 0.25 ms / 5% for p99. These are
explicit policy allowances, not a measured noise distribution or statistical
significance test. A noisy baseline widens the screen; inspect both raw repeats.
This is a regression screen, not evidence that a candidate improved performance.

A renderer-wide change needs the full representative suite. A narrow feature
can start with its affected workloads; an incomplete suite stays explicitly
incomplete. Intentional new graphics costs need an explicitly reviewed,
versioned feature budget and visual reference. The tool still reports the old
relative regression; it does not silently treat that cost as free or update
the baseline. This avoids requiring an exhaustive profiling campaign for every
small graphics edit while preserving the meaning of a complete suite result.

Clean runs must omit `SL_RENDER_PROFILE`; instrumented runs are always
`DIAGNOSTIC_ONLY`, even if the numbers meet the frame budget. GPU profile data
must be completed elapsed-query results. Pending or unsupported values remain
blank and block required GPU coverage; host time is never substituted. CPU
submission is wall time, not sampled CPU execution. Scope times can nest and
are never summed by the analyzer.

### What a profile row measures, and what it does not

The `frame` scope opens where the renderer begins a frame and closes where it
presents. The native pump runs the pacing wait, the game and phase work, the
developer probes and the frame-counter increment between those two points, so
`cpu_submit_ms` for that scope is a **clear-to-present CPU wall span**: pacing
time, game CPU time and any unrelated CPU gap are inside it. It is not renderer
submission cost and not sampled CPU execution time. `gpu_elapsed_ms` is the
elapsed span between two points on the GPU timeline, which is **not exclusive
GPU-busy time** - unrelated work may occupy the GPU within it.

The two spans overlap. Subtracting one from the other does not isolate a
bottleneck and must not be reported as doing so; a narrower renderer-only figure
needs a narrower scope, not arithmetic over these two. Held frames are recorded
in the separate `frame-held` series because no regular present is noted for
them, so the `frame` series stays exactly the regular-present grid.

## Inert planning

Run from the performance worktree. `-I -B` is supported:

```powershell
python -I -B tools/profiling/readiness.py catalog --out C:/local/catalog-001.json
python -I -B tools/profiling/readiness.py plan --scene facility-gas --profile 1080p60 --bindings C:/local/bindings.json --out C:/local/plan-001.json
python -I -B tools/profiling/readiness.py preflight --plan C:/local/plan-001.json --out C:/local/preflight-001.json
```

Every output is create-only. Parents must exist. A plan's future output root
must be new and outside the source tree. Planning pins the suite, executable,
pose, input, harness and event-producer source; it does not create the run root.
Example **bindings** (all paths and identity values must be replaced with actual
reviewed values; this is not a launch command):

```json
{
  "binary": "C:/local/bin/reference.exe",
  "source_root": "D:/Projects/sightline-v040",
  "machine_id": "reviewed-machine-and-driver-id",
  "input": "C:/local/input.txt",
  "pose": "C:/local/pose.txt",
  "harness": "C:/local/run.ps1",
  "output_root": "C:/local/runs/gas-a1",
  "measurement_mode": "clean",
  "workload": {"stage": "factank", "pose_id": "reviewed-tank-room", "seed": "reviewed-seed"},
  "base_environment": {"SL_RUN": "0"},
  "overrides": {"SL_RUN": "1", "SL_RUN_DIR": "C:\\local\\runs\\gas-a1\\native-events"},
  "counter_mapping_reviewed": true,
  "metric_capabilities": ["host_frames"]
}
```

Paths in the effective environment are exact native path strings, so on Windows
use the same normalized backslash spelling that the plan uses for its output
root. Configuration is literal JSON, never evaluated shell text. Record the
complete relevant `SL_` launch environment, not merely the differences between
arms. `workload` is supplied launcher/fixture metadata, not proof of native game
state. Binary hashes do not establish that a binary was built from the pinned
source; preserve the build provenance separately.

The event adapter recognizes one byte-pinned existing harness. Its default
`SL_RUN=0` suppresses native logging. Gas plans require an explicit final
`SL_RUN=1` override and a private `output_root/native-events` root. Native logging
creates a run subdirectory there; the receipt must name that actual events file.
Unknown harnesses/source layouts refuse instead of assuming logging works.
Preflight checks source prerequisites and configuration only. It cannot prove
that the saved binary emits an event. Capabilities are declared expectations;
postflight still requires the raw producer files.

The same source contract carries the **frame-pump order guard**. The pinned
event-record counter is one series and the metric CSV joins on another, so
pinning the counter's name without the statement order around it would pin a
counter whose meaning that order decides. Inside the pump block delimited by
`int held = sl_gfx_active() && sl_pres_hold();` and the `limit 0 means run
until` comment, the guard requires exactly one end-frame call, the regular
present note spelled as the source spells it, the begin-frame call, and the
frame-counter increment, in that order. If the increment moves ahead of either
frame boundary, every emitted row is attributed to the wrong frame and no
downstream check can see it, so the contract refuses and names adapter review.
Unknown, duplicated or respelled statements refuse the same way, and
`frame_identity_order` in the plan records what was required.

This guard is read-only text inspection. It is not a proof of C semantics: it
cannot establish which statements execute, how often, under which condition, or
that a shipped binary was built from this text. No clock ratio, frame offset or
source line number is assumed anywhere in it.

For a `profile` plan, declare `cpu_submit` and `gpu_elapsed`, and set final
`SL_RENDER_PROFILE` to the exact native `output_root/metrics.csv` path. All
required spans must have ready GPU values on the selected frame grid. A clean
plan must have no profiler environment variable and no metric CSV receipt.

## Supplied evidence and offline checks

A future, separately authorized launcher must preserve `pres.txt`, `launch.txt`,
`foreground-log.txt`, strict raw P6 PPM captures and a `receipt.json` under each
planned root. This tool does not assemble or execute that launcher. Do not replay
closed experiments or treat a successful preflight as permission to collect.

Receipt schema `sightline-performance-receipt/1` has these exact fields:

- `plan_sha256`: canonical JSON identity (`readiness.identity(plan)`).
- `binary_sha256`, `machine_id`, `resolution: [width,height]`, `workload` and
  `effective_environment`: match the plan and actual launcher observations.
- `normal_exit: true`, `foreground_qualified: true`,
  `primary_work_area: [left,top,right,bottom]`.
- `captures`: ordered objects `{ "path": "shot-000.ppm", "frame": 2001,
  "read": 1500 }`, using the producer's pumped-frame and input-read identities.
- `events_path`: relative native events path for gas, otherwise `null`.
- `metrics_path`: `"metrics.csv"` for profiles, otherwise `null`.
- `evidence_kind`: `"synthetic"` or `"live"`. Synthetic results cannot qualify
  a renderer; a live label is an attestation, not automatic provenance proof.

Frame counters are not interchangeable: the native watch event uses
`sl_vi_advances` / `read=`, while `sl_frames_completed()` is the pumped-frame
counter. Never infer a fixed conversion. The default regular-only grid is
12001 through 17999, step two. Static image frames are 2001, 2101, 2201, before
that timing window. Gas samples cover 100..20000 every 300 pumped frames, with
one of the two declared parity phases (67 images). The complete schedule, raw
pixels, capture identities and RGB/far-fog timelines must match every arm.

`launch.txt` must include exactly one `exit_code 0` and `timed_out False`.
Foreground checks read the existing launcher header and all live polls after
the six-second grace period, requiring visible, unminimized, unobscured game
ownership within the recorded primary working area. Polling is evidence only
at the observation times; it cannot prove focus between polls.

GPU CSV columns are exactly:

```text
frame,scope,cpu_submit_ms,gpu_elapsed_ms,gpu_status
```

`scope` uses lowercase letters, digits, `_` or `-`. `frame` must use the reviewed
pumped-frame identity. Values are finite and nonnegative; statuses are `ready`,
`pending`, `unsupported`. A missing GPU value is blank. One row per frame/scope;
the `frame` scope must cover the selected regular frame grid. Extra rows outside
that grid are validated but excluded, not reinterpreted as regular frames.

These are coverage checks, not per-row causal truth. Two separate mechanisms do
the work. The window's phase filter selects rows where `(frame - lo) % step` is
zero, so a uniformly shifted file is off-phase at every row and the window
selects nothing. Independently, `validate_run` requires the `frame` series to
cover the declared grid exactly, **for any step** - that is what still refuses a
missing or extra grid frame when `step` is 1 and the phase filter cannot
discriminate at all. So the phase filter alone does not police the native join:
a broad `feature_metrics` import at `step` 1 is an import, not a validator pass,
because only `validate_run` applies the exact grid-coverage requirement.

Neither mechanism can tie a row's numbers to the work of the frame it names. A
file carrying the right frame numbers with values belonging to other frames
validates, because the attribution is not derivable from the CSV. That property
has to hold in the producer, which is what the two offline guards below exist
for.

```powershell
python -I -B tools/profiling/readiness.py validate --plan C:/local/a1-plan.json --root C:/local/runs/a1 --out C:/local/a1-validation.json
python -I -B tools/profiling/readiness.py compare --plans C:/local/a1-plan.json C:/local/b1-plan.json C:/local/b2-plan.json C:/local/a2-plan.json --roots C:/local/runs/a1 C:/local/runs/b1 C:/local/runs/b2 C:/local/runs/a2 --out C:/local/comparison.json
python -I -B tools/profiling/readiness.py gate --comparisons C:/local/comparison.json --out C:/local/gate.json
```

The last example is intentionally incomplete: a complete gate needs each of the
five clean comparisons plus a separate combat-effects profile comparison, all
with identical arm binaries, machine and policy revision. It recomputes every
report from pinned raw evidence and rejects missing workloads, edited summaries,
cross-workload run reuse and mixed synthetic/live inputs. Inputs are rehashed
after each run validation. Keep producers stopped while analyzing saved files.

Exit 0 means a valid output was written (including explicitly diagnostic results
or an inert prepared plan). Exit 1 means blocked, incomplete, synthetic-only
suite, over budget or regression. Exit 2 means invalid input or I/O failure.
Always read the output status: no command grants release, adoption, automatic
rebaseline, default enablement or further launch authority.

## Future graphics changes

Attach a versioned input/pose/seed, binary/build provenance, machine/driver/config,
raw correctness evidence and both mean/p99 results to each change. Obtain clean
budget runs separately from diagnostic CPU stacks and GPU query timings. For an
intentional visual change, review/version the new visual reference explicitly;
do not increase an image tolerance to hide the change. Effects should consume
the remaining frame budget, not rely on an assumed percentage of historical FPS.

Reopen optimization only for a measured missed budget, a material regression or
a measured hotspot with a concrete candidate. A finite pass ends with a retained
gain, a rejected candidate or a documented inconclusive result. New graphics can
change bottlenecks, so this closes current speculative work without promising
that future features never need performance checks.

## Offline tests

```powershell
python -I -B tools/profiling/check.py
```

Tests fabricate all evidence in disposable temporary roots and forbid native
subprocess launches. They cover the full JSON CLI path, strict images/events,
missing producers, delayed GPU values, tail-only regressions, clean/profile
separation, exact sample schedules, immutability and complete synthetic suites.
No result is an optimization measurement.

The check command runs the Python tests in its own process without launching a
native subprocess. For the native producer, the optional `--native-mocks` mode
additionally compiles only the fixed synthetic test sources under
[native/](native/README.md), inspects each resulting binary's DLL imports, and
only then executes it:

```powershell
python -I -B tools/profiling/check.py
python -I -B tools/profiling/check.py --native-mocks --out C:/local/new-mock-check
```

Two executables are built, not one, because each fixed test source carries its
own `main()`:

| binary | source | covers |
| --- | --- | --- |
| `renderer-profile-mock` | `native/test_sl_gfx_profile.c` | the producer unit suite: 16 cases, 103 checks |
| `frame-identity-regression` | `native/regress_frame_identity.c` | the **native counter regression**: 33 checks |

The regression drives the pump's real ordering - end the previous frame, note
the regular present, begin the next, then advance the counter - against the
synthetic counter, and asserts that each row's frame equals the counter value at
the moment that frame ended, that a nested scope joins its enclosing frame, and
that a held frame never appears in the regular-present series. It is the offline
check that fails if the pump is reordered, which is why both binaries must run:
dropping either silently retires everything that source covers. A missing fixed
source refuses rather than building a smaller suite, and every fixed input is
pinned in the result.

Both runs refuse graphics/SDL imports, existing output roots and output inside
the source tree; the import inspection is strictly before each launch. The local
MinGW32 compiler is used, and `--compiler` can name another installation with
`objdump.exe` beside it. Outputs and failures are preserved without retry, and a
failed binary leaves `result.json` with status `FAILED` and the commands so far.

These mocks fabricate GL entry points, clocks, environment and frame counters.
This optional mode does not compile or execute a game binary, load a graphics
API, start a window, or produce any optimization measurement. The separate
[native validation record](../../docs/decisions/profiling-live-validation.md)
documents the real timestamp-path check. That live check is not part of this
offline command and does not authorize another live run.
