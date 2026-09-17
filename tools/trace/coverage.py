#!/usr/bin/env python3
"""Report what AI behaviour the observation sweeps actually captured.

Traces store hashes, not values, so this replays each recording and samples the
state directly. That is the only way to see what is in them - and worth doing,
because a sweep can gate perfectly while containing almost no guard activity.

Slots whose actiontype is out of range are DISCARDED. Roughly a tenth of
occupied pool entries hold uninitialised memory; it is deterministic, so the
gate is unaffected, but counting it here would quietly inflate every figure.
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
TRACES = ROOT / "tools/trace/traces"
INPUTS = ROOT / "tools/trace/inputs"


def act_names() -> list[str]:
    txt = (ROOT / "src/bondconstants.h").read_text(errors="replace").splitlines()
    start = next(i for i, l in enumerate(txt) if re.search(r"\bACT_STAND\b", l))
    names = []
    for line in txt[start - 3:start + 60]:
        m = re.match(r"\s+(ACT_[A-Z0-9_]+)", line)
        if m:
            names.append(m.group(1))
        elif names and "}" in line:
            break
    return names


ACTS = act_names()
ACT_MAX = len(ACTS)


def body_names() -> dict:
    """BODY_ ids to names - what a character IS, as opposed to what it is doing.

    Guards, scientists, civilians and named characters are distinguished only by
    appearance fields; nothing else in ChrRecord says which is which.
    """
    txt = (ROOT / "src/bondconstants.h").read_text(errors="replace").splitlines()
    start = next(i for i, l in enumerate(txt)
                 if re.search(r"\bBODY_Jungle_Commando\b", l))
    out, cur = {}, 0
    # The enum is read as text, so preprocessor blocks have to be honoured:
    # BODY_Connery/Dalton/Moore_Tuxedo sit inside #ifdef ALL_BONDS, which is
    # never defined.  Counting them shifted every id after BODY_Brosnan_Tuxedo
    # up by three and mislabelled the whole roster - depot's guards reported as
    # "Mayday", and every named character looked unobserved.
    skip = False
    for line in txt[start:start + 140]:
        st = line.strip()
        if st.startswith("#ifdef ALL_BONDS"):
            skip = True
            continue
        if skip:
            if st.startswith("#endif"):
                skip = False
            continue
        m = re.match(r"\s*(BODY_[A-Za-z0-9_]+)\s*(?:=\s*(0x[0-9A-Fa-f]+|\d+))?\s*,?", line)
        if not m:
            if "}" in line and out:
                break
            continue
        if m.group(2) is not None:
            cur = int(m.group(2), 0)
        out[cur] = m.group(1)[5:]
        cur += 1
    return out


BODIES = body_names()


def sample(name: str, stride: int = 4) -> dict:
    """Replay one sweep in its own process and return its statistics.

    Samples every `stride` ticks. Building a detail dict for every entity on
    every tick is several times slower than the gate's hash-only path, and the
    statistics are indistinguishable: a character alive for even a second is
    still caught fifteen times at stride 4.
    """
    ticks = (INPUTS / f"{name}.input").stat().st_size // 4
    code = f'''
import sys, collections, json
sys.path.insert(0, {str(ROOT / "tools/trace")!r})
from sltrace.emu_libretro import LibretroEmulator
from sltrace.symbols import SymbolTable
from sltrace.state import StateReader, Memory
syms = SymbolTable.from_map({str(ROOT / "build/u/ge007.u.map")!r})
r = StateReader(syms)
e = LibretroEmulator({str(ROOT / f"build/u/direct/ge007.u.{name}.z64")!r},
    input_stream={str(INPUTS / f"{name}.input")!r},
    eeprom={str(INPUTS / f"{name}.eeprom")!r})
ACT_MAX = {ACT_MAX}
STRIDE = {stride}

import math
def plausible(d):
    """Is this slot a character, or memory that merely survives the pool test?

    ACT_INIT is enum 0, so a zeroed or garbage slot reads as a character
    standing in its initial state. Filtering only on actiontype range lets
    those through, and they dominate: Dam reported 173 characters, 98%
    permanently in INIT and never moving, yet 97% claiming to have seen the
    player. Sampled directly they hold maxdamage 1.16e35, damage -1.2e33,
    positions containing nan, and lastseetarget60 of -1507224631 - against a
    real guard's maxdamage 4.0 and a sane position.

    Reporting only. The hash deliberately still covers every occupied slot:
    that memory is deterministic, which is why the gate passes on it, and
    narrowing what is hashed would stale every trace for no correctness gain.
    """
    if not (0 <= d["actiontype"] < ACT_MAX):
        return False
    for v in list(d["pos"]) + [d["damage"], d["maxdamage"]]:
        if not math.isfinite(v) or abs(v) > 1e6:
            return False
    if not (0.0 <= d["maxdamage"] <= 1000.0):
        return False
    if not (-1 <= d["lastseetarget60"] < 10**7):
        return False
    if not (-1 <= d["lastheartarget60"] < 10**7):
        return False
    return True
ids=set(); tot=0; valid=0; saw=0; heard=0; fired=0; shot=0
acts=collections.Counter(); bodies=collections.Counter()
heads=collections.Counter(); ticks=0
def tick(i, fc, base):
    global tot, valid, saw, heard, fired, shot, ticks
    ticks += 1
    if i % STRIDE:
        return
    st = r.capture(Memory(base), i, detail=True)
    for k, d in (st.detail or {{}}).get("entities", {{}}).items():
        tot += 1
        if not plausible(d):
            continue
        valid += 1; ids.add(k); acts[d["actiontype"]] += 1
        bodies[d["bodynum"]] += 1
        heads[d["headnum"]] += 1
        if d["lastseetarget60"] > 0: saw += 1
        if d["lastheartarget60"] > 0: heard += 1
        if d["firecount0"] or d["firecount1"]: fired += 1
        if 0 <= d["lastshooter"] < 1000: shot += 1
# Stop at the end of the recording. The emulator does NOT stop when the
# input runs out - it keeps going with neutral input - so an open-ended
# max_ticks replays forever.
e.run(tick, lambda b: r.frame_counter(Memory(b)), max_ticks={ticks})
e.close()
print("JSON" + json.dumps({{"ticks":ticks,"tot":tot,"valid":valid,"distinct":len(ids),
    "saw":saw,"heard":heard,"fired":fired,"shot":shot,"acts":dict(acts),"bodies":dict(bodies),"heads":dict(heads)}}))
'''
    cmd = ["xvfb-run", "-a", "--server-args=-screen 0 640x480x24",
           sys.executable, "-c", code]
    p = subprocess.run(cmd, capture_output=True, text=True)
    for line in p.stdout.splitlines():
        if line.startswith("JSON"):
            out = json.loads(line[4:])
            out["name"] = name
            return out
    return {"name": name, "error": (p.stderr or "no output")[-200:]}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--jobs", "-j", type=int, default=5)
    ap.add_argument("--stride", type=int, default=4,
                    help="sample every Nth tick (default 4)")
    ap.add_argument("--level", default="", help="one level only")
    ap.add_argument("--out", default="", help="also write raw JSON here")
    a = ap.parse_args()

    names = sorted(p.stem for p in TRACES.glob("*-sweep-*.sltrace"))
    if a.level:
        names = [n for n in names if n.rsplit("-sweep-", 1)[0] == a.level]
    if not names:
        print("no sweeps found", file=sys.stderr)
        return 1

    print(f"sampling {len(names)} sweeps, {a.jobs} at a time...", flush=True)
    rows = []
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        futs = {ex.submit(sample, n, a.stride): n for n in names}
        for i, fut in enumerate(as_completed(futs), 1):
            r = fut.result()
            rows.append(r)
            print(f"  [{i}/{len(names)}] {r['name']}"
                  f"{' FAILED' if 'error' in r else ''}", flush=True)

    good = [r for r in rows if "error" not in r]
    bad = [r for r in rows if "error" in r]
    pct = lambda n, d: (100.0 * n / d) if d else 0.0

    print(f"\n{'sweep':26} {'ticks':>6} {'chrs':>5} {'valid':>6} "
          f"{'saw':>5} {'heard':>6} {'fire':>5}  top actions")
    for r in sorted(good, key=lambda r: r["name"]):
        top = sorted(r["acts"].items(), key=lambda kv: -kv[1])[:3]
        names_top = ", ".join(f"{ACTS[int(k)][4:]}" for k, _ in top)
        print(f"{r['name']:26} {r['ticks']:>6} {r['distinct']:>5} "
              f"{pct(r['valid'], r['tot']):>5.0f}% {pct(r['saw'], r['valid']):>4.0f}% "
              f"{pct(r['heard'], r['valid']):>5.0f}% {pct(r['fired'], r['valid']):>4.0f}%  {names_top}")

    print("\nby preset:")
    by = collections.defaultdict(list)
    for r in good:
        by[r["name"].rsplit("-sweep-", 1)[1]].append(r)
    print(f"  {'preset':9} {'sweeps':>6} {'chrs':>6} {'saw':>6} {'heard':>6} {'fire':>6} {'shot-at':>8}")
    for k in sorted(by):
        g = by[k]
        v = sum(r["valid"] for r in g)
        print(f"  {k:9} {len(g):>6} {sum(r['distinct'] for r in g):>6} "
              f"{pct(sum(r['saw'] for r in g), v):>5.0f}% {pct(sum(r['heard'] for r in g), v):>5.0f}% "
              f"{pct(sum(r['fired'] for r in g), v):>5.0f}% {pct(sum(r['shot'] for r in g), v):>7.0f}%")

    tot_acts = collections.Counter()
    for r in good:
        for k, n in r["acts"].items():
            tot_acts[int(k)] += n
    print("\naction distribution across all sweeps:")
    for k, n in tot_acts.most_common(12):
        print(f"  {ACTS[k][4:]:18} {n:>10,}  {pct(n, sum(tot_acts.values())):>5.1f}%")
    tot_bodies = collections.Counter()
    for r in good:
        for k, n in r.get("bodies", {}).items():
            tot_bodies[int(k)] += n
    if tot_bodies:
        total_b = sum(tot_bodies.values())
        print("\ncharacter types seen (by samples):")
        for k, n in tot_bodies.most_common(16):
            print(f"  {BODIES.get(k, f'body{k}'):26} {n:>10,}  {pct(n, total_b):>5.1f}%")

    print(f"\n  {sum(r['distinct'] for r in good):,} distinct characters, "
          f"{sum(r['valid'] for r in good):,} valid samples, "
          f"{sum(r['ticks'] for r in good):,} ticks")
    if bad:
        print(f"\n  {len(bad)} failed: {', '.join(r['name'] for r in bad)}")
    if a.out:
        Path(a.out).write_text(json.dumps(rows, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
