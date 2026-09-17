#!/usr/bin/env python3
"""Rebuild each recording's direct-boot ROM from its .spec sidecar.

Why this exists: symbols must be resolved against the ROM being run. A
direct-boot ROM carries extra defines, which changes code size and shifts
symbols above some address, so reading it through the matching build's map
fails SILENTLY - g_CurrentPlayer resolves to a null word, _read_player returns
nothing, and the player hash is eight zero bytes on every tick. The gate passes
because zeros are deterministic, while nothing about the player is verified.

direct-boot now writes a .map beside each ROM, but the ROMs already on disk
predate that. This rebuilds them so each gets its map.

The ROM BYTES must not change - the traces record a rom_sha1 and are matched by
it. Rebuilding from the same source is deterministic, so the hash is verified
after every build and a mismatch is reported rather than ignored.

Builds are SEQUENTIAL on purpose: the recipe touches shared sources and runs
make in one tree, so running two at once would interleave and corrupt both.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
INPUTS = REPO / "tools/trace/inputs"
TRACES = REPO / "tools/trace/traces"
DIRECT = REPO / "build/u/direct"


def flags_from_spec(spec: str, name: str) -> list[str]:
    """LEVEL=dam DIFFICULTY=007 TOUGH=10 ALLY=1 S007=a/d/h/r -> make flags."""
    out, s007 = [], ""
    for tok in spec.split():
        k, _, v = tok.partition("=")
        if k == "S007":
            s007 = v
        elif k == "ALLY":
            if v:
                out.append(f"ALLY_INVINCIBLE={v}")
        elif v:
            out.append(f"{k}={v}")
    if s007:
        acc, dmg, hp, rea = (s007.split("/") + ["", "", "", ""])[:4]
        for key, val in (("S007_ACCURACY", acc), ("S007_DAMAGE", dmg),
                         ("S007_HEALTH", hp), ("S007_REACTION", rea)):
            if val:
                out.append(f"{key}={val}")
    out.append(f"SL_NAME={name}")
    return out


def trace_rom_sha1(name: str) -> str | None:
    p = TRACES / f"{name}.sltrace"
    if not p.exists():
        return None
    raw = p.read_bytes()[:800]
    i = raw.index(b"{")
    depth = 0
    for j in range(i, len(raw)):
        c = raw[j:j + 1]
        if c == b"{":
            depth += 1
        elif c == b"}":
            depth -= 1
            if depth == 0:
                return json.loads(raw[i:j + 1]).get("rom_sha1")
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--level", default="", help="one recording instead of all")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    specs = sorted(INPUTS.glob("*.spec"))
    if a.level:
        specs = [p for p in specs if p.stem == a.level]
    if not specs:
        print("no .spec sidecars match", file=sys.stderr)
        return 1

    ok = changed = failed = 0
    for n, sp in enumerate(specs, 1):
        name = sp.stem
        flags = flags_from_spec(sp.read_text().strip(), name)
        if a.dry_run:
            print(f"  [{n}/{len(specs)}] make direct-boot {' '.join(flags)}")
            continue

        want = trace_rom_sha1(name)
        r = subprocess.run(["make", "direct-boot", *flags], cwd=REPO,
                           capture_output=True, text=True)
        rom = DIRECT / f"ge007.u.{name}.z64"
        if r.returncode != 0 or not rom.exists():
            print(f"  [{n}/{len(specs)}] {name}: BUILD FAILED")
            print("   ", "\n    ".join(r.stdout.strip().splitlines()[-4:]))
            failed += 1
            continue

        got = hashlib.sha1(rom.read_bytes()).hexdigest()
        have_map = rom.with_suffix(".map").exists()
        if want and got != want:
            # Not fatal to the build, but it invalidates the trace: say so
            # loudly rather than let a later rebaseline fail with "no ROM
            # matches the hash this trace was recorded with".
            print(f"  [{n}/{len(specs)}] {name}: ROM HASH CHANGED "
                  f"{want[:12]} -> {got[:12]}  map={have_map}")
            changed += 1
        else:
            print(f"  [{n}/{len(specs)}] {name}: ok {got[:12]}  map={have_map}")
            ok += 1

    if not a.dry_run:
        print(f"\n  {ok} unchanged, {changed} hash-changed, {failed} failed")
    return 1 if (changed or failed) else 0


if __name__ == "__main__":
    raise SystemExit(main())
