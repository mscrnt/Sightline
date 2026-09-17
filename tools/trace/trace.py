#!/usr/bin/env python3
"""Sightline trace harness CLI: capture and compare deterministic state traces."""

from __future__ import annotations

import argparse
import subprocess
import hashlib
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from sltrace.symbols import SymbolTable
from sltrace.state import StateReader, Memory, SCHEMA_VERSION
from sltrace.emu import Emulator, GFX_PLUGIN, RSP_PLUGIN
from sltrace.emu_libretro import LibretroEmulator, DEFAULT_CORE
from sltrace.traceio import TraceWriter, TraceHeader, Trace
from sltrace.diff import find_divergences, format_report

REPO = Path(__file__).resolve().parents[2]
DEFAULT_ROM = REPO / "build/u/ge007.u.z64"
DEFAULT_MAP = REPO / "build/u/ge007.u.map"


def _map_for(rom, mapopt):
    """Resolve symbols against the ROM being run, not the matching build.

    direct-boot ROMs carry extra defines, which changes code size and shifts
    symbols above some address. Reading them through the matching build's map
    does NOT fail loudly - it fails silently and expensively: g_CurrentPlayer
    resolves to a null word, _read_player returns {}, and the player hash is
    eight zero bytes on every tick. Deterministic, so the gate passes happily
    while verifying nothing whatever about the player. Measured on dam: the
    real pointer sat 0x10 further along, holding health 0.9812, while the
    schema read 0x00000000 for 29,320 consecutive ticks.

    So prefer the .map written beside the ROM by direct-boot. An explicit
    --map always wins; this only replaces the default.
    """
    if str(mapopt) == str(DEFAULT_MAP):
        sibling = Path(rom).with_suffix(".map")
        if sibling.exists():
            return str(sibling)
    return mapopt

# ONE config directory for record and replay alike. The core reads its settings
# from here, and a difference between the two changes emulated timing - which
# changes how often the game polls the controller, which desyncs a stream that
# is indexed by read count. Recording under one config and replaying under
# another is what made a Facility recording select Dam on playback.
DEFAULT_CONFIG = Path.home() / ".config/sightline-harness"


def ensure_pad_profile(cfgdir: Path, rom: Path) -> None:
    """Make sure the harness config carries the gamepad mapping."""
    import subprocess
    cfg = cfgdir / "mupen64plus.cfg"
    if cfg.is_file():
        return
    gen = Path(__file__).resolve().parent / "recording" / "fps_config.py"
    subprocess.run([sys.executable, str(gen), str(cfgdir), "--rom", str(rom),
                    "--gamepad"], check=True)


def _env_name() -> str:
    """Name the execution environment; SL_ENV overrides the guess."""
    env = os.environ.get("SL_ENV")
    if env:
        return env
    return "container" if Path("/.dockerenv").exists() else "host"


def libretro_fingerprint() -> str:
    """Everything that must match for two libretro traces to be comparable.

    The core is identified by CONTENT, not filename. This core is downloaded
    rather than packaged, so `parallel_n64_libretro.so` names a file that can
    differ between machines and across a re-download while the path stays put.
    A silent core swap would surface as a game divergence.

    `env=` is carried over from the mupen64plus fingerprint deliberately, and
    it is NOT yet known to be necessary here. Under mupen64plus the host and
    the CI container disagreed on the same ROM and input (240 VI per 120 ticks
    versus 356) while each was internally deterministic. That was a property of
    VI-keyed sampling, and `retro_run()` - exactly one frame, no VI boundaries -
    plausibly removes it. Plausibly is not measured. Until someone runs the same
    capture in both places and compares, the conservative fingerprint is the
    correct one: refusing a valid comparison costs a re-record, while allowing
    an invalid one reports an environmental artifact as a bug in the game. See
    B-007.
    """
    core = Path(DEFAULT_CORE)
    try:
        digest = hashlib.sha1(core.read_bytes()).hexdigest()[:12]
    except OSError:
        digest = "missing"
    return (f"libretro core={core.name} sha1={digest} "
            f"sampling=retro_run env={_env_name()}")


