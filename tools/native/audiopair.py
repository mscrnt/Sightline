#!/usr/bin/env python3
"""Capture the SAME input stream through the cartridge and the native build.

This is the yardstick B-022's mixer work will be judged against. Audio
correctness is not settleable by ear, and "it sounds right" is exactly the kind
of claim this project does not accept, so both sides are reduced to numbers by
one shared module (audiocap.py) and printed side by side.

    tools/native/audiopair.py --input facility --frames 900 --out /tmp/pair

What it does NOT do is decide whether the two agree. There is no pass/fail
threshold here, deliberately: nothing has ever been measured against this yet,
so any threshold would be invented rather than derived. It prints the deltas
and leaves the judgement to a human until there is a baseline worth asserting.

POSITIVE CONTROL. The cartridge side is this harness's own known-positive. A
tool that can report "the native build produced no audio" has to first prove it
was capable of seeing audio at all - otherwise a broken harness and a silent
mixer are the same output. If the ROM side comes back silent, this exits
nonzero and refuses to report the comparison.

Captures are ROM-derived: they go under /tmp and are never committed.
"""
from __future__ import annotations

import argparse
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/native"))

import audiocap  # noqa: E402

#: What audi.c asks the DAC for (OUTPUT_RATE, src/audi.c:19). The native build
#: has no measurable rate of its own - it submits buffers to a shim - so this
#: is the rate its PCM is interpreted at.
NATIVE_RATE = 22050


