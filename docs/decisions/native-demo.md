# Making the native build playable

What changed on 2026-08-25 to turn the native skeleton from something that
replayed traces into something a person can start and play, and why each choice
went the way it did.

`native-boot.md` covers how the sim comes up. This covers what had to be true
on top of that for a demo.

## The window opened, flickered and closed

Two causes, both in the pump, and neither of them graphics.

The pump ran as fast as the host allowed — 3000 frames in 5 seconds — and
`sl_frame_limit` defaults to 1000. So a windowed run sprinted through its frame
budget in under two seconds and exited normally. Nothing had crashed. Nothing
was misconfigured. It had simply finished.

**Pacing lives at the presentation boundary, not the tick boundary.** Both the
recv-pump and `sl_frame_advance` bump `sl_frame`, so the counter climbs at
roughly twice the presented rate. Pacing in both slept twice per frame and
pinned the window at 114fps instead of 60 — which is how the second site was
found at all. The pump is where a frame is actually shown, so that is where the
sleep belongs; `sl_frame_advance` still needed the same *limit* rule, or a
windowed run walks out through that door at 1000 frames while the pump believes
it is unlimited.

**Pacing is gated on a live window, so replay still runs flat out** — measured,
600 headless frames in 1 second, unchanged. Phase 0 established that real time
must never reach the sim. Wall-clock reaching `sleep(2)` is not the same thing
as wall-clock reaching the sim, and `trace-verify` holding IDENTICAL across
15869 ticks is what makes that distinction a fact rather than an argument.

## Live input joins the recorded stream; it does not compete with it

`osContGetReadData` in the shim is the only controller producer natively — one
game-side caller, and the SDK copy is not linked. Live input is an `else if`
arm on that one function, reachable only when
`sl_input_data == NULL && sl_gfx_active()`.

That shape is deliberate. A recorded stream always wins, and headless has no
window, so the stream branch is byte-for-byte the code that was there before.
Determinism is preserved **structurally** rather than by remembering to be
careful — which matters, because determinism is the foundation the whole
project rests on and "we were careful" is not a property you can verify.

Control style *was* a runtime switch (`SL_KBM_STYLE`) because it is a saved
player option the platform layer cannot see. Measuring the recorded corpus is
what surfaced this: across `archives-sweep-agent`'s 9174 records the commonest
button words are C-up, C-up|C-left and C-up|C-right, which under 1.2 Solitaire
reads as walking and strafing but under 1.1 would mean holding look-down for
most of a level.

**Superseded.** An env switch still made the platform layer *guess*, and a
wrong guess is indistinguishable from broken input — it sends "walk forward"
as "look up", which is how it reached the player three times. The style is now
READ from the game each frame via `sl_game_control_style()`
(`src/native/sl_game_query.c`), so changing it in the in-game options re-maps
the devices with no restart. `SL_STYLE` remains as a forcing override for
testing; `SL_KBM_STYLE`/`SL_PAD_STYLE` are gone and warn if set.

## Boot every level, not just the slice

Facility crashed on a gas-releasing object that Archives does not have. That is
the whole argument for `make bootsweep`: "it boots" had only ever been
established for the levels somebody happened to try, and the levels somebody
happened to try were the ones that worked.

The first run found **6 of 20 campaign levels broken** (B-014), three of them
sharing a single root cause. A harness pointed at one level cannot see any of
that, and twenty levels is too many to check by hand. It is a smoke test, not a
gate — it answers "does the sim come up", and `trace-verify` answers whether it
is correct.

Its stage numbers come from the same `levelboot.py` table the trace harness and
the demo launcher use. Naming a level in three places is how the three drift
apart.

## Measure, don't read

Every native bug found this session was found by measurement, and the one
conclusion reached by reading code was wrong:

- **Facility's gas tanks.** `gedocs` named type 0x24; gdb supplied the bytes.
  The fix followed from arithmetic — wire `00 75 27 10` swept as a word lands
  `10 27 75 00`, which reads back as exactly the observed `obj`/`pad`.
