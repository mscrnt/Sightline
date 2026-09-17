# Porting hazards: N64 decomp → native

A catalogue of the bug classes that actually bit while making a GoldenEye 007
decompilation run natively on x86, with the symptom each produces, the
mechanism behind it, and how to find the next one.

It is written for anyone doing the same thing to another N64 decomp. Nearly
every entry cost hours before it was understood and minutes afterwards, and in
almost every case the fix was small while the *diagnosis* was the whole job.

The single most useful sentence here: **every one of these failed silently.**
None produced a compiler warning, none crashed at the site of the mistake, and
several read as perfectly healthy in telemetry while rendering garbage.

---

## 1. Byte order, and where it is allowed to matter

The rule that made the rest tractable: **native word order everywhere**, with
big-endian file data swapped exactly once at its load choke point. The
alternative — keeping data big-endian and swapping at every read — dies as soon
as one function has two callers from different domains.

Everything below is a way that rule gets violated by accident.

### 1a. Positional byte reads of data already swapped to native order

**Symptom:** a parser that consumes commands, reports healthy counters, and
produces nothing correct.

A display list rewritten to native word order still has its opcode in the same
*word*, but no longer in the same *byte*. Code that reads `*((s8 *) gdl)` or
`((u8 *) gdl)[1]` gets the wrong end of the word.

Real instance: the background raycast read room display lists positionally, so
no triangle was ever recognised — every bullet passed through the world and no
decal ever appeared. The loop's exit test read the same wrong byte, so it also
ran off the end of the list, consuming 15× more commands than the list held.

**How to find them:** grep for byte and halfword indexing of anything that came
from a file. Convert to arithmetic extraction (`(w >> 24) & 0xff`) behind a
macro, with the original kept under the matching build's `#ifdef`.

**Trap:** a partial conversion looks finished. In that same file a correct
`SL_OP` macro already existed and was used for two other scans; only the
triangle walk was missed.

### 1b. Bitfield packing runs the opposite way

IDO allocates bitfields from the most significant bit down; GCC from the least
significant up. So a raw byte read over a bitfield selects a **different field**
on each platform — not a byte-swapped value of the same field.

Real instance: `((u8 *) &g_Textures[n])[0] & 0xf` selected `hitTexture` on N64
and `hitSound` natively.

**Count the blast radius before fixing.** Only 3 of 2698 entries had differing
nibbles, which made the fix provably narrow instead of frightening.

### 1c. Generated swappers resolve a union by its first member

**Symptom:** one field in a record is never swapped, and only the code paths
that read *that* field misbehave.

Real instance: a key record declared `union { s8 keyID; u32 keyflags; }`. The
swap generator saw the `s8`, decided the word was a byte field, and emitted
nothing. The door's copy of the same value *was* swapped, and the two are
ANDed — so no locked door in the game could ever open.

**How to find them:** enumerate every union reachable from a
file-loaded struct and check whether its members differ in width AND more than
one is actually read.

### 1d. A region is only word-swappable if it is uniformly word-shaped

**Symptom:** everything from one table is subtly wrong at once.

Real instance: a 0x1400-byte image bank was swapped as words. Its first 0xAC8
bytes are display lists — correct. The remainder is 0xC-byte declarations where
only the leading `u32` is a word and the following eight bytes are independent
`u8` fields. The blanket swap reversed each group of four, so every image in
the game got its width, height, level and format shuffled.

**Symptom worth memorising:** a quad drawn *the right size in the right place*
but untextured, taking a flat colour. That is a failed texture **binding**, not
missing content.

---

## 2. Reads that run off one symbol into the next

**Symptom:** a struct field arrives as zero, or as a plausible-but-wrong value,
with no crash.

The decomp frequently reads more bytes than a symbol declares, relying on the
N64 image's memory adjacency. Natively the linker is free to place those
symbols anywhere — and zero-initialised data lands in `.bss` while its neighbour
lands in `.data`, potentially megabytes apart.

Real instance: a 0x40-byte render template read from a **four-byte** symbol.
Its `flags` came out zero, and the renderer emits nothing without `flags & 1` —
so the player's gun was invisible.

**How to find them:** compare `nm -S` symbol sizes against the types being cast
onto them. This is mechanisable and worth doing as a sweep.

**They can also work by luck.** Three sites were found where the overrun reads
the right value for the wrong reason — one landing in `.data` alignment padding
that happens to be zero. Those are landmines: any relink changes the answer.

---

### 1f. Lists terminated by a negative sentinel

**Symptom:** an infinite loop, an out-of-range write, or silent corruption far
from the walk — but never a complaint about the list itself.

`-1` is `0xFFFFFFFF` (or `0xFFFF`), which is **byte-order invariant**. So a list
of indices that was never byte-swapped still terminates in exactly the right
place while every real entry in it is wrong. Nothing downstream notices until a
walk cycles or an index lands somewhere fatal.

This pattern accounted for three separate bugs in one codebase: a display-list
walk that recognised no triangles, a pathfinding search that scattered writes
through live objects, and a point-merging chain that hung the game. Each
presented completely differently.