def reader_self_test(endian: str) -> str | None:
    """Prove the native-dump READER works before it is allowed to say "none".

    The cartridge side is a positive control for the capture, but it does not
    exercise this path: the ROM capture arrives through wave, the native one
    through raw bytes and Capture(endian=...). A bug in the raw path would
    report an empty native dump no matter what the game did, and that reads
    identically to a silent mixer.

    So: synthesise PCM with known peak, known frame count and known channel
    asymmetry, push it through the same code, and check the numbers come back.
    Returns None on success, or a message describing what disagreed.

    What it does NOT catch, stated so nobody reads more into a pass than is
    there: it packs with the same byte order it reads, so an endianness
    MISCONFIGURATION is invisible to it by construction. The diagnostic for
    that is `rms_byteswapped` in the metrics, which a human reads. Verified
    non-vacuous against two deliberate breaks - forcing `silent` true, and
    swapping the channel de-interleave - and it caught both.
    """
    import struct

    nframes = 512
    peak = 12345
    vals = []
    for i in range(nframes):
        # Left ramps, right is half of it - so a channel mix-up or a dropped
        # channel is caught, not just a total-silence bug.
        left = peak if i == nframes // 2 else (i % 97)
        vals.append(left)
        vals.append(left // 2)
    order = "<" if endian == "little" else ">"
    pcm = struct.pack(f"{order}{len(vals)}h", *vals)

    m = audiocap.Capture(pcm, rate=NATIVE_RATE, endian=endian,
                         source="self-test").metrics()
    if m["stereo_frames"] != nframes:
        return f"frame count {m['stereo_frames']} != {nframes}"
    if m["peak"] != peak:
        return f"peak {m['peak']} != {peak}"
    if m["silent"]:
        return "reported silent on a known-nonzero signal"
    if m["left"]["rms"] <= m["right"]["rms"]:
        return (f"channel split wrong: L rms {m['left']['rms']} "
                f"should exceed R rms {m['right']['rms']}")
    return None


def level_of(stream: str) -> tuple[str | None, str | None]:
    """LEVEL/DIFFICULTY from the stream's .spec, so the native build boots the
    same level the cartridge run did. Guessing from the name would silently
    compare two different levels."""
    spec = ROOT / "tools/trace/inputs" / f"{stream}.spec"
    if not spec.is_file():
        return None, None
    text = spec.read_text()
    lvl = re.search(r"LEVEL=(\S+)", text)
    dif = re.search(r"DIFFICULTY=(\S+)", text)
    return (lvl.group(1) if lvl else None, dif.group(1) if dif else None)


def stage_number(level: str) -> str | None:
    """Stage number from the trace harness's own table - the same source
    play.sh reads, so the pairing and the determinism corpus cannot drift."""
    src = (ROOT / "tools/trace/sltrace/levelboot.py").read_text()
    m = re.search(r'"%s"\s*:\s*(\d+)' % re.escape(level), src)
    return m.group(1) if m else None


def difficulty_number(name: str | None) -> str:
    return {"00": "0", "agent": "1", "secret": "2",
            "007": "3", "007agent": "3", "007max": "3"}.get(name or "", "1")


def run_rom(args, out: pathlib.Path) -> pathlib.Path | None:
    """The cartridge side, in its own process: a libretro core may only be
    loaded once per process, and dlopen would hand back the first one."""
    prefix = out / "rom"
    cmd = [sys.executable, str(ROOT / "tools/native/romshot.py"),
           "--input", args.input, "--frames", str(args.frames),
           "--no-video", "--audio-out", str(prefix)]
    if args.audio_rate:
        cmd += ["--audio-rate", str(args.audio_rate)]
    print("$ " + " ".join(cmd))
    env = dict(os.environ)
    env.setdefault("SDL_AUDIODRIVER", "dummy")
    r = subprocess.run(["xvfb-run", "-a"] + cmd, env=env,
                       capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
        return None
    return prefix.with_suffix(".wav")


def run_native(args, out: pathlib.Path) -> pathlib.Path:
    """The native side. Headless: no window, and SL_AUDIO stays unset so no
    device is opened - sl_audio.c opens the DUMP independently of the device
    precisely so a box with no DAC still measures what the game produced."""
    dump = out / "native.pcm"
    binary = ROOT / "build/native/skeleton"
    if not binary.is_file():
        raise SystemExit(f"audiopair: no native build at {binary} "
                         f"(run tools/native/build.sh)")
    level, diff = level_of(args.input)
    env = dict(os.environ)
    env.update({
        "SL_ROM": str(ROOT / "baserom.u.z64"),
        "SL_INPUT": str(ROOT / "tools/trace/inputs" / f"{args.input}.input"),
        "SL_FRAMES": str(args.frames),
        "SL_AUDIO_DUMP": str(dump),
        "SL_AUDIO_STATS": "1",
    })
    if level:
        stage = stage_number(level)
        if stage:
            env["SL_BOOT_LEVEL"] = stage
        env["SL_BOOT_DIFFICULTY"] = difficulty_number(diff)
    eep = ROOT / "tools/trace/inputs" / f"{args.input}.eeprom"
    if eep.is_file():
        env["SL_EEPROM"] = str(eep)

    print(f"$ SL_INPUT=... SL_AUDIO_DUMP={dump} {binary}")
    r = subprocess.run([str(binary)], env=env, cwd=str(ROOT),
                       capture_output=True, text=True)
    # The native side needs its own proof-of-execution, for the same reason the
    # cartridge side is a positive control: a build that died during boot
    # produces an empty dump, and that is indistinguishable from a mixer that
    # ran fine and emitted nothing. "survived N pumped frames" is the game's
    # own statement that it got to the end, and the audio line distinguishes
    # "never reached the audio path" from "submitted buffers of zeros".
    ran = None
    for line in r.stderr.splitlines():
        if "survived" in line and "pumped frames" in line:
            ran = line.strip()
        if "sightline audio" in line or "survived" in line:
            print("  " + line.strip())
    if r.returncode != 0:
        print(f"  native exited {r.returncode}")
    if ran is None:
        print("  WARNING: the native build never reported completing its "
              "frames. An empty dump below may mean it died during boot "
              "rather than that the mixer is silent.", file=sys.stderr)
    return dump


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True,
                    help="stream name under tools/trace/inputs (no extension)")
    ap.add_argument("--frames", type=int, default=900)
    ap.add_argument("--out", default="/tmp/sl-audiopair")
    ap.add_argument("--audio-rate", type=int, default=None,
                    help="force the ROM-side wav rate (default: measured)")
    ap.add_argument("--native-rate", type=int, default=NATIVE_RATE)
    ap.add_argument("--native-endian", choices=("little", "big"),
                    default="little",
                    help="byte order of the native dump. SL_AUDIO_DUMP writes "
                         "the bytes the game SUBMITTED, before sl_audio.c's "
                         "optional swap, so a mixer that keeps N64 DRAM order "
                         "produces big-endian here. Never guessed: the "
                         "rms-if-byteswapped figure makes a mistake visible.")
    ap.add_argument("--skip-rom", action="store_true")
    ap.add_argument("--skip-native", action="store_true")
    args = ap.parse_args()

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    rom_m = nat_m = None

    if not args.skip_rom:
        wav = run_rom(args, out)
        if wav is None or not wav.is_file():
            print("audiopair: ORACLE FAILED - no cartridge capture. "
                  "Nothing below would be attributable.", file=sys.stderr)
            return 3
        import wave
        with wave.open(str(wav), "rb") as w:
            rate = w.getframerate()
            pcm = w.readframes(w.getnframes())
        rom_cap = audiocap.Capture(pcm, rate=rate, endian="little", source="rom")
        rom_m = rom_cap.metrics()

    if not args.skip_native:
        why = reader_self_test(args.native_endian)
        if why is not None:
            print(f"audiopair: READER SELF-TEST FAILED ({why}). The native "
                  f"capture path cannot be trusted to distinguish 'no audio' "
                  f"from 'cannot read audio'. Refusing to report.",
                  file=sys.stderr)
            return 4
        print(f"  reader self-test OK ({args.native_endian}-endian path)")
        dump = run_native(args, out)
        pcm = dump.read_bytes() if dump.is_file() else b""
        nat_cap = audiocap.Capture(
            pcm, rate=args.native_rate, endian=args.native_endian,
            source="native",
            meta={"input_stream": args.input, "frames": args.frames,
                  "git_rev": audiocap.git_rev(ROOT), "argv": sys.argv})
        nat_cap.write_json(out / "native.json")
        if pcm:
            nat_cap.write_wav(out / "native.wav")
        nat_m = nat_cap.metrics()

    print()
    print("=" * 66)
    if rom_m:
        print(audiocap.format_metrics("rom", rom_m))
    if rom_m and nat_m:
        print("-" * 66)
    if nat_m:
        print(audiocap.format_metrics("native", nat_m))
    print("=" * 66)

    if rom_m and rom_m["silent"]:
        print("\naudiopair: ORACLE FAILED - the cartridge produced no audio. "
              "The harness, not the mixer, is what this measures. "
              "Refusing to report a comparison.", file=sys.stderr)
        return 3

    if rom_m and nat_m:
        print("\ndelta (native - rom):")
        print(f"  stereo frames  {nat_m['stereo_frames'] - rom_m['stereo_frames']:+d}")
        print(f"  peak           {nat_m['peak'] - rom_m['peak']:+d}")
        print(f"  rms            {nat_m['rms'] - rom_m['rms']:+.2f}")
        print(f"  nonzero frac   {nat_m['nonzero_frac'] - rom_m['nonzero_frac']:+.6f}")
        if nat_m["silent"]:
            print("\n  native produced NO SOUNDING SAMPLES. The oracle above is "
                  "nonzero, so this is a statement about the native build, not "
                  "about the harness - which is the distinction the positive "
                  "control exists to make.")
        print(f"\n  captures: {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
