# Renderer profiling toolkit

Standard-library Python tools for Windows CPU/GPU trace collection, offline
sample attribution, and repeatable presentation-time comparisons. The trace
collector does not change engine or game-launch code. No packages need to be installed.

For inert scene plans, event-producer preflight, clean frame budgets and separate
CPU/GPU metric gates, use the [offline readiness guide](READINESS.md). It also
records the supported reference, parked experiments and future graphics workflow.

The default-off renderer timing producer, its CSV schema, its frame identity and
its offline mock programs are described in
[native/README.md](native/README.md). A bounded Facility validation exercised the
timestamp path on one NVIDIA driver; see the [live validation record](../../docs/decisions/profiling-live-validation.md).
The representative performance suite remains prospective.

## Development and offline verification

From the performance worktree, run:

```powershell
D:\Projects\sightline\.venv\Scripts\python.exe -B tools/profiling/test_profiling.py
```

Every test input is generated in a temporary directory. The recorder uses a fake
backend, including failure and interruption tests. The test entry point blocks
all native subprocess creation. It never opens a real process, launches a game,
starts WPR, reads a real capture, or measures optimization performance.

`capture.py --help`, `analyze.py --help`, and `capture.py plan` are inert. Planning
reads file metadata/hashes and prints JSON; it does not query the process, create
the output directory, or run WPR. Live recording is a separate explicit command.

Live validation on the saved g255 build established native process identity,
liveness and foreground observations, a complete presentation-log window, and
4,467 explicit symbol/unwind ranges. An ordinary token could not start WPR
(`0xc5585011`). A separately authorized, single-use elevated recorder then saved
one CPU/GPU trace while the game and controller retained ordinary privileges.
That trace contains sampled CPU and DxgKrnl events with zero reported event or
buffer loss. Its requested observation was 20 seconds; startup, shutdown and
rundown extend the ETL to about 43.5 seconds. These results validate collection,
not an optimization benefit or automatic function/GPU attribution. No permanent
privilege change or scheduled task was installed.

## Capture workflow for a later profiling session

Use an ignored local output directory, such as `.sightline/chat-handoff/artifacts/`
or `build/`. Traces are system-wide and may contain unrelated application
activity. Keep them local. No recording command launches the game or any other
application, elevates permissions, changes settings, or adjusts sampling rates.

1. Choose a known binary and repeatable workload. Preserve its input/config,
   pose, workload identities, correctness evidence, foreground checks, and
   presentation log through the existing run harness. Profiling does not replace
   those gates. Select the capture interval in advance.
2. Check that the Windows CPU and GPU WPR profiles and an ETL viewer/exporter are
   available. WPR recording commonly requires an elevated terminal; the tool
   does not elevate itself. Other ongoing ETW recordings may conflict. A conflict
   is an error; the tool never cancels another recording.

   ```powershell
   python tools/profiling/capture.py doctor
   ```

   `doctor` reads current token capabilities without starting WPR. Exit 1 means
   the required system profiling privilege is absent; do not spend a game launch
   on that recorder session. Recording fails before opening the target when
   this minimum prerequisite is missing. No privilege is enabled or assigned.
3. Start the game separately using the approved foreground harness. Obtain its
   PID. Plan the capture with the actual executable path, PID and a new output
   directory. The parent directory must already exist:

   ```powershell
   python tools/profiling/capture.py plan --exe C:\local\g255.exe --pid 1234 --seconds 20 --out C:\local\profiles\capture-001
   ```

4. When live recording is permitted, explicitly select `record` and confirm the
   system-wide scope. Switch back to the game within ten seconds:

   ```powershell
   python tools/profiling/capture.py record --exe C:\local\g255.exe --pid 1234 --seconds 20 --out C:\local\profiles\capture-001 --confirm-system-recording
   ```

The recorder holds a query-only process handle and checks executable identity
and creation time. It waits up to ten seconds for foreground readiness without
starting a trace. It then starts **CPU + GPU** profiles with a fresh named WPR
instance, in file mode. Target liveness and foreground ownership are polled every
250 ms. A failure ends collection and saves the partial ETL with a failed result.
Foreground polling cannot prove ownership between observations.
If ordinary activation is refused, the capture tool does not force it. Arrange
foreground setup separately. A validated integration test used one normal click
on its own title bar while the PC was unattended, before the measured window;
it left the other foreground application open and restored the cursor afterward.

`--seconds` accepts 1 through 120 and bounds the observation interval after WPR
start returns; start/stop overhead is additional. File-mode WPR itself has no
disk cap. Use short captures with sufficient free disk space. This is diagnostic
instrumentation and can perturb performance; do not mix instrumented timing with
uninstrumented FPS results as though they were equivalent.

Output is created once and never reused:

- `plan.json`: binary and tool hashes, session name, exact command arguments,
  and a named recovery command.