def emu_fingerprint() -> str:
    """Everything that must match for two traces to be comparable.

    The EXECUTION ENVIRONMENT is part of this, and it is not optional. Measured
    2026-08-18: the same ROM and the same input produce a different state hash on
    the host than in the CI container - 240 VI per 120 ticks versus 356 - even
    with identical mupen64plus and plugin package versions, identical data files
    and software GL forced on both. Each environment is internally deterministic
    and reproduces itself exactly; they simply do not agree with each other.

    Binary hashes of the core and plugins would NOT catch this, because the
    packages are byte-identical in both places. The difference is environmental,
    so the environment is named explicitly and traces recorded in one are refused
    against replays from another.

    Set SL_ENV to name the environment; it is auto-detected otherwise.
    """
    return (f"mupen64plus/pure-interp gfx={GFX_PLUGIN} rsp={RSP_PLUGIN} "
            f"env={_env_name()}")


def _eeprom_sha1(path: str) -> str:
    """Identify the cartridge save a run starts from, or '' when there is none."""
    if not path:
        return ""
    f = Path(path)
    return hashlib.sha1(f.read_bytes()).hexdigest() if f.exists() else ""


def cmd_capture(args) -> int:
    rom = Path(args.rom)
    rom_sha1 = hashlib.sha1(rom.read_bytes()).hexdigest()
    syms = SymbolTable.from_map(_map_for(args.rom, args.map))
    reader = StateReader(syms)

    writer = TraceWriter(args.out, TraceHeader(
        rom_sha1=rom_sha1, schema_version=SCHEMA_VERSION, level=args.level,
        emu_fingerprint=(libretro_fingerprint() if args.backend == "libretro"
                         else emu_fingerprint()), tick_count=0,
        eeprom_sha1=_eeprom_sha1(getattr(args, "eeprom", "")),
        input_stream=args.replay or args.input or "",
        notes=args.notes or ""))

    def frame_counter_of(base):
        return reader.frame_counter(Memory(base))

    def on_tick(idx, fc, base):
        st = reader.capture(Memory(base), idx)
        writer.add(idx, fc, st.composite, st.entity_hashes)

    if args.backend == "libretro":
        rp = args.replay or None
        is_bsv = bool(rp) and Path(rp).suffix == ".replay"
        ticks = args.ticks
        if ticks <= 0:
            # Replay the whole stream and no further. Running past the end
            # feeds neutral input to a game that is still live, so the tail of
            # the trace would record drift the recording never contained.
            ticks = (Path(rp).stat().st_size // 4) if (rp and not is_bsv) else 300
        emu = LibretroEmulator(str(rom), verbose=args.verbose,
                               input_stream=None if is_bsv else rp,
                               replay_file=rp if is_bsv else None,
                               eeprom=(args.eeprom or None))
        stats = emu.run(on_tick, frame_counter_of, max_ticks=ticks)
        emu.close()
        writer.close()
        print(f"captured {stats['ticks']} ticks "
              f"({stats['frames']} frames) -> {args.out}")
        return 0

    emu = Emulator(str(rom), config_dir=str(args.config), verbose=args.verbose,
                   input_replay=args.replay or None,
                   load_state=args.state or None)
    if args.sample == "breakpoint":
        stats = emu.run_on_write(on_tick, syms.addr("currentFrameCounter"),
                                 max_ticks=args.ticks)
    else:
        stats = emu.run(on_tick, frame_counter_of, max_ticks=args.ticks)
    writer.close()
    extra = (f"{stats['vi']} VI" if "vi" in stats else f"{stats.get('hits', 0)} bkp hits")
    print(f"captured {stats['ticks']} ticks ({extra}) -> {args.out}")
    return 0


def cmd_record(args) -> int:
    """Play the game in a window, logging input AND the state trace.

    Recording must use the exact emulator configuration replay uses, or the
    recording is valid and the replay of it still desyncs. Sharing one backend
    is what guarantees that, so this drives the same LibretroEmulator that
    capture and replay drive - the only difference is that a window is attached
    and each frame's input is written out before it is consumed.

    The state trace is captured here too. Without it, "did replay reproduce the
    session?" is a question you can only answer by watching the screen.

    Play, then close the window to stop. `make trace-roundtrip` proves the
    stream this writes replays back identically.
    """
    if args.state or args.save_state:
        print("savestates are not wired into the libretro recorder yet "
              "(they need a warm-up frame before retro_serialize works).\n"
              "Record from boot for now; see docs/backlog.md B-008.",
              file=sys.stderr)
        return 1

    from sltrace.record_libretro import Recorder

    syms = SymbolTable.from_map(_map_for(args.rom, args.map))
    reader = StateReader(syms)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    trace_out = Path(args.trace) if args.trace else out.with_suffix(".sltrace")
    trace_out.parent.mkdir(parents=True, exist_ok=True)

    rom_sha1 = hashlib.sha1(Path(args.rom).read_bytes()).hexdigest()
    writer = TraceWriter(trace_out, TraceHeader(
        rom_sha1=rom_sha1, schema_version=SCHEMA_VERSION, level=args.level,
        emu_fingerprint=libretro_fingerprint(), tick_count=0,
        # The save this session starts from. The snapshot written beside the
        # recording is a copy of these same bytes, so a later replay that
        # loads a DIFFERENT save is caught by name instead of showing up as an
        # unexplained divergence somewhere in the menus.
        eeprom_sha1=_eeprom_sha1(args.eeprom or ""),
        input_stream=str(out), notes="recorded live"))

    def frame_counter_of(base):
        return reader.frame_counter(Memory(base))

    def on_tick(idx, fc, base):
        st = reader.capture(Memory(base), idx)
        writer.add(idx, fc, st.composite, st.entity_hashes)
        if idx % 600 == 0:
            print(f"  tick {idx} (fc={fc})", flush=True)

    if not os.environ.get("DISPLAY") and not os.environ.get("WAYLAND_DISPLAY"):
        print("no DISPLAY - the recorder needs a real one, since the point is "
              "to see the game. Do not run this under xvfb.", file=sys.stderr)
        return 1

    live = None
    if args.rom_debug:
        # The DEBUG ROM's mailbox, driven live. This is what makes the reserved
        # Create bit answer out loud instead of silently.
        from sltrace.romdbg import LiveCapture, PROPDEF_TINTED_GLASS  # noqa: F401
        if "g_SlRomDbg" not in syms:
            print(f"--rom-debug, but g_SlRomDbg is not in "
                  f"{_map_for(args.rom, args.map)}.\n"
                  f"  That map is from a ROM built WITHOUT SIGHTLINE_ROM_DEBUG. "
                  f"Build one with: make trace-debug LEVEL=<level> "
                  f"SL_NAME={args.level}", file=sys.stderr)
            return 1
        _debug_rom_banner(args, out, trace_out, "THIS RECORDING IS ABOUT TO BE MADE")

    print(f"recording to {out}")
    print("  play, then CLOSE THE WINDOW to stop.")
    # Snapshot the save AS LOADED, beside the recording. Replay must start from
    # the exact cartridge state this session started from, and the live save
    # file will have moved on by the time anyone replays this. The Recorder
    # writes it once the core has loaded, which is when it is actually correct.
    rec = Recorder(str(args.rom), str(out), eeprom=(args.eeprom or None),
                   snapshot_path=str(out.with_suffix(".eeprom")))
    pre_tick = None
    if args.rom_debug:
        capture_out = Path(args.capture_out or (out.parent / (out.stem + "-captures")))
        live = LiveCapture(syms.addr("g_SlRomDbg"),
                           out_dir=capture_out,
                           emit=lambda s: print(s, flush=True),
                           aid_type=(None if args.aim_aid < 0 else args.aim_aid),
                           aid_every=args.aid_every,
                           miss_detail=not args.no_miss_detail,
                           framebuffer=rec.framebuffer)

        def pre_tick(index, frame, base):
            live.before_frame(index, frame, Memory(base))

        base_on_tick = on_tick

        def on_tick(idx, fc, base):                       # noqa: F811
            base_on_tick(idx, fc, base)
            live.after_frame(idx, Memory(base))

        print(f"  press CREATE (left of the touchpad) to capture what the "
              f"crosshair is on")
        print(f"  captures -> {capture_out}")
        if args.aim_aid >= 0:
            print(f"  aim aid ON for PROPDEF {args.aim_aid} "
                  f"(one line every {args.aid_every} frames, only when it changes)")

    synth = set()
    for tok in (args.synth_select or "").split(","):
        if tok.strip():
            synth.add(int(tok.strip()))
    if synth:
        print(f"  SYNTHETIC Create presses at frames {sorted(synth)} "
              f"(known-positive control for the capture path)")

    rec.open()
    try:
        stats = rec.run(on_tick=on_tick, frame_counter_of=frame_counter_of,
                        max_frames=args.ticks, pre_tick=pre_tick,
                        synth_select=synth)
    finally:
        rec.close()
        writer.close()

    if not stats["logged"]:
        # An abandoned session leaves a header-only trace behind, and a 0-tick
        # trace looks exactly like a recorded level to anything that lists the
        # directory - so it silently shrinks gate coverage while appearing to
        # add to it. Clear it out rather than leaving it to be tidied by hand.
        for stray in (trace_out, out, out.with_suffix(".eeprom")):
            if stray.exists() and (stray != out or stray.stat().st_size == 0):
                stray.unlink()
        print("no frames recorded - nothing kept.", file=sys.stderr)
        print(f"  re-run: make trace-record LEVEL={args.level}", file=sys.stderr)
        return 1

    reason = "window-closed" if getattr(rec, "quit_requested", False) else "frame-cap"
    print(f"STOP {reason} frames={stats['logged']}")
    print(f"recorded {stats['logged']} frames")
    print(f"  input stream: {out}")
    print(f"  state trace:  {trace_out}")
    if args.rom_debug:
        print(f"  captures:     {live.summary()}")
        if live.presses and not live.captures:
            # The instrument saying it failed is worth more than a clean-looking
            # run: presses that produced no capture at all mean the mailbox was
            # never reached, and every conclusion drawn from the session is void.
            print("  WARNING: presses were recorded but NOTHING was captured - "
                  "the debug hook never served a command.", file=sys.stderr)
        _debug_rom_banner(args, out, trace_out, "WHAT YOU JUST RECORDED")
        return 0
    print(f"\nVerify it replays identically with:")
    print(f"  make trace-verify LEVEL={args.level}")
    return 0


def _debug_rom_banner(args, out, trace_out, headline: str) -> None:
    """Say what a debug-ROM recording is and is NOT, at both ends of the run.

    Printed twice on purpose. The distinction is not a footnote: the debug ROM
    is knowingly non-matching (boss.c:389 seeds the RNG from osGetCount(), so
    ANY change of ROM image changes the run from tick 79 onwards), and a capture
    press perturbs the run measurably from roughly 110 game ticks after it. A
    stream recorded here is therefore an INSPECTION artefact. Discovering that
    afterwards is how a day gets spent comparing two runs that were never the
    same run. See docs/backlog.md, B-051.
    """
    line = "=" * 74
    print(line)
    print(f"  {headline}: A DEBUG-ROM RECORDING, FOR CAPTURE AND INSPECTION")
    print(line)
    print(f"  rom:   {args.rom}")
    print(f"  input: {out}")
    print(f"  trace: {trace_out}")
    print("")
    print("  It IS: a way to ask the cartridge what the crosshair was on, live,")
    print("         and to keep the input stream that got you there.")
    print("  It is NOT interchangeable with a normal recording:")
    print("    * the debug ROM is knowingly NON-MATCHING - a different ROM image")
    print("      gets a different RNG seed at boot (boss.c:389), so its trace")
    print("      diverges from the parity ROM's by tick 79 no matter what;")
    print("    * every capture press perturbs the run, measurably from about 110")
    print("      game ticks later. What you captured is sound for that frame and")
    print("      every frame before it. After it, this is a different run.")
    print("  So do NOT use this stream for pixel-fidelity work, trace comparison")
    print("  or re-baselining. For those, record again against the normal ROM:")
    print(f"    make trace-record LEVEL=<level> SL_NAME={args.level}")
    print(line)


def _boot_and_dump(rom: str, eeprom: str | None, out_blob: str) -> None:
    """Boot far enough for the game to initialise its save, then dump it."""
    emu = LibretroEmulator(rom, eeprom=eeprom)
    emu._load()
    for _ in range(600):
        emu._core.retro_run()
    Path(out_blob).write_bytes(emu.eeprom_snapshot())
    emu.close()


def _verify_save(rom: str, eeprom: str, symmap: str) -> None:
    """Boot with the seeded save and report what the GAME made of it.

    This is the part that matters. If the checksums were wrong the game
    silently resets the slot on load, so reading our own file back would prove
    nothing - the times have to be read out of live RAM after the game has
    validated and accepted them.
    """
    from sltrace.savefile import SAVE_SIZE, TIMES_OFF, TIMES_LEN

    syms = SymbolTable.from_map(symmap)
    base_addr = syms.addr("saves")
    emu = LibretroEmulator(rom, eeprom=eeprom)
    emu._load()
    for _ in range(900):
        emu._core.retro_run()
    mem = Memory(emu.rdram())
    live = 0
    for slot in range(6):
        rec = base_addr + slot * SAVE_SIZE
        # Raw bytes: RDRAM is word-swapped, but "is anything set" does not
        # care about order within a word.
        nonzero = any(mem.block(rec + TIMES_OFF, TIMES_LEN))
        if nonzero:
            live += 1
    emu.close()
    print(f"  slots the game loaded with progress: {live}/6")
    if not live:
        print("  the game rejected the save - checksums are wrong", file=sys.stderr)
        raise SystemExit(1)


def cmd_unlock(args) -> int:
    """Seed the cartridge save so every stage is selectable.

    Recording a level means reaching it, and reaching a late level means
    completing every level before it first. That is not a reasonable
    prerequisite for recording traces, so the save gets seeded instead.
    """
    from sltrace.savefile import find_records, unlock_all

    save = Path(args.eeprom)
    scratch = save.with_suffix(".seed")
    if args.internal_dump:
        _boot_and_dump(args.rom, str(save) if save.exists() else None, str(scratch))
        return 0
    if args.internal_verify:
        _verify_save(args.rom, str(save), args.symmap)
        return 0

    me = [sys.executable, str(Path(__file__).resolve()), "unlock",
          "--eeprom", str(save), "--rom", args.rom, "--map", args.symmap]
    if subprocess.run(me + ["--internal-dump"]).returncode:
        return 1
    blob = scratch.read_bytes()
    scratch.unlink()

    found = find_records(blob)
    if not found:
        print("no valid save slots in the cartridge image - cannot seed it",
              file=sys.stderr)
        return 1
    seeded, touched = unlock_all(blob)
    save.parent.mkdir(parents=True, exist_ok=True)
    save.write_bytes(seeded)
    print(f"seeded {len(touched)} save slots -> {save}")

    return subprocess.run(me + ["--internal-verify"]).returncode


def cmd_rom_for(args) -> int:
    """Name the candidate ROM whose hash matches what the trace was recorded on.

    Levels recorded before direct-boot existed were captured on the matching
    ROM; levels recorded since use a per-level image. Choosing by hash means
    neither has to be special-cased, and a replay can never be handed an image
    the baseline was not recorded with - which would surface as "different ROM"
    at best and as a mysterious divergence at worst.

    Prints nothing and exits 1 when none match, so a caller can fall back.
    """
    want = Trace.load(args.trace).header.rom_sha1
    for cand in args.candidates:
        c = Path(cand)
        if c.exists() and hashlib.sha1(c.read_bytes()).hexdigest() == want:
            print(cand)
            return 0
    return 1


def cmd_detail(args) -> int:
    """Re-run and dump full field values around a tick.

    Traces deliberately store only hashes. Because replay is deterministic the
    exact values are always recoverable, so this is what turns "entity 7 moved"
    into "entity 7's lastknowntargetpos differs".
    """
    import json
    syms = SymbolTable.from_map(_map_for(args.rom, args.map))
    reader = StateReader(syms)
    # Capture by frame counter when asked: tick indices are sample ordinals and
    # can drift between runs, so they are the wrong coordinate for comparing two
    # runs at "the same moment".
    use_fc = args.fc > 0
    centre = args.fc if use_fc else args.tick
    lo = max(0, centre - args.window)
    hi = centre + args.window
    out = {"tick": args.tick, "window": args.window, "schema": SCHEMA_VERSION,
           "ticks": {}}

    def frame_counter_of(base):
        return reader.frame_counter(Memory(base))

    def on_tick(idx, fc, base):
        key = fc if use_fc else idx
        if lo <= key <= hi:
            st = reader.capture(Memory(base), idx, detail=True)
            out["ticks"][str(key)] = {"frame_counter": fc, "tick": idx,
                                      **st.detail}

    emu = Emulator(args.rom, config_dir=str(args.config), verbose=args.verbose,
                   input_replay=args.replay or None, load_state=args.state or None)
    emu.run_on_write(on_tick, syms.addr("currentFrameCounter"),
                     max_ticks=args.max_ticks)
    Path(args.out).write_text(json.dumps(out, indent=1, sort_keys=True))
    print(f"detail for ticks {lo}..{hi} -> {args.out}")
    return 0


def _flatten(d, prefix=""):
    flat = {}
    for k, v in d.items():
        key = f"{prefix}{k}"
        if isinstance(v, dict):
            flat.update(_flatten(v, key + "."))
        elif isinstance(v, list):
            for i, x in enumerate(v):
                flat[f"{key}[{i}]"] = x
        else:
            flat[key] = v
    return flat


def cmd_detaildiff(args) -> int:
    """Field-level diff between two detail dumps - the end of the diagnostic chain."""
    import json
    A = json.loads(Path(args.a).read_text())
    B = json.loads(Path(args.b).read_text())
    RED, GRN, BOLD, RST = "\033[31m", "\033[32m", "\033[1m", "\033[0m"
    if args.no_color:
        RED = GRN = BOLD = RST = ""

    common = sorted(set(A["ticks"]) & set(B["ticks"]), key=int)
    found = False
    for t in common:
        fa, fb = _flatten(A["ticks"][t]), _flatten(B["ticks"][t])
        diffs = [(k, fa.get(k), fb.get(k)) for k in sorted(set(fa) | set(fb))
                 if fa.get(k) != fb.get(k)]
        if not diffs:
            continue
        if not found:
            print(f"{BOLD}field-level divergence{RST}")
            found = True
        print(f"\n  {BOLD}tick {t}{RST} (fc={A['ticks'][t].get('frame_counter')})")
        for k, va, vb in diffs[:args.max_fields]:
            print(f"    {k}")
            print(f"      A {GRN}{va}{RST}")
            print(f"      B {RED}{vb}{RST}")
        if len(diffs) > args.max_fields:
            print(f"    ... {len(diffs)-args.max_fields} more fields")
        if args.first_only:
            break
    if not found:
        print(f"{GRN}no field differences in the compared window{RST}")
        return 0
    return 1


def cmd_diff(args) -> int:
    a, b = Trace.load(args.a), Trace.load(args.b)
    problems = a.comparable_with(b)
    if problems and getattr(args, "across_roms", False):
        # ONE legitimate case for comparing traces from different ROM images:
        # proving that a knowingly non-matching DEBUG build (SIGHTLINE_ROM_DEBUG,
        # see src/game/sl_romdbg.h) simulates the same game as the normal ROM
        # for the same input. There the differing sha1 is the point of the test,
        # not a mistake - an oracle that explains a different game than the one
        # being measured is worse than no oracle. Everything else that makes two
        # traces incomparable still refuses.
        problems = [pb for pb in problems if not pb.startswith("different ROM:")]
        print("\033[33mcomparing ACROSS ROM IMAGES (--across-roms).\033[0m")
        print("  Only valid when the two builds are meant to simulate identically.")
    if problems:
        print("\033[31mtraces are not comparable:\033[0m")
        for p in problems:
            print(f"  - {p}")
        print("\nA divergence between incomparable traces is meaningless.")
        return 2
    divs = find_divergences(a, b)
    print(format_report(a, b, args.a, args.b, divs, color=not args.no_color))
    return 1 if divs or len(a.ticks) != len(b.ticks) else 0


def main() -> int:
    p = argparse.ArgumentParser(description="Sightline trace harness")
    sub = p.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("capture", help="run the ROM and record a state trace")
    c.add_argument("--out", required=True)
    c.add_argument("--level", default="boot")
    c.add_argument("--ticks", type=int, default=0,
                   help="0 = length of --replay stream, else 300")
    c.add_argument("--rom", default=str(DEFAULT_ROM))
    c.add_argument("--map", default=str(DEFAULT_MAP))
    c.add_argument("--input", default="")
    c.add_argument("--notes", default="")
    c.add_argument("--verbose", action="store_true")
    c.add_argument("--config", default=str(DEFAULT_CONFIG))
    c.add_argument("--replay", default="", help="input stream to feed the game")
    c.add_argument("--eeprom", default="", help="cartridge save to start from")
    c.add_argument("--state", default="", help="savestate to start from")
    c.add_argument("--backend", choices=["libretro", "mupen"], default="libretro",
                   help="libretro is deterministic; mupen is kept for comparison")
    c.add_argument("--sample", choices=["breakpoint", "vi"], default="breakpoint",
                   help="where to sample state; breakpoint is frame-deterministic")
    c.set_defaults(func=cmd_capture)

    r = sub.add_parser("record", help="play through the harness, logging input")
    r.add_argument("--out", required=True)
    r.add_argument("--ticks", type=int, default=200000)
    r.add_argument("--rom", default=str(DEFAULT_ROM))
    r.add_argument("--map", default=str(DEFAULT_MAP))
    r.add_argument("--verbose", action="store_true")
    r.add_argument("--level", default="recorded")
    r.add_argument("--trace", default="", help="state trace path (default: alongside --out)")
    r.add_argument("--state", default="", help="savestate to start from (skips menus)")
    r.add_argument("--save-state", dest="save_state", default="",
                   help="write a savestate on exit, to start future runs from")
    r.add_argument("--config", default=str(DEFAULT_CONFIG))
    r.add_argument("--eeprom", default="",
                   help="cartridge save to load and update (progress persists)")
    r.add_argument("--rom-debug", dest="rom_debug", action="store_true",
                   help="the ROM is a SIGHTLINE_ROM_DEBUG build: drive its "
                        "mailbox live, so each press of the reserved Create bit "
                        "prints what the crosshair was on as it happens")
    r.add_argument("--capture-out", dest="capture_out", default="",
                   help="where per-capture reports and framebuffers go "
                        "(default: <input>-captures/)")
    r.add_argument("--aim-aid", dest="aim_aid", type=int, default=-1,
                   help="PROPDEF to keep an eye on while playing, e.g. 47 for "
                        "TINTED_GLASS. Prints the nearest one and its distance, "
                        "and only when that line CHANGES. Default off: it costs "
                        "a mailbox command every --aid-every frames, and every "
                        "command perturbs the run.")
    r.add_argument("--aid-every", dest="aid_every", type=int, default=120,
                   help="frames between aim-aid samples (default 120, ~2s)")
    r.add_argument("--no-miss-detail", dest="no_miss_detail",
                   action="store_true",
                   help="do not follow a MISS with a listing of what WAS on "
                        "screen")
    r.add_argument("--synth-select", dest="synth_select", default="",
                   help="comma-separated frame indices at which to synthesise a "
                        "Create press. The known-positive control for the "
                        "capture path - it needs no pad, so it runs headless.")
    r.set_defaults(func=cmd_record)

    u = sub.add_parser("unlock", help="seed the save so every stage is selectable")
    u.add_argument("--eeprom", default="tools/trace/saves/cartridge.sav")
    u.add_argument("--rom", default=str(DEFAULT_ROM))
    u.add_argument("--map", dest="symmap", default=str(DEFAULT_MAP))
    u.add_argument("--internal-dump", action="store_true", help=argparse.SUPPRESS)
    u.add_argument("--internal-verify", action="store_true", help=argparse.SUPPRESS)
    u.set_defaults(func=cmd_unlock)

    rf = sub.add_parser("rom-for",
                        help="print which candidate ROM a trace was recorded with")
    rf.add_argument("trace")
    rf.add_argument("candidates", nargs="+")
    rf.set_defaults(func=cmd_rom_for)

    d = sub.add_parser("diff", help="compare two traces")
    d.add_argument("a")
    d.add_argument("b")
    d.add_argument("--no-color", action="store_true")
    d.add_argument("--across-roms", dest="across_roms", action="store_true",
                   help="allow a diff between traces captured on different ROM "
                        "images. Only for proving a debug build simulates "
                        "identically to the normal one.")
    d.set_defaults(func=cmd_diff)

    dt = sub.add_parser("detail", help="dump full field values around a tick")
    dt.add_argument("--tick", type=int, default=0)
    dt.add_argument("--fc", type=int, default=0,
                    help="capture at this currentFrameCounter (preferred)")
    dt.add_argument("--max-ticks", dest="max_ticks", type=int, default=200000)
    dt.add_argument("--window", type=int, default=2)
    dt.add_argument("--out", required=True)
    dt.add_argument("--rom", default=str(DEFAULT_ROM))
    dt.add_argument("--map", default=str(DEFAULT_MAP))
    dt.add_argument("--verbose", action="store_true")
    dt.add_argument("--config", default=str(DEFAULT_CONFIG))
    dt.add_argument("--replay", default="", help="input stream to reproduce")
    dt.add_argument("--state", default="", help="savestate to start from")
    d.add_argument("--config", default=str(DEFAULT_CONFIG))
    dt.set_defaults(func=cmd_detail)

    dd = sub.add_parser("detail-diff", help="field-level diff of two detail dumps")
    dd.add_argument("a")
    dd.add_argument("b")
    dd.add_argument("--max-fields", type=int, default=25)
    dd.add_argument("--first-only", action="store_true")
    dd.add_argument("--no-color", action="store_true")
    dd.set_defaults(func=cmd_detaildiff)

    args = p.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
