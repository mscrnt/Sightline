# The background CPU vertex loop reduced where its outputs are not read

**Status:** opt-in, default off, behind `SL_BG_GPU_BYPASS=1`. Requires
`SL_BG_GPU_XFORM=1`. Native build only; no effect on the matching build, on
headless runs or on trace replay. Not enabled by default and not proposed for
default enablement by this record.

**Scope:** `src/gfx/sl_gfx_dl.c`, the background pass (`bg_draw_pass`,
`bg_vertex_range`). No other subsystem is touched. No gameplay, AI, physics or
entity behaviour changes on either setting.

---

## 1. What it does

Binding the GPU vertex program (`SL_BG_GPU_XFORM=1`,
`src/gfx/sl_gfx_bggpu.h`) removes no CPU work on its own: the per-vertex loop
still runs, because its outputs have CPU consumers in the same frame. The
near-cut fill consumes a per-vertex near classification, and no readback of
the GPU's clip positions is permitted, so the loop cannot be left out.

This path narrows the loop instead of removing it. For a vertex that no mesh
of the walk can read the clip position of, the loop evaluates only the two
clip components the near class actually reads — `sl_wa_near_class` reads
`c[2]` and `c[3]` and nothing else — and writes one byte of class. It does not
write that vertex's clip position, raw depth, fog coordinate or pushed
position. Every other vertex, and every walk that fails a term below, takes
the loop exactly as it ships.

Per reduced vertex that is 2 of the loop's 4 matrix rows (8 of 16
multiply-adds) and 1 of 41 bytes of scattered per-vertex stores, plus the
per-vertex coplanar branch and the fog test. It is **a reduction, not a
skip**.

## 2. How to use it

```
SL_BG_GPU_XFORM=1      # the GPU vertex program - the prerequisite
SL_BG_GPU_BYPASS=1     # the reduced loop
```

Both are exact-string tests read once through a cached static, so no walk pays
a `getenv` and the hot loop never reads the environment. `SL_BG_GPU_BYPASS` is
on for the one-character value `1` and off for everything else, the empty
string and absence included.

Under the switch the path prints its own report to stderr, bounded, in three
kinds of line:

- one **eligibility** line per analysed item, giving the exact marked and
  unmarked vertex counts;
- a cumulative **reduced-walk** line at the first reduced walk and each new
  multiple of 300 thereafter, carrying the vertex split, the per-term refusal
  tally, repairs and unsound draws;
- a cumulative **refusal** line, on the same cadence in examined walks, for a
  run in which **nothing** was reduced.

Each of those two is printed **at most once per distinct value of the counter
it reports**. That is a property of progress rather than of the counter's
value, and it has to be stated because the two counters advance differently: a
reduced-walk count does not move on a refused walk, so a cadence testing only
`count % 300 == 0` would reprint on every frame while the count is parked —
which on a mixed item is most of them.

A default run prints none of them. The display-list census (`SL_DL_CENSUS=1`)
cannot carry this report, because the census is itself one of the readers that
refuses a reduced walk — so a run with the census on reduces nothing and would
report zeros.

### Why a refusal has a line of its own

The reduced-walk line is guarded on having reduced at least one walk, and the
eligibility line is printed from the per-item analysis, which is the **last**
term of the walk predicate — so a walk refused by any earlier term never
reaches it. A run in which every walk is refused therefore used to print
nothing at all, and **a missing line cannot be told apart from the switch being
unset, or from a binary built without this path.**

That is precisely the case a fogged stage produces by design, and it is the
case a fallback has to be judged on. So the refusal is stated positively
instead: how many walks the predicate examined, that none was reduced, the
per-term tally, and which term refused the most. Its guard is the exact
complement of the reduced-walk line's, so the two are mutually exclusive by
construction and a reader never has to merge them.

It is bounded in the unit it reports — the first refusal, then each new
multiple of 300 examined walks, and never twice for the same count — and it
prints fixed-width counters only, with no vertex, mesh or item quantity, so its
volume does not grow with the scene. The term names and their order are the
reduced-walk line's own, so one reader covers both.
The exact format strings are the parse contract; silence remains evidence of
nothing.

### Default off, and what that costs

With `SL_BG_GPU_BYPASS` unset the walk's `need` pointer is `NULL` and the
shipping loop runs. The whole residual cost is:

- one `getenv` on the first call in the process, then a static int load at the
  two call sites per background walk;
- one `need != NULL` test per vertex inside `bg_vertex_range`. `need` is a
  loop-invariant local assigned once before the loop, and the test
  short-circuits before `need[i]` is formed;
- one pointer store per walk (`vin->need = NULL`) into `g_bgv_in`, an existing
  file static already written field by field on the lines above it.

The change deletes no existing line, so no shipping statement is altered.

## 3. Which walks, and why each term is owed

A walk takes the reduced loop only if all of these hold. Every term is this
frame's own fact and already in hand where the loop runs.