- `start-pending.json`, `recording.json`, `stop-pending.json`: immutable checkpoints.
- `capture.etl`: WPR trace, including partial captures when possible.
- `result.json`: UTC stamps, command outputs, sampled foreground/liveness,
  target identity, ETL hash and failures.

There is one start attempt and at most one automatic stop attempt. No retry,
global cancel, data deletion, or automatic game restart exists. Ctrl-C requests
saving the current named instance. A failed/uncertain start, failed stop, forced
termination, or machine interruption may leave a recording active. Inspect the
saved plan and use **that instance's** recovery arguments manually. Never issue
an unscoped `wpr -cancel` to recover this tool's recording.

`RECORDED` means a nonempty ETL was saved after the observation interval. It does
not certify zero lost ETW events, usable CPU stacks, correct workload coverage,
GPU timing accuracy, normal game exit, or correctness. `trace_quality` stays
`UNREVIEWED`; review those properties in WPA before interpreting the capture.
GPU activity is system/driver scheduling evidence, not OpenGL call-level timing.

## Export and address attribution

Open an ETL in Windows Performance Analyzer (WPA). Inspect lost events, target
process lifetime, CPU samples, relevant threads/stacks, loaded module bases and
GPU activity. Resolve symbols where available. Use a saved WPA profile for
repeatable exports; WPR installation alone does not imply WPA/exporter exists.
WPA profiles depend on the installed tool version and selected tables and are
not fabricated here without a validating trace.

For a later automated export with an existing reviewed profile, Microsoft's
exporter command shape is:

```text
wpaexporter.exe -i capture.etl -range 5s 20s -profile renderer.wpaProfile -outputfolder exported -symbols
```

This toolkit does not execute the exporter. Retain the original ETL, profile and
CSV. Use unaggregated **leaf instruction-pointer samples**, not inclusive stack
rows, percentage rows, subtotals, or averaged functions. A stack's multiple
frames must not be counted as independent CPU samples. If the selected WPA table
does not expose leaf IP and loaded base, use WPA's resolved stacks directly;
do not invent addresses or feed an aggregate table to this importer.

`analyze.py samples` accepts canonical UTF-8 CSV (BOM is supported):

```csv
time_s,pid,tid,module_sha256,module_base,ip,weight_ms
1.25,1234,5678,<64 lowercase hex characters>,500000,501234,1.0
```

- `time_s` is a nonnegative trace-relative timestamp in seconds; windows are
  `[start,end)`. It is **not** the presentation log's clock.
- PID/TID are positive decimal integers, without names or thousands separators.
- `module_base` and `ip` are hexadecimal strings, optionally starting with `0x`.
  The base is the **loaded runtime base for that row**, including relocation.
- `module_sha256` identifies a pinned image; use `-` for an unidentified module.
  Other modules remain in the denominator so missing driver attribution stays
  visible. If its loaded base is also unavailable, `module_base=0` is an
  explicit unknown-base sentinel, permitted only with hash `-`. It is not a
  claimed load address and cannot resolve to target code.
- `weight_ms` is positive sampled CPU running-time weight in milliseconds,
  never a percentage. Convert export units explicitly before importing.

For a raw export with different headers, `normalize` accepts an exact column
mapping. It requires the seven fields `time_s,pid,tid,module,module_base,ip,weight_ms`.
For example, `columns.json` can map these to `Time,PID,TID,Module,Base,IP,Weight`.
All fields must map to distinct existing columns. There is no locale, units,
process-name, timestamp, or filename guessing.

`modules.json` maps exact exported module strings to pinned binary records:

```json
{
  "g255.exe": {
    "path": "C:\\local\\g255.exe",
    "bytes": 12345,
    "sha256": "<actual 64-character SHA-256>"
  }
}
```

The binary record can be copied from `plan.json`'s `target` or a symbol map's
`binary`. Verify from ETL module identity that the recorded module is that build;
a filename match alone does not prove it. Every mapped file is rehashed, and
unmatched export strings retain `-`. A mapping matching no modules is an error.

```powershell
python tools/profiling/analyze.py normalize --csv exported.csv --columns columns.json --modules modules.json --out samples.csv
```

The normalized CSV gets a `.provenance.json` sidecar pinning original CSV,
column map, module map and output. Keep both. Neither output is overwritten.

For MinGW builds that WPA cannot symbolize, use **saved** BSD-format GNU nm output
from `nm -n -S --defined-only --format=bsd`, without demangling. Import explicit
image bounds (hexadecimal) from build metadata:

```powershell
python tools/profiling/analyze.py symbols --binary C:\local\g255.exe --nm-text nm.txt --image-base 400000 --image-size 2000000 --out symbols.json
python tools/profiling/analyze.py samples --csv samples.csv --symbols symbols.json --pid 1234 --start 5 --end 20 --out hotspots.json
```

