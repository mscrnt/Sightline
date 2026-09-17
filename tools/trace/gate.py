#!/usr/bin/env python3
"""Phase 0 gate: every campaign level replays byte-identically, N runs running.

Why N and not one: both real failures this harness has caught were invisible
to a single comparison. mupen64plus reproduced roughly one replay in four, and
the save write-back passed run 1 and only then corrupted the input for run 2.
One clean pass per level is not evidence.

Results are keyed to the CONTENT of a recording - input stream, cartridge
snapshot and baseline trace. A level that has passed is skipped until one of
those changes, so adding levels costs only the new levels, and re-recording
one invalidates exactly that one. Re-running a finished set is free.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
TRACES = ROOT / "tools/trace/traces"
INPUTS = ROOT / "tools/trace/inputs"
LEDGER = ROOT / "tools/trace/gate-results.json"
TOOL = ROOT / "tools/trace/trace.py"
PY_ = sys.executable


#: Observation recordings - sweeps for AI reference - are named "<level>-sweep-
#: <difficulty>". They are NOT part of the Phase 0 criterion, which is about
#: parity on the campaign as played. They still have to replay deterministically
#: to be usable as references, but proving that does not need the full ten runs,
#: and at 77 of them it would add ~10 hours to every gate.
def is_observation(level: str) -> bool:
    return "-sweep-" in level


def _sha1(p: Path) -> str:
    return hashlib.sha1(p.read_bytes()).hexdigest() if p.exists() else ""


def fingerprint(level: str) -> dict:
    """What a gate result is valid for."""
    return {
        "input": _sha1(INPUTS / f"{level}.input"),
        "eeprom": _sha1(INPUTS / f"{level}.eeprom"),
        "trace": _sha1(TRACES / f"{level}.sltrace"),
    }


def load_ledger() -> dict:
    if LEDGER.exists():
        try:
            return json.loads(LEDGER.read_text())
        except json.JSONDecodeError:
            print("ledger unreadable, starting a new one", file=sys.stderr)
    return {}


def save_ledger(data: dict) -> None:
    LEDGER.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")


def rom_for(level: str) -> str | None:
    trace = TRACES / f"{level}.sltrace"
    cands = [str(ROOT / f"build/u/direct/ge007.u.{level}.z64"),
             str(ROOT / "build/u/ge007.u.z64")]
    r = subprocess.run([PY_, str(TOOL), "rom-for", str(trace), *cands],
                       capture_output=True, text=True)
    return r.stdout.strip() or None


def run_once(level: str, rom: str, out: Path) -> bool:
    cmd = ["xvfb-run", "-a", "--server-args=-screen 0 640x480x24",
           PY_, str(TOOL), "capture", "--out", str(out), "--level", level,
           "--replay", str(INPUTS / f"{level}.input"),
           "--eeprom", str(INPUTS / f"{level}.eeprom"), "--rom", rom]
    return subprocess.run(cmd, capture_output=True).returncode == 0


def compare(level: str, candidate: Path) -> tuple[bool, str]:
    r = subprocess.run([PY_, str(TOOL), "diff", str(TRACES / f"{level}.sltrace"),
                        str(candidate), "--no-color"],
                       capture_output=True, text=True)
    text = r.stdout
    if "IDENTICAL" in text:
        return True, ""
    for line in text.splitlines():
        if "DIVERGED at" in line or "different" in line:
            return False, line.strip()
    return False, "diff produced no verdict"


def _one_run(level: str, rom: str, i: int) -> tuple[int, bool, str]:
    with tempfile.NamedTemporaryFile(suffix=".sltrace", delete=False) as fh:
        tmp = Path(fh.name)
    try:
        if not run_once(level, rom, tmp):
            return i, False, "capture failed"
        ok, why = compare(level, tmp)
        return i, ok, why
    finally:
        tmp.unlink(missing_ok=True)


def gate_level(level: str, runs: int, jobs: int = 1) -> tuple[bool, str]:
    """Replay a level `runs` times, up to `jobs` of them at once.

    The runs are independent processes replaying the same input, so running
    them concurrently changes wall-clock and nothing else - the frame is the
    unit of simulation, and each process dlopens its own core. Contention
    cannot alter a result, only how long it takes to get one.

    Each replay already uses about five cores of its own (24 threads, measured
    at 475% CPU), so useful concurrency is roughly cores/5, not cores.
    """
    rom = rom_for(level)
    if not rom:
        return False, "no ROM matches the hash this trace was recorded with"
    if not (INPUTS / f"{level}.input").exists():
        return False, "no input stream"

    if jobs <= 1:
        for i in range(1, runs + 1):
            _, ok, why = _one_run(level, rom, i)
            if not ok:
                return False, f"run {i}: {why}"
            print(f"    run {i}/{runs} identical", flush=True)
        return True, ""

    with ThreadPoolExecutor(max_workers=jobs) as ex:
        futures = [ex.submit(_one_run, level, rom, i) for i in range(1, runs + 1)]
        done = 0
        for fut in as_completed(futures):
            i, ok, why = fut.result()
            if not ok:
                for f in futures:
                    f.cancel()
                return False, f"run {i}: {why}"
            done += 1
            print(f"    {done}/{runs} identical", flush=True)
    return True, ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--runs", type=int, default=10)
    ap.add_argument("--sweep-runs", dest="sweep_runs", type=int, default=3,
                    help="runs for observation sweeps (default 3)")
    ap.add_argument("--jobs", "-j", type=int, default=1,
                    help="replays to run at once (each uses ~5 cores)")
    ap.add_argument("--level", default="", help="gate one level instead of all")
    ap.add_argument("--force", action="store_true",
                    help="re-gate levels already recorded as passing")
    ap.add_argument("--status", action="store_true", help="report and exit")
    ap.add_argument("--strict", action="store_true",
                    help="with --status, exit non-zero if any level is ungated")
    a = ap.parse_args()

    ledger = load_ledger()
    # Shortest first. A time-boxed run finishes more levels that way, and a
    # systematic failure shows up in minutes instead of after the longest
    # recording in the set.
    def _cost(level: str) -> int:
        f = INPUTS / f"{level}.input"
        return f.stat().st_size if f.exists() else 0

    levels = ([a.level] if a.level
              else sorted((p.stem for p in TRACES.glob("*.sltrace")), key=_cost))

    if a.status:
        print(f"{'level':12} {'runs':>4}  state")
        for lvl in levels:
            rec = ledger.get(lvl)
            state = "not gated"
            if rec:
                state = ("passed" if rec.get("fingerprint") == fingerprint(lvl)
                         else "STALE - recording changed since")
            print(f"{lvl:12} {rec.get('runs', '-') if rec else '-':>4}  {state}")
        ungated = [l for l in levels
                   if ledger.get(l, {}).get("fingerprint") != fingerprint(l)]
        parity = [l for l in levels if not is_observation(l)]
        obs = [l for l in levels if is_observation(l)]
        par_ok = len([l for l in parity if l not in ungated])
        obs_ok = len([l for l in obs if l not in ungated])
        print(f"\nPhase 0 parity: {par_ok}/{len(parity)} campaign levels passing")
        if obs:
            print(f"observation sweeps: {obs_ok}/{len(obs)} passing "
                  f"(reference data, not part of the criterion)")
        if ungated and a.strict:
            print("\nungated: " + ", ".join(ungated), file=sys.stderr)
            print("run 'make trace-gate' locally - it only gates what changed.",
                  file=sys.stderr)
            return 1
        return 0

    failures = []
    for lvl in levels:
        rec = ledger.get(lvl)
        fp = fingerprint(lvl)
        want = a.sweep_runs if is_observation(lvl) else a.runs
        if not a.force and rec and rec.get("fingerprint") == fp \
                and rec.get("runs", 0) >= want:
            print(f"{lvl}: already passed {rec['runs']} runs, unchanged - skipping")
            continue
        print(f"{lvl}: gating {want} runs", flush=True)
        ok, why = gate_level(lvl, want, a.jobs)
        if ok:
            ledger[lvl] = {"runs": want, "fingerprint": fp}
            print(f"{lvl}: PASSED {want}/{want}", flush=True)
        else:
            ledger.pop(lvl, None)
            failures.append((lvl, why))
            print(f"{lvl}: FAILED - {why}", flush=True)
        save_ledger(ledger)     # after every level, so a kill loses nothing

    print()
    if failures:
        print(f"GATE FAILED for {len(failures)} level(s):")
        for lvl, why in failures:
            print(f"  {lvl}: {why}")
        return 1
    par = [l for l in levels if not is_observation(l)]
    obs = [l for l in levels if is_observation(l)]
    msg = f"GATE PASSED - {len(par)} campaign level(s) at {a.runs} runs"
    if obs:
        msg += f", {len(obs)} sweep(s) at {a.sweep_runs}"
    print(msg)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
