# Performance closeout and future graphics budgets

Status: readiness policy, with full-suite live qualification pending. This record
does not enable experimental rendering switches or change the supported default.

The current default renderer remains the reference for development. The optional
GPU transform and background-loop reduction remain experimental/default off.
Their earlier local gains were scene- and configuration-specific; they are not
a full-campaign performance promise. CPU optimization work is closed. It is not
necessary to exhaust all imaginable CPU transformations before graphics work.

The skydome pass's indexed submission and its float colour carrier are in the
tree on the same terms: experimental and default off, opt-in as `SL_SKY_BATCH=1`
with `SL_SKY_BATCH_FLOAT_COLOR=prod`, and the float value alone enables nothing.
Their evidence is one descriptive Frigate screen on one isolated build, where
two opposite-order blocks both qualified a gain (5.94% lower frame time over the
eight measured runs) while an earlier screen of the same pair on the same binary
credited no gain on that level. The integrated source passes 1008 native CPU
checks with no failures, and its isolated Windows build differs from that
measured executable in five bytes of PE header metadata alone, every section
and both runtime DLLs being identical; that recorded equivalence is what
carries the earlier evidence, and the integrated executable has not been
launched. Neither result is a performance promise or an adoption, and the
supported default is unchanged.

The latest expanded fog reduction experiment is parked. Twelve attempts completed,
and sampled raw images matched, but the intact-room repeats did not clear their
predeclared gain bar. Gas runs lacked the required native RGB/far-fog event files:
the launcher default `SL_RUN=0` prevented the writer from opening. The overall
result was inconclusive; matching pixels do not replace event coverage or show
an FPS benefit. The naive deferred upload and small GL state-query cache also
remain rejected/parked. No closed attempt or correction allowance is reopened.

Work that remains reasonable is guided by measurement: renderer CPU leaf stacks,
completed GPU span timings, draw submission/upload behavior and specific future
effects. A CPU-to-GPU architectural migration is a larger design decision because
existing CPU consumers still need some transformed data. It should not be hidden
inside another small optimization patch. No remaining speedup is assumed here.

The [readiness toolkit](../../tools/profiling/READINESS.md) defines a finite path:

1. Pin a representative workload, source/configuration and executable provenance.
2. Check event and metric prerequisites before allocating a live test window.
3. Collect clean correctness/budget comparisons separately from instrumented
   CPU/GPU diagnostics, when live execution is authorized.
4. Retain changes only with appropriate correctness and measured evidence.
   Otherwise reject or record uncertainty and stop that candidate.

The representative inventory covers Facility intact tanks and gas, Frigate sky
and background, dense Statue geometry, and combat effects. The clean 1080p60
target budgets both mean and p99 at 16.667 ms. These are future policy gates;
the suite is not yet live-qualified. A complete synthetic pass checks tooling,
not game readiness. Frame-time budgets and raw evidence remain useful as new
lights, shadows, particles and other graphics features are added.

A subsequent [bounded native tool validation](profiling-live-validation.md)
confirmed timestamp metrics and logging in Facility on one driver. It did not
reopen the closed fog candidate or qualify this representative suite.

Default-off asynchronous GPU spans complement CPU stack attribution. They report
submission wall time and completed GPU elapsed time separately. Instrumented
host timing does not become the clean frame budget. Unsupported or incomplete
GPU results must remain unavailable rather than becoming zero or CPU estimates.

Optimization is not mathematically exhaustible, and future graphics may introduce
new bottlenecks. The practical exit is a maintained representative baseline,
known limits, repeatable regression checks and explicit feature budgets. A later
performance pass requires a measured issue and a bounded candidate, not an
open-ended search for every possible gain.