Image-base/size values above are examples, not this build's asserted bounds.
Positive-size T/t symbols form half-open function ranges. Aliases merge;
unknown extents, inter-function gaps and unsized symbols remain unmapped. Unequal
overlapping extents are refused. GNU nm may not supply sizes for some PE builds;
an entirely unsized map fails clearly. It is not extended to the next symbol.
The saved g255 build has unsized symbols, but its `.eh_frame` metadata supplies
explicit PC ranges. Save `objdump --dwarf=frames` output and provide it with the
same binary's nm output:

```powershell
python tools/profiling/analyze.py symbols --binary C:\local\g255.exe --nm-text nm.txt --frames-text eh-frames.txt --image-base 400000 --image-size 21f0000 --out symbols.json
```

These bounds are validated g255 metadata; use the actual metadata for another
build. Only an FDE whose start exactly matches a named code entry supplies a
range. Unnamed/cold ranges, padding and missing unwind entries stay unmapped.
Section labels are excluded. Unequal overlapping intervals are refused.

For builds with usable function debug information, a second option resolves
exact addresses from canonical sample CSV with a pinned GNU addr2line:

```powershell
python tools/profiling/analyze.py resolve --binary C:\local\game.exe --addr2line C:\local\binutils\addr2line.exe --csv samples.csv --pid 1234 --image-base 400000 --image-size 21f0000 --out exact-points.json
```

This offline command invokes only the explicitly selected debug reader, with
argument arrays and addresses on stdin. It never launches a target or records
performance. Inputs and tool are hashed; addresses are translated through the
runtime base before lookup. Output uses one-byte point intervals, which describe
only queried instruction addresses. Neighboring addresses remain unmapped. The
saved g255 renderer's addr2line lookups did not resolve function names, so its
validated path is the FDE+nm importer above; CRT debug lookups alone do not prove
renderer debug coverage.

Reports include every selected process/thread CPU sample, function weights,
unmapped rows and other/unidentified modules. `--tid` narrows to one thread;
the default includes all threads in the selected process. No automatic render
thread inference occurs. CSV rows outside the selected PID/time window are
still checked for malformed values. An empty selection, nonfinite values,
invalid target addresses or changed binaries are errors.

These are leaf-IP CPU weights, not inclusive call-tree costs or GPU timings.
Waits need scheduling/stack analysis in WPA; low sampled CPU weight alone does
not establish a GPU bottleneck. Sample attribution does not claim all renderer
work was measured or that a particular function caused a frame spike.

## Presentation timing and ABBA screening

The frame analyzer consumes the existing six-column `pres.txt`. R and E are
presentations; B is a separately validated budget record. It validates finite,
strictly increasing presentation timestamps and rejects empty/malformed logs.
Declare the regular-frame grid before comparing runs:

```powershell
python tools/profiling/analyze.py frames --pres pres.txt --lo 12001 --hi 18000 --frame-step 2 --out frames.json
```

The example selects `[12001,18000)` on an odd-frame grid. The selected regular
frames must match every expected frame exactly once and in order. Extra
presentations remain included. Intervals are consecutive selected R/E timestamps,
so N selected presentations produce N-1 intervals. The window never shifts or
shrinks to accommodate a truncated run. Reports include mean frame time, FPS
from that mean, nearest-rank p99 and reciprocal mean of the slowest ceil(1%)
intervals. Log digests are reported separately; zeroed digests do not prove
image equality. These digests also do not replace raw-image or game-state checks.

Four independently reviewed runs can be screened in declared A,B,B,A order:

```powershell
python tools/profiling/analyze.py compare --reports a1.json b1.json b2.json a2.json --contexts contexts.json --out comparison.json
```

`contexts.json` is an array of four objects, each with exactly these keys:

```json
{
  "workload": "scene/pose/window identifier",
  "config_sha256": "<actual SHA-256>",
  "input_sha256": "<actual SHA-256>",
  "binary_sha256": "<actual SHA-256>",
  "arm": "A",
  "foreground_passed": true,
  "normal_exit": true,
  "correctness_passed": true
}
```

The three booleans are supplied attestations from separate checks, not facts
inferred by this analyzer. Workload/config/input must agree across all four;
binary hashes must agree within each repeated arm. Every frame report is
recomputed from its pinned raw log, and selected identities must match exactly.
The screen requires both paired savings to exceed the larger within-arm repeat
spread. It reports values and a descriptive boolean; it does not calibrate a
general noise floor or approve an optimization. Every report retains
`optimization_verdict: NOT_EVALUATED`.

ETW and presentation timestamps have no assumed shared epoch. Do not align
them by subtracting first samples. A future explicit clock/marker correlation
is needed for claims tying an individual frame spike to a CPU/GPU event.

## References

- [WPR built-in recording profiles](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/built-in-recording-profiles)
- [WPR command options and named instances](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/wpr-command-line-options)
- [WPA Exporter and range units](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/exporter)
- [GNU nm output and symbol sizes](https://sourceware.org/binutils/docs/binutils/nm.html)
- [GNU addr2line](https://sourceware.org/binutils/docs/binutils/addr2line.html)
- [GNU objdump and DWARF frame output](https://sourceware.org/binutils/docs/binutils/objdump.html)