| term | what it discharges |
|---|---|
| `SL_BG_GPU_XFORM` is on | the program must supply the rendered clip positions at all |
| `!pano` | the panorama shell keeps the clip positions, and `pano_coords` reads the fog coordinate and the shell positions |
| `!anim` | an animated walk's per-vertex displacement is CPU work the program does not carry |
| `!wbox && !fstats` | the item box, the nearest vertex and the fog/depth statistics are per-vertex reductions neither path writes without them |
| `fogm == FOG_NONE && !have_f && g_bg_env == NULL` | without a level band and without an environment row the fog array is neither uploaded nor bound |
| `fogless` | load-bearing twice; see below |
| the model positions are already resident (`g_bgv_state`, `g_bgv_asset`) | the GL admission later in the pass must succeed without a further allocation |
| the program is already compiled and linked (`sl_bggpu_state`) | likewise, without a compile |
| the per-item analysis succeeded | the per-vertex marking exists |

The program and the buffers are asked for by their **cached** answers, because
the walk's own are not established until the GL section, which runs after the
loop. The first walk of a run and the first walk of an item are therefore
never reduced.

**`fogless` is load-bearing twice.** The obvious half is the far pass: the far
pass requires `nfar != 0`, `nfar` rises only in the far clamp, and that clamp
is gated on `!fogless` — so on a fogless walk the far pass cannot fire and the
blanket far-range rewrites of `clip`, `zraw` and the pushed arrays are inert.
The less obvious half is that the shipping loop takes the near class from `c`
**after** that same clamp may have rewritten `c[2]`. On a walk where the clamp
can fire, a class computed from the unclamped component would be a different
answer for a vertex past the far plane. On a fogless walk the component the
class is taken from is the unclamped one on both paths. This is why the term
is `fogless` and not merely a bound on the far plane.

## 4. Which vertices, and the direction of conservatism

A mesh is **clip-dependent** when some consumer can read the walk's clip array
through it: no material or a mesh index past the pass's own per-mesh tables,
`SL_WA_MAT_DECAL`, `SL_WA_MAT_PANORAMA`, `SL_WA_BLEND_ALPHA`, a four-image
material, or a mesh the coplanar answer names. Each of those is the term the
pass's own per-mesh predicate tests, so "unmarked implies accepted by that
predicate" is structural rather than coincidental.

Every vertex a clip-dependent mesh's indices name is clip-dependent, and so is
every vertex carrying a two-sided push normal of its own — which is what keeps
the pushed count exactly the shipping value rather than a recount.

The analysis is `O(index_count + vertex_count)`, runs once per item, and is
cached on the item plus the four facts it is made of, so a change to any of
them rebuilds rather than being read through a stale answer. Rebuilds are
capped at 16; past the cap the analysis refuses and the walk takes the
shipping path, so a run that would thrash cannot pay more than the reduction
saves.

**Conservatism has one direction.** Marking a vertex that would not have been
read costs the saving, never the picture. The converse would be a wrong
picture, so it is counted rather than assumed: a mesh the marking did not mark
which the per-mesh predicate then refuses increments `g_bgb_broken`, says so
once on stderr, and must be zero in any validation gate.

## 5. Fallback

Three distinct fallbacks, all to the existing CPU path:

1. **Refusal before the loop.** Any failed term leaves `need` `NULL` and runs
   the full loop. Refusals are counted per category and reported — including
   when *every* walk is refused, which is the case a fogged stage produces and
   the one a fallback is judged on. See "Why a refusal has a line of its own"
   in section 2: the refusal is observed, never inferred from a missing line.
2. **The repair.** If the GL admission refuses a walk *after* the reduced loop
   has already run — the cached answers and the walk's own answering
   differently because a context went away between them — the loop is run
   again over the whole item with `need == NULL`. The reductions are not
   re-read and need not be: the maximum `w` is taken on both of the loop's
   paths, `nfar` cannot rise on a fogless walk, an animated walk is refused
   outright, and every pushed vertex is marked. A refusal therefore costs that
   walk's time and never its picture. Repairs are counted.
3. **Per-mesh.** Unsupported geometry or state inside an otherwise eligible
   walk is drawn from the clip array as before, because its vertices were
   marked and computed whole.

## 6. The compiler-arithmetic assumption

The two components the reduced path computes are the shipping loop's own
`j == 2` and `j == 3` iterations of `c[j] = x*C[j] + y*C[4+j] + z*C[8+j] +
C[12+j]`, written out in the same left-to-right order. They are
**bit-identical**, not merely algebraically equal, and that rests on the flags
`src/gfx` is compiled with (`tools/windows/build.ps1`, the `src/gfx` flag list
and the note above `OPTFLAGS_NATIVE`):

- `-msse2 -mfpmath=sse` — no x87 excess precision to round away differently;
- no `-march` naming an FMA-capable target and no `-mfma` — so there is no
  fused instruction for the compiler to contract the three products into;
- none of `-ffast-math`, `-funsafe-math-optimizations`, `-ffp-contract=fast`
  or `-flto` — so the association is not rewritten.

