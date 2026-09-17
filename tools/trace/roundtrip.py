#!/usr/bin/env python3
"""Prove that what the recorder writes is what replay plays back.

This is the link everything else in Phase 0 rests on. Both halves being
individually deterministic is not enough: if the logged stream is applied one
frame early, or the recorder's warm-up differs from replay's, every trace we
record is a recording of the wrong run and the harness certifies nothing.

Records with a scripted pad so it needs no human, then replays the resulting
stream, and compares the per-tick composite hash and the frame-counter
progression. Frame counters are reported separately because the two failures
need different fixes: counters diverging means the loops are misaligned, while
counters agreeing and state diverging means the runs genuinely behaved
differently.

Runs each half as a SUBPROCESS, and that is load-bearing, not tidiness. dlopen
hands back the same core for an already-loaded path, so an in-process record
then replay makes the second run resume the first one's core instead of
booting. LibretroEmulator raises SecondCoreError rather than let that happen
silently, but the split has to be here for the test to run at all.
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
from pathlib import Path

TRACE_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TRACE_DIR))

DEFAULT_ROM = TRACE_DIR.parent.parent / "build/u/ge007.u.z64"
DEFAULT_MAP = TRACE_DIR.parent.parent / "build/u/ge007.u.map"

# Pulsed START. Neutral input would leave the game on one screen, where a
# misapplied input stream is indistinguishable from a correct one; pulsing
# drives it through menu transitions whose timing exposes an off-by-one frame.
PULSE_PERIOD = 25


def _scripted_input(counter: list[int]):
    from sltrace.emu_libretro import InputFrame
    counter[0] += 1
    on = (counter[0] // PULSE_PERIOD) % 2 == 0
    return InputFrame((1 << InputFrame.START) if on else 0)


def _digest(rom: str, symmap: str, stream: str, frames: int, record: bool):
    """Run one half and print 'OK <sha1> <fc-samples>' for the parent to read."""
    from sltrace.state import StateReader, Memory
    from sltrace.symbols import SymbolTable

    reader = StateReader(SymbolTable.from_map(symmap))
    h = hashlib.sha1()
    fcs: list[tuple[int, int]] = []

    def on_tick(i, fc, base):
        h.update(reader.capture(Memory(base), i).composite)
        if i % 100 == 0:
            fcs.append((i, fc))

    fc_of = lambda base: reader.frame_counter(Memory(base))

    if record:
        from sltrace.record_libretro import Recorder
        rec = Recorder(rom, stream)
        rec.open()
        counter = [0]
        rec._read_pad = lambda: _scripted_input(counter)
        rec.run(on_tick=on_tick, frame_counter_of=fc_of, max_frames=frames)
        rec.close()
    else:
        from sltrace.emu_libretro import LibretroEmulator
        emu = LibretroEmulator(rom, input_stream=stream)
        emu.run(on_tick, fc_of, max_ticks=frames)
        emu.close()

    print(f"OK {h.hexdigest()} {fcs}")


def _half(rom: str, symmap: str, stream: str, frames: int, record: bool):
    """Spawn one half and return (digest, frame-counter samples)."""
    cmd = [sys.executable, str(Path(__file__).resolve()), "--internal-half",
           "--rom", rom, "--map", symmap, "--stream", stream,
           "--frames", str(frames), "--mode", "record" if record else "replay"]
    if record:
        # The recorder always opens a window; give it a virtual display so this
        # is runnable from CI and over SSH.
        cmd = ["xvfb-run", "-a", "--server-args=-screen 0 1400x1000x24"] + cmd
    proc = subprocess.run(cmd, capture_output=True, text=True)
    for line in proc.stdout.splitlines():
        if line.startswith("OK "):
            _, digest, rest = line.split(" ", 2)
            return digest, rest
    label = "record" if record else "replay"
    sys.stderr.write(f"{label} half produced no result (exit {proc.returncode})\n")
    sys.stderr.write(proc.stderr[-2000:] + "\n")
    raise SystemExit(1)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rom", default=str(DEFAULT_ROM))
    ap.add_argument("--map", dest="symmap", default=str(DEFAULT_MAP))
    ap.add_argument("--frames", type=int, default=600)
    ap.add_argument("--stream", default="/tmp/sl-roundtrip.input")
    ap.add_argument("--internal-half", action="store_true",
                    help=argparse.SUPPRESS)
    ap.add_argument("--mode", choices=("record", "replay"),
                    help=argparse.SUPPRESS)
    a = ap.parse_args()

    if a.internal_half:
        _digest(a.rom, a.symmap, a.stream, a.frames, a.mode == "record")
        return 0

    if not Path(a.rom).exists():
        sys.stderr.write(f"no ROM at {a.rom} - run 'make matching' first\n")
        return 1

    rec_digest, rec_fcs = _half(a.rom, a.symmap, a.stream, a.frames, True)
    rep_digest, rep_fcs = _half(a.rom, a.symmap, a.stream, a.frames, False)

    print(f"  record {rec_digest}")
    print(f"  replay {rep_digest}")
    if rec_digest == rep_digest:
        print(f"ROUND TRIP OK - {a.frames} frames identical")
        return 0

    print("ROUND TRIP DIVERGED")
    if rec_fcs != rep_fcs:
        print("  frame counters differ - the two loops are MISALIGNED:")
        print(f"    record {rec_fcs}")
        print(f"    replay {rep_fcs}")
    else:
        print("  frame counters agree, so alignment is fine and the runs")
        print("  genuinely behaved differently. Compare fields at the first")
        print("  differing tick with 'make trace-diff'.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
