#!/usr/bin/env python3
"""Regenerate baseline traces by replaying the recordings that produced them.

Needed whenever the state schema changes: the recordings stay valid, since
input streams and cartridge snapshots do not depend on what gets hashed, so no
level ever has to be replayed by hand.

The ROM is chosen by matching the hash the trace was recorded with, never by
preferring one image. Doing it the other way silently rebuilt Facility - the
one level recorded through the menus - against a direct-boot ROM that starts
inside the level, producing a trace that was perfectly deterministic and
completely meaningless.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
TRACES = ROOT / "tools/trace/traces"
INPUTS = ROOT / "tools/trace/inputs"
TOOL = ROOT / "tools/trace/trace.py"


def rom_for(level: str) -> str | None:
    trace = TRACES / f"{level}.sltrace"
    cands = [str(ROOT / f"build/u/direct/ge007.u.{level}.z64"),
             str(ROOT / "build/u/ge007.u.z64")]
    r = subprocess.run([sys.executable, str(TOOL), "rom-for", str(trace), *cands],
                       capture_output=True, text=True)
    return r.stdout.strip() or None


def rebaseline(level: str) -> tuple[str, bool, str]:
    inp = INPUTS / f"{level}.input"
    if not inp.exists() or not inp.stat().st_size:
        return level, False, "no input stream"
    rom = rom_for(level)
    if not rom:
        return level, False, "no ROM matches the hash this trace was recorded with"
    cmd = ["xvfb-run", "-a", "--server-args=-screen 0 640x480x24",
           sys.executable, str(TOOL), "capture",
           "--out", str(TRACES / f"{level}.sltrace"), "--level", level,
           "--replay", str(inp), "--eeprom", str(INPUTS / f"{level}.eeprom"),
           "--rom", rom]
    r = subprocess.run(cmd, capture_output=True)
    if r.returncode:
        return level, False, "replay failed"
    return level, True, Path(rom).name


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--jobs", "-j", type=int, default=1,
                    help="levels to replay at once (each uses ~5 cores)")
    ap.add_argument("--level", default="", help="one level instead of all")
    a = ap.parse_args()

    levels = ([a.level] if a.level
              else sorted(p.stem for p in TRACES.glob("*.sltrace")))
    failures = []
    with ThreadPoolExecutor(max_workers=max(1, a.jobs)) as ex:
        futs = [ex.submit(rebaseline, l) for l in levels]
        for fut in as_completed(futs):
            lvl, ok, why = fut.result()
            print(f"  {lvl}: {'rebaselined on ' + why if ok else 'FAILED - ' + why}",
                  flush=True)
            if not ok:
                failures.append(lvl)
    print(f"\n{len(levels) - len(failures)}/{len(levels)} rebaselined")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