**This is a precondition of the path, not a tuning detail.** A build that adds
any of those flags invalidates the bit-identity argument rather than merely
changing the speed, and the reduced path must not be used in such a build
until the argument is redone. A build gate that greps the compile line for
`src/gfx/sl_gfx_dl.c` is the appropriate place to enforce it.

## 7. What has been observed, and what it does not say

One foreground ABBA quartet was run at one Frigate pose, windowed 1365x768,
vsync off, pacing off, display-list census off, on one machine. The arms were
*both switches absent* (A) against *both switches `=1`* (B).

| arm | mean host present interval | reciprocal |
|---|---|---|
| A1 | 3.6834 ms | 271.48 |
| B1 | **2.7757 ms** | 360.26 |
| B2 | **2.7776 ms** | 360.03 |
| A2 | 3.4436 ms | 290.39 |

The reduced path's own counters, identical in both B runs at the last printed
checkpoint (9900 reduced walks): 333560700 vertices computed whole,
1568051100 reduced — **82.4591%** of the accounted vertices reduced — with
**0 repairs** and **0 unsound mesh draws**, one standing refusal category
(`arrays`, constant at 1), and one item analysed in one rebuild. The three
early captures at pumped frames 2001, 2101 and 2201 were byte-identical across
all four runs.

### The limits of that result, stated rather than implied

- **It is not shipping FPS.** The figures are the reciprocal of a mean host
  present interval at one instrumented pose with pacing and vsync off. They
  are not a frame rate a player would see.
- **It is not GPU time.** `frame_ms` is host time including any wait inside a
  call. No GPU timer query exists in this path and none was added.
- **The two switches are not separated.** Both B runs set both. The reduced
  loop is refused outright without the transform, so no result here, and none
  obtainable from this arrangement, attributes any part of the difference to
  one switch alone. The contrast measured is *shipping default* against *GPU
  transform plus reduced CPU loop* as a bundle.
- **The baseline barely qualified.** The two A runs differ by 0.2398 ms
  against a 0.2500 ms inconclusive cutoff fixed before the runs — 0.0102 ms of
  headroom. The predeclared rule then set the required margin to that spread,
  so the bar moved with the noise. The margin was not marginal (the worse B
  run cleared the 3.2038 ms bar by 0.4262 ms), but this is not a clean
  two-arm separation, and the A drift has a direction (A1 slower than A2, with
  the two B runs between them agreeing to 1.8 µs) consistent with a warm-up or
  background-load settle that was not tested.
- **It is not a parity claim.** Three sampled frames at one pose on one level.
  Beyond those frames the picture is unverified in that window. No full-run
  image equality, no other level and no other pose was checked.
- **Coverage is a counter, not a forecast.** 82.4591% is the share of vertices
  the reduced path handled over the walks it reduced. It is not a share of
  frame time and not a speedup prediction. It is also a periodic checkpoint,
  not a guaranteed exit-time total.
- **It is a different binary.** That quartet measured a scratch build of the
  candidate source. This tree has not been compiled.

## 8. What is deliberately not here

- **The falsification mode.** The offline experiment carried a second switch
  value which wrote a clip position behind the eye for every bypassed vertex,
  so that any draw reading one lost its triangles and a picture could falsify
  the unread claim. It is not in this path: a mode whose purpose is to render
  a deliberately wrong picture does not belong in a shipped binary, and it
  pays a per-vertex store this path does not, so its timing was never this
  path's timing. The switch is a strict `1` test, so `2` is off. The accepted
  measurement did not use it either — both candidate runs were mode 1.
- **Measurement overlays.** No display-list breakdown instrumentation, capture
  overlay or profiling hook is part of this change.
- **A same-frame readback** of the GPU's clip positions to serve the near
  class. It replaces arithmetic with a synchronisation.
- **Turning the near-cut fill off** to widen eligibility. That changes the
  picture, so it is not a correctness-preserving option.
- **Restricting the whole-array upload** to the clip-dependent spans. Sound,
  and it would remove real bytes, but it is a second independent change.
- **Broader eligibility or retuned arithmetic.** Neither is part of this
  change.

## 9. Validation this record does not claim

This tree's integration has not been compiled, run or measured. The
integrated source was verified offline only, and it differs from the accepted
candidate in exactly three ways, each deliberate and each recorded above: the
measurement overlay the candidate inherited is absent, the falsification mode
of section 8 is absent, and the refusal line of section 2 is added. Nothing
else: the predicate's terms, the marking rules, the reduced rows, the repair
and the safety counters are the accepted candidate's.

Checked mechanically, with no compiler: the patches delete no line; bracket
balance per function and over one preprocessor branch; `printf` arity over the
whole file; the default-off guard inventory, which classifies every added line
and leaves the default path's residual cost to the three items in section 2;
that each refusal counter is incremented in exactly one place and the
predicate is one exclusive chain; and that the refusal line is bounded by its
cadence and names no per-item quantity. The accounting invariant the reports
rest on — examined equals reduced plus the nine refusals — follows from that
audit rather than from reading the code once.

Compilation, images and timing on this tree remain outstanding. No adoption,
default enablement or shipping-performance claim follows from this record.