**Treat every negative-terminated list as a place to check first**, and check
the *contents*, not just that the walk stops.

### 1e. Packing a pixel into a word and storing the word

**Symptom:** one image format renders with its channels permuted while every
other texture looks fine.

```c
dst32[x] = r << 24 | g << 16 | b << 8 | a;   /* R,G,B,A on MIPS; A,B,G,R on x86 */
```

This is the same hazard as 1a seen from the other side: the code is *writing*
byte-ordered data through a word. It is easy to miss because it looks like
plain arithmetic, and because most textures do not go through it — in
GoldenEye almost every texture is compressed or paletted, and the paletted path
in the same file already stored its bytes explicitly, so only a handful of
images ever exercised the word store.

**Write the bytes explicitly on the native branch**, keeping the word store for
the reference compiler.

**What made it hard to find:** the one texture that visibly used this path was a
red-on-black crosshair, and `ff0000ff` is unchanged by byte reversal. It decoded
perfectly and looked like proof the path was sound. Only an image with three
distinct channel values reveals the fault — so when a format looks correct,
check whether the sample could have failed.

## 3. Relying on the ABI rather than the language

**Symptom:** none, until the compiler changes.

Real instance: a function declared to return `Gfx *` with no return statement,
whose caller assigns the result. On MIPS the callee's return value is still in
`v0` at the epilogue, so it flows through and the ROM depends on it.

Notably the decomp *itself* carried a warning: "missing a return, this will
cause bugs on other compilers." Worth grepping a decomp for its own warnings
before hunting for new ones.

**Guard rather than fix.** Adding the `return` changes what the reference
compiler emits and breaks byte-identical matching. Keep the original under
`#ifdef` and correct only the native branch.

---

## 4. Address translation that is the identity natively

`osVirtualToPhysical` is the identity in a native shim, so a value it "already
translated" is a host pointer. Applying `PHYS_TO_K0`'s `| 0x80000000` to that
pushes it out of the 32-bit map.

**How to find them:** `grep -rn "0x80000000" src/ | grep "u16 \*"` and its
variants. Each hit is either already guarded or waiting.

---

## 5. Hangs are a normal failure mode, not an exotic one

N64 games spin rather than abort. This one's allocator ends in `while (1);` on
pool exhaustion, so a native "crash" often presents as a frozen window at 100%
CPU with no signal and no core.

**Consequences for tooling:**

- A crash reporter that only handles signals covers half the failures.
- A hang watchdog must watch **progress** (a frame counter), not wall-clock
  time — otherwise it cannot tell a hang from a player reading a briefing.
- Handlers must **re-raise** rather than `_exit()`, or no core is ever produced.
- `prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY)` at startup is one syscall and the
  difference between attaching a debugger to a hung window and having no
  recourse at all. With the common `ptrace_scope=1`, only a parent may attach.

---
## 6. Graphics state that is recorded but never applied

**Symptom:** a surface renders as a single flat colour, or a uniformly wrong
shade, with no error anywhere. Everything measurable reads healthy — coverage,
draw order, texture decode, alpha, opcode counts.

A display-list interpreter grows naturally as a *decoder*: it parses each
command and stores the state. **Storing is not applying.** A mode bit can sit in
a variable that nothing ever reads, and no tooling notices, because parsing it
correctly is what the decoder was tested on.

Real instance: `G_TEXTURE_GEN`. An environment-mapped surface carries `s=t=0`
in its vertices and expects the RSP to *manufacture* texture coordinates from
the vertex normals. The bit was tracked and never acted on, so every fragment
sampled texel (0,0) — a structured 54x54 image collapsed to its one top-left
pixel, which happened to be black. The surface drew as a flat dark rectangle.

**How to find them:** for every mode bit and every command field the interpreter
parses, grep for a *read* of the variable it is stored into. A field with
exactly one write and no reads is the entire bug. This is mechanisable.

### 6a. The meaning of vertex bytes depends on the mode

**Symptom:** a colour cast on one class of surface, and — less obviously — a
*suppression* of the texture's own variation.

The four bytes at offset 0x0C of a vertex are RGBA **or** a signed normal plus
alpha, depending on whether `G_LIGHTING` is set for that draw. Read as colour,
a normal multiplies the texture by a direction vector.

