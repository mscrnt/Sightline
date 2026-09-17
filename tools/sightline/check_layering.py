#!/usr/bin/env python3
"""Sightline — layering check.

Goal (docs/project-rules.md, "Layering goal"): src/game/ must not include
from src/gfx/, src/platform/, or
the libultra/RCP layers. That is what makes the AI track independently shippable
and lets the server build headless.

This is NOT true today. The decomp's src/game calls into libultra and the RCP
directly, and severing it is Phase 1's work. So the check runs in reporting mode:

  * every current violation is recorded in docs/layering-baseline.txt
  * the check fails ONLY on violations absent from that baseline
  * the remaining count prints on every run, pass or fail

The baseline must decrease monotonically and reach zero at the Phase 1 gate.
Never add to the baseline to make a build pass — if a change introduces a new
violation, the change is wrong.

usage:
  check_layering.py                      # check against baseline
  check_layering.py --write-baseline     # regenerate baseline (Phase 1 progress)
  check_layering.py --list               # print current violations, no check
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BASELINE = REPO / "docs" / "layering-baseline.txt"
GAME_DIR = REPO / "src" / "game"

SOURCE_SUFFIXES = {".c", ".h", ".s", ".inc"}

# Matches #include "x.h" and #include <x.h>, ignoring commented-out lines only
# in the crude sense that the regex requires the directive to start the line.
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')

# --- what counts as a forbidden layer -------------------------------------
# Directories under src/ that src/game may not reach into.
FORBIDDEN_DIRS = ("gfx", "platform", "libultra", "libultrare")

# The N64 SDK / RCP headers. src/game reaching these is exactly the coupling
# Phase 1 removes: they pull in the OS, the RSP/RDP command interface, and the
# cartridge/VI hardware surface.
SDK_HEADERS = {
    "ultra64.h",
    "gbi_extension.h",
    "os_extension.h",
    "sgidefs.h",
}
SDK_PREFIXES = ("PR/",)


def classify(include: str) -> str | None:
    """Return the layer name an include belongs to, or None if it is allowed."""
    inc = include.strip().replace("\\", "/")
    norm = inc.lstrip("./")

    # Direct reach into a forbidden source directory, e.g. "../libultra/foo.h"
    for part in norm.split("/"):
        if part in FORBIDDEN_DIRS:
            return "gfx/platform" if part in ("gfx", "platform") else "libultra"

    if norm.startswith(SDK_PREFIXES):
        return "rcp-sdk"

    if norm in SDK_HEADERS:
        return "rcp-sdk"

    return None


def collect() -> tuple[list[str], dict[str, int]]:
    """Return (sorted violation records, per-layer counts)."""
    records: set[str] = set()
    layers: dict[str, int] = {}

    if not GAME_DIR.is_dir():
        return [], {}

    for path in sorted(GAME_DIR.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        rel = path.relative_to(REPO).as_posix()
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for line in text.splitlines():
            m = INCLUDE_RE.match(line)
            if not m:
                continue
            inc = m.group(1)
            layer = classify(inc)
            if layer is None:
                continue
            records.add(f"{rel} -> {inc}")
            layers[layer] = layers.get(layer, 0) + 1

    return sorted(records), layers


def read_baseline() -> set[str]:
    if not BASELINE.is_file():
        return set()
    out = set()
    for line in BASELINE.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        out.add(line)
    return out


def write_baseline(records: list[str], layers: dict[str, int]) -> None:
    BASELINE.parent.mkdir(parents=True, exist_ok=True)
    header = [
        "# Sightline — layering baseline (GENERATED)",
        "#",
        "# Every line is a known, pre-existing include from src/game/ into a layer",
        "# it must not depend on: src/gfx/, src/platform/, or libultra/RCP.",
        "#",
        "# Regenerate with:  make check-layering-baseline",
        "#",
        "# This file may only ever SHRINK. It reaches zero at the Phase 1 gate.",
        "# Never add a line here to make a build pass: a new violation means the",
        "# change that introduced it is wrong. See docs/project-rules.md, 'Layering goal'.",
        "#",
        f"# violations: {len(records)}",
    ]
    for layer in sorted(layers):
        header.append(f"#   {layer}: {layers[layer]} include site(s)")
    header.append("")
    BASELINE.write_text("\n".join(header + records) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description="Sightline layering check")
    ap.add_argument("--write-baseline", action="store_true",
                    help="regenerate docs/layering-baseline.txt from current state")
    ap.add_argument("--list", action="store_true",
                    help="print current violations and exit 0")
    args = ap.parse_args()

    records, layers = collect()
    files = len({r.split(" -> ")[0] for r in records})

    if args.list:
        for r in records:
            print(r)
        print(f"\n{len(records)} violation(s) across {files} file(s)")
        return 0

    if args.write_baseline:
        write_baseline(records, layers)
        print(f"layering: wrote baseline with {len(records)} violation(s) "
              f"across {files} file(s) -> {BASELINE.relative_to(REPO)}")
        for layer in sorted(layers):
            print(f"layering:   {layer}: {layers[layer]} include site(s)")
        return 0

    baseline = read_baseline()
    current = set(records)

    new = sorted(current - baseline)
    fixed = sorted(baseline - current)

    # The headline number, printed on every run per the Session 0 spec.
    print(f"layering: {len(current)} violation(s) remaining "
          f"across {files} file(s) [baseline {len(baseline)}]")
    for layer in sorted(layers):
        print(f"layering:   {layer}: {layers[layer]} include site(s)")

    if fixed:
        print(f"layering: {len(fixed)} baseline violation(s) now fixed - "
              f"run 'make check-layering-baseline' to lock the progress in")

    if new:
        print("", file=sys.stderr)
        print("LAYERING CHECK FAILED - "
              f"{len(new)} new violation(s) not in the baseline:", file=sys.stderr)
        for r in new:
            print(f"  {r}", file=sys.stderr)
        print("", file=sys.stderr)
        print("src/game/ must not depend on src/gfx/, src/platform/, or the",
              file=sys.stderr)
        print("libultra/RCP layers. Do NOT add these to the baseline to get a",
              file=sys.stderr)
        print("green build - the baseline only ever shrinks. See docs/project-rules.md.",
              file=sys.stderr)
        return 1

    if not baseline:
        print("layering: no baseline file yet - "
              "run 'make check-layering-baseline' to create it")

    print("layering: OK - no new violations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