- **The 97.6% unknown opcodes.** An opcode histogram split one number into a
  benign cluster (texture setup, the implement-next list) and a real bug
  (opcode `00` at 84.5%, the walker grinding through zeros at depth 2). Neither
  was visible in the aggregate.
- **The door that was never a door.** Read as `type=1` and dispatched to the
  Door arm, yet demonstrably not swapped as one. Both facts were true: a
  preceding CCTV record's handler swapped four words the struct declares but
  the *file* does not contain, and `SW32` over the neighbour's header turned
  type 5 into type 1.
- **The wrong one.** `langGet` returns NULL natively by design and its caller
  does not check, so `src` was the obvious null in the `textWrap` crash. A
  conditional breakpoint on `src == 0` never fired. Recorded in B-013 so the
  same inference is not made twice.

Two false alarms are worth the same warning. `textWrap` "crashing on the
objectives menu" turned out to require `SL_TRACE_OUT`; without it the same run
completes 45,000 frames. And firing appeared unwired — zero `Z` presses across
two runs — until the button was *held*: `xdotool click` releases in ~12ms and
the input layer samples at 60Hz (B-015). In both cases the measurement was
wrong before the code was.

## Seeing it changed everything

The section that stood here said "nobody has seen the window", listed the
compositor's refusals, and treated that as a limitation to work around. It was
the single most expensive assumption in the session.

**The fix was to stop going through the compositor.** `SL_SHOT` reads the back
buffer with `glReadPixels` from inside the process, before the swap. Whatever
comes back is what GL actually rasterised. Every external route had failed —
WSLg refuses root capture, Xvfb returns uniformly black, and `import` against
the window id produced 42 byte-identical PNGs across frames whose content
telemetry proved had changed completely. That last one is the instructive
failure: it *looked* like a successful capture of a static scene.

So the check that matters is not "did I get an image" but "do images from
different frames differ". Distinct MD5s across frames, every time.

Within an hour of being able to look, four bugs fell:

- **`GE_TRI4` index decode.** `w0` bits 16-23 are structurally zero in
  8340/8340 Facility commands, so the first index was always 0 and one triangle
  in four was pinned to vertex 0 of the batch. The fan of slivers that produced
  was the "black spiky shards".
- **CI palettes** decoded against the texture's own pixel data — right indices,
  palette made of pixels.
- **`OS_K0_TO_PHYSICAL`** still doing `ptr - 0x80000000` on a flat address
  space, so room display lists resolved to an unset segment and were dropped in
  silence. Room geometry had *never* rendered natively; the frames that looked
  like they worked were props floating with no floor beneath them.
- **The odd-row swizzle** applied to glyphs that were never swizzled — the flag
  was passed to every texture unconditionally.

**Every one of them was invisible to telemetry that read completely healthy:**
unknown opcodes 0, 916/916 triangles textured, 0 decode failures, 0 GL errors,
matrices `unresolved=0`, 76% of vertices inside the clip volume. Those counters
were all true and all measuring the wrong thing — that commands were CONSUMED,
not that they were INTERPRETED correctly. A vertex inside the NDC cube does not
imply a triangle that covers a pixel; a texture that decodes without error does
not imply it decoded to the right pixels.

Two of the four (CI palette, font swizzle) had the same shape: a well-tested
decoder handed the wrong thing by its call site. `sl_gfx_tex.c` has 593
self-test checks and is a verified inverse of Rare's own swizzle routine — and
it cannot see a format, a palette pointer, or a flag chosen by its caller. When
a decoder is proven and the output is wrong, suspect the call site.

The same trap has a scale variant. B-021 was first filed as "guards may sit
slightly below the floor", judged by eye at full-frame size. Enlarged 2.5x it is
not placement at all: heads and torsos are correct, legs are scattered polygons,
hands are detached. Trust the enlargement, not the thumbnail.

## What is deliberately not done

- **Lighting.** The scenes are dark. That is Phase 3 and rule 6 says stay in
  the current phase, so it was left alone deliberately rather than overlooked.
- **The `demo` target is not a `personal` build.** It reads the player's own
  ROM at runtime. No assets are in the repository and none are produced by it.
- **Audio.** Not attempted.