**The tell is arithmetic:** decode them signed and take the magnitude. Normals
cluster near 127 — unit vectors in 8-bit fixed point. Colour has no reason to
lie on a sphere. Two unrelated surfaces here (boot logos and a level's glass)
hit this independently before it was recognised as one class.

### 6b. Capturing part of a command

**Symptom:** a feature that cannot work, with the failure appearing somewhere
far from the omission.

`movemem` was handled for the viewport type and dropped everything else,
including the look-at vectors — so the reflectance basis never entered the
renderer *at all*, and a correct generator would still have had no inputs.
Separately, `gSPTexture` was decoded for `on` and `tile` while its S/T scale and
mip level were discarded.

**The tell:** an interpreter that reads two of a command's five fields. Dropped
fields are invisible until something needs them.

### 6c. Resource lookups that demand an exact address

**Symptom:** a plausible wrong image, rather than a failure.

Texture tiles legitimately address into the *middle* of a loaded block — a
mipmap's levels live at increasing offsets inside one load. A lookup keyed on
exact address match finds nothing and falls back to the block's base pointer
**with no offset**, so every level decodes out of the base level's bytes.

**The tell:** any fallback that returns something usable instead of failing
loudly. It converts a lookup miss into silent wrong data, and any experiment
built on top of it measures its own artefact.

### 6d. A command shape whose data is a whole value, not a field

**Symptom:** an effect never appears, and the state that would enable it is
provably never set. The parser looks correct, and *is* correct for the form it
was written against.

A set-mode command carries (shift, length, data), and masking the write to that
field is the faithful reading. But an SDK's composite macros pack a **complete**
mode value into one write with a wide length, and the low bits of that value
carry fields the mask excludes.

Real instance: `gDPSetRenderMode` passes shift 3, length 29. The bottom three
bits are the alpha-compare field, and the render mode a watch face uses embeds
alpha-compare dither there. Masking them off made dither unreachable, so a
random static burst the game genuinely produced rendered perfectly flat - for
the entire life of the port, on a screen nobody had reason to measure.

**How to find them:** census *every* shape of the command the game emits, not
only the shapes you handle. If a mode value the game demonstrably depends on can
never be produced by your parser, the parser is wrong regardless of how well it
matches the documentation for one form.

**The trap is in the fix, not the diagnosis.** "Just take the whole word"
satisfies the composite form and silently breaks the narrow ones - the explicit
two-bit and one-bit writes that must touch only their own field. The census
found exactly two emitted shapes, and the correct rule was narrow: that one
shape takes the whole word, everything else stays field-wise. Let the census
decide which shapes exist; do not generalise from the single case that broke.

A third reading - OR the data in unmasked - was refuted by a *consequence*
rather than by argument. It predicted the dither bleeding onto translucent bars
drawn immediately afterwards; those bars measured 0.38-0.68x the dithered
surface's frame-to-frame change, never at or above 1, so they were showing it
through rather than being cut out themselves. When several parser readings all
explain the bug you started from, look for one that predicts something else
observable.

---

---


## 7. Finding these deliberately

**Measure; do not reason.** The consistent experience: every measurement held
up, and nearly every conclusion reached by reading code was wrong — including
several confident ones with plausible mechanisms. Two published root causes for
one crash were both wrong; the format notes settled it in under a minute.

**Counters lie.** They measure that commands were *consumed*, not that they were
*interpreted*. Renderer bugs here read as "0 unknown opcodes, 0 decode
failures, 0 GL errors" while drawing garbage. Capture the frame and look at it.

**A headless renderer is not an unmeasurable one.** `xvfb-run` gives a virtual
display, so the full pipeline runs and frames can be captured with no physical
screen. Believing otherwise cost a day of asking a human for screenshots.

**Diff captures numerically.** Comparing two runs frame-by-frame with an RMSE
metric finds the exact frame where a setting changes the picture, instead of
eyeballing dozens.

**AddressSanitizer needs to be taught the game's allocator.** These games
sub-allocate from a few large arenas, so ASan sees one huge valid block and an
inter-object stray write passes silently. Annotating the arena — poison on
reset and free, unpoison on allocation — is what makes it catch the class it is
wanted for.

**Verify on the path you changed.** A culling change was verified against room
geometry, found harmless, shipped, and destroyed every character model. Room
and character geometry are different paths. "Measured, not feared" is worth
nothing if the measurement could not have failed.

**Consult format documentation before the decompilation.** The decomp tells you
what the code *does*, not what the format *is* — and when those disagree, which
is exactly what a byte-order bug is, reading source produces confident, wrong,
expensive answers.

**An instrument that cannot fail is not a measurement.** Three separate cases:
a stability gate that compared three renders of frame 0 and reported STABLE; a
twenty-level census run headless, where the renderer never executes, reporting
"none" for every level; and a coordinate generator that declined silently when
its inputs were missing, which is indistinguishable from being switched off.
**Any probe that can report "none" must first demonstrate it ran** — carry a
known-positive in the same run and require it to light up.

**Check the census key before trusting the count.** Keyed on texture plus
combiner, a survey collapses two independent models that share a material into
one row. Keyed on a vertex address, it splits one model into a row per
instance — because each instance rewrites the segment base before drawing, so
the resolved address is instance identity, not model identity. Both failures
produce a confident number, and they fail in opposite directions.

**Statistics from unmatched views cannot rank candidates.** The same cartridge
aperture, measured 60 frames apart, spanned nearly the entire range being used
to judge which implementation was closest. Absolute means and variances are
only comparable at a matched state — and a matched state has to be *proved*, by
scoring a known-good pair first to learn what "matched" scores.

**Name experimental arms for what they compute, not for the answer you expect.**
An arm called "the faithful one" cannot lose cleanly: if it wins you cannot
tell whether it won on measurement or on its name.
