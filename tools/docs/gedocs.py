#!/usr/bin/env python3
"""Route a question to the documentation that actually answers it.

Zoinkity's notes are 617 files. Searching them is a DECISION, and under
momentum that decision does not get made: four times in one session the decomp
was read instead, two wrong root causes were published, and hours were spent on
questions a sub-minute lookup settled. This tool exists to remove the decision.

    gedocs.py areas                     what subsystems are mapped
    gedocs.py topic display-lists       the authoritative files for an area
    gedocs.py for src/game/propobj.c    which areas govern a source file
    gedocs.py search vertex command     rank the corpus for a phrase
    gedocs.py show ucode05.txt vertex   print matching lines with context
    gedocs.py status                    index state, last lookup

`--json` on any subcommand emits machine-readable output, so an MCP server can
wrap this without reimplementing it.

Every successful lookup stamps a marker the docs-first hook reads, which is how
the hook can say "no doc lookup this session" instead of nagging blindly.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ROUTING = REPO / "docs/doc-routing.json"
CACHE_DIR = REPO / "build/docs"
INDEX = CACHE_DIR / "index.tsv"
MARKER = CACHE_DIR / "last-lookup.json"

# Notes for a ROM HACK's modified formats. Never authority for the base game.
EXCLUDE_PREFIX = "Rand++ Documentation (Misc)"


def load_routing() -> dict:
    try:
        return json.loads(ROUTING.read_text())
    except Exception as exc:                                  # noqa: BLE001
        die(f"cannot read {ROUTING}: {exc}")


def docs_root(routing: dict) -> Path:
    """Resolve the notes root. Relocatable by design.

    Order:
      1. $GEDOCS_DOCS_ROOT                    - explicit override, always wins
      2. docs_root from doc-routing.json      - but only if it exists
      3. ../goldeneye_docs/notes/GE Documentation, beside this repo

    (2) is tested rather than trusted, and ships EMPTY. The recorded value is
    an absolute path written by whichever machine last edited the file, so it
    does not survive a move between hosts. It used to name this project's
    pre-migration location; measured 2026-09-01, that path still exists under
    WSL at the same commit as the canonical checkout, so it was serving
    identical content. It was a latent trap rather than an active fault - the
    pre-migration copy is kept deliberately frozen, so it would have outranked
    the canonical sibling the moment the canonical copy moved ahead.

    (3) is the rule that actually holds, and with (2) empty it is the default.
    The notes are cloned BESIDE the repo on every platform this project is
    developed on, under Windows and under WSL and Linux alike, so a sibling
    lookup is correct everywhere while hardcoding no machine's layout.
    """
    env = os.environ.get("GEDOCS_DOCS_ROOT", "").strip()
    if env:
        return Path(env)
    recorded = str(routing.get("docs_root", "")).strip()
    if recorded and Path(recorded).is_dir():
        return Path(recorded)
    sibling = REPO.parent / "goldeneye_docs" / "notes" / "GE Documentation"
    if sibling.is_dir():
        return sibling
    # Nothing resolved. Return the recorded path so the failure message names
    # what was actually configured rather than a guess.
    return Path(recorded) if recorded else sibling


def die(msg: str, code: int = 1):
    print(f"gedocs: {msg}", file=sys.stderr)
    raise SystemExit(code)


# --------------------------------------------------------------------------
# index


def build_index(root: Path) -> list[tuple[str, int, str]]:
    """(relpath, size, first meaningful line) for every note."""
    rows = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in filenames:
            p = Path(dirpath) / fn
            rel = str(p.relative_to(root))
            if rel.startswith(EXCLUDE_PREFIX):
                continue
            try:
                size = p.stat().st_size
                head = ""
                with open(p, errors="replace") as fh:
                    for _ in range(8):
                        line = fh.readline()
                        if not line:
                            break
                        line = line.strip()
                        if line and not set(line) <= set("_-=+*#/ \t"):
                            # Collapse ALL whitespace: these notes contain tabs
                            # and form feeds, which would add fields or split
                            # the row across lines - and the parser dropped
                            # such rows SILENTLY. 337 of 496 notes were
                            # invisible to search before this.
                            head = " ".join(line.split())[:110]
                            break
            except Exception as exc:                          # noqa: BLE001
                size, head = 0, f"<{exc}>"
            rows.append((rel, size, head))
    rows.sort()
    return rows


def index_is_stale(root: Path) -> bool:
    if not INDEX.exists():
        return True
    try:
        newest = max(
            (Path(dp) / f).stat().st_mtime
            for dp, _dn, fns in os.walk(root) for f in fns
        )
        return newest > INDEX.stat().st_mtime
    except ValueError:
        return True
    except Exception:                                          # noqa: BLE001
        return False


def ensure_index(root: Path, force: bool = False) -> list[tuple[str, int, str]]:
    if not root.is_dir():
        die(f"docs root not found: {root}\n"
            f"        clone the notes repo beside this one as "
            f"{REPO.parent / 'goldeneye_docs'}, or set "
            f"$GEDOCS_DOCS_ROOT, or fix docs_root in {ROUTING}")
    if force or index_is_stale(root):
        rows = build_index(root)
        CACHE_DIR.mkdir(parents=True, exist_ok=True)
        with open(INDEX, "w") as out:
            for rel, size, head in rows:
                out.write(f"{rel}\t{size}\t{head}\n")
        return rows
    rows, dropped = [], 0
    for line in INDEX.read_text(errors="replace").split("\n"):
        if not line.strip():
            continue
        parts = line.split("\t", 2)          # maxsplit: a stray tab must not drop the row
        if len(parts) >= 2:
            try:
                rows.append((parts[0], int(parts[1] or 0),
                             parts[2] if len(parts) > 2 else ""))
                continue
            except ValueError:
                pass
        dropped += 1
    if dropped:
        print(f"gedocs: WARNING {dropped} index rows unparsable - rebuild with "
              f"'gedocs.py index'", file=sys.stderr)
    return rows


def stamp(what: str) -> None:
    """Record that a lookup happened - the hook reports on this."""
    try:
        CACHE_DIR.mkdir(parents=True, exist_ok=True)
        MARKER.write_text(json.dumps({"ts": int(time.time()), "what": what}))
    except Exception:                                          # noqa: BLE001
        pass


# --------------------------------------------------------------------------
# resolution helpers


def resolve_targets(root: Path, spec: str) -> list[str]:
    """A routing entry may name a file or a directory. Expand directories."""
    p = root / spec
    if p.is_dir():
        out = []
        for dp, _dn, fns in os.walk(p):
            for f in sorted(fns):
                out.append(str((Path(dp) / f).relative_to(root)))
        return sorted(out)
    return [spec] if p.exists() else []


def areas_for_path(routing: dict, src: str) -> list[str]:
    """Which areas govern a source file. Substring match on the governs list."""
    s = src.replace("\\", "/").lstrip("./")
    hits = []
    for name, area in routing["areas"].items():
        for gov in area.get("governs", []):
            g = gov.lstrip("./")
            if s.endswith(g) or g.endswith(s) or Path(s).name == Path(g).name:
                hits.append(name)
                break
    return hits


# --------------------------------------------------------------------------
# subcommands


def cmd_areas(a, routing, root):
    rows = [(n, ar.get("why", "")) for n, ar in routing["areas"].items()]
    if a.json:
        print(json.dumps({"areas": [{"name": n, "why": w} for n, w in rows]}, indent=2))
        return 0
    print("mapped areas:")
    for n, w in rows:
        print(f"  {n:20} {w}")
    print("\nestablished NOT covered by the docs:")
    for topic, note, when in routing.get("not_covered", []):
        print(f"  {topic:20} {note}  [{when}]")
    return 0


def cmd_topic(a, routing, root):
    area = routing["areas"].get(a.area)
    if not area:
        near = [n for n in routing["areas"] if a.area.lower() in n.lower()]
        die(f"unknown area '{a.area}'"
            + (f"; did you mean: {', '.join(near)}" if near else
               f"; run 'gedocs.py areas'"))
    ensure_index(root)
    stamp(f"topic {a.area}")
    files = []
    for spec, note in area["files"]:
        found = resolve_targets(root, spec)
        files.append({"spec": spec, "note": note, "exists": bool(found),
                      "resolved": found[:12]})
    if a.json:
        print(json.dumps({"area": a.area, "why": area.get("why"),
                          "files": files, "governs": area.get("governs", []),
                          "learned": area.get("learned")}, indent=2))
        return 0
    print(f"{a.area}: {area.get('why','')}\n")
    for f in files:
        mark = " " if f["exists"] else "!"
        print(f" {mark}{f['spec']}")
        print(f"    {f['note']}")
        if len(f["resolved"]) > 1:
            for r in f["resolved"][:8]:
                print(f"      - {Path(r).name}")
        if not f["exists"]:
            print("      (NOT FOUND on disk - path may have moved)")
    if area.get("learned"):
        print(f"\n learned: {area['learned']}")
    print(f"\n root: {root}")
    return 0


def cmd_for(a, routing, root):
    hits = areas_for_path(routing, a.path)
    ensure_index(root)
    stamp(f"for {a.path}")
    if a.json:
        out = {"path": a.path, "areas": []}
        for h in hits:
            ar = routing["areas"][h]
            out["areas"].append({"name": h, "why": ar.get("why"),
                                 "files": ar["files"], "learned": ar.get("learned")})
        print(json.dumps(out, indent=2))
        return 0
    if not hits:
        print(f"no mapped area governs {a.path}")
        print("if the docs turn out to cover it, ADD the mapping to docs/doc-routing.json")
        return 0
    print(f"{a.path} is governed by: {', '.join(hits)}\n")
    for h in hits:
        ar = routing["areas"][h]
        print(f" [{h}] {ar.get('why','')}")
        for spec, note in ar["files"]:
            print(f"    {spec}")
            print(f"        {note}")
        if ar.get("learned"):
            print(f"    learned: {ar['learned']}")
        print()
    return 0


def cmd_search(a, routing, root):
    rows = ensure_index(root)
    stamp("search " + " ".join(a.terms))
    terms = [t.lower() for t in a.terms]
    scored = []
    for rel, size, head in rows:
        hay = (rel + " " + head).lower()
        score = sum(3 for t in terms if t in Path(rel).name.lower())
        score += sum(1 for t in terms if t in hay)
        if score:
            scored.append((score, rel, size, head))
    scored.sort(key=lambda r: (-r[0], r[1]))

    content = []
    if a.content:
        pat = re.compile("|".join(re.escape(t) for t in terms), re.I)
        for rel, _size, _head in rows:
            if len(content) >= a.limit:
                break
            try:
                text = (root / rel).read_text(errors="replace")
            except Exception:                                  # noqa: BLE001
                continue
            hits = [ln.strip() for ln in text.splitlines() if pat.search(ln)]
            if hits:
                content.append({"file": rel, "hits": hits[:4]})

    if a.json:
        print(json.dumps({"terms": a.terms,
                          "by_name": [{"score": s, "file": r, "size": z, "head": h}
                                      for s, r, z, h in scored[:a.limit]],
                          "by_content": content}, indent=2))
        return 0
    if scored:
        print("by filename/summary:")
        for s, rel, size, head in scored[:a.limit]:
            print(f"  [{s}] {rel}  ({size}B)")
            if head:
                print(f"        {head}")
    else:
        print("no filename matches")
    if a.content:
        print("\nby content:")
        for c in content:
            print(f"  {c['file']}")
            for h in c["hits"]:
                print(f"      {h[:120]}")
    if not scored and not content:
        print("\nNothing found. If you then answer from the decomp instead, record it:")
        print("  add a 'not_covered' entry to docs/doc-routing.json so nobody re-searches.")
    return 0


def cmd_show(a, routing, root):
    matches = [r for r, _s, _h in ensure_index(root)
               if a.file.lower() in r.lower()]
    if not matches:
        die(f"no note matching '{a.file}'")
    rel = matches[0]
    stamp(f"show {rel}")
    text = (root / rel).read_text(errors="replace")
    lines = text.splitlines()
    if not a.pattern:
        out = lines[:a.limit]
        body = [{"line": i + 1, "text": t} for i, t in enumerate(out)]
    else:
        pat = re.compile(a.pattern, re.I)
        body = []
        for i, t in enumerate(lines):
            if pat.search(t):
                lo, hi = max(0, i - a.context), min(len(lines), i + a.context + 1)
                for j in range(lo, hi):
                    body.append({"line": j + 1, "text": lines[j]})
                if len(body) >= a.limit:
                    break
    if a.json:
        print(json.dumps({"file": rel, "lines": body}, indent=2))
        return 0
    print(f"--- {rel}")
    seen = set()
    for b in body:
        if b["line"] in seen:
            continue
        seen.add(b["line"])
        print(f"  {b['line']:5}  {b['text']}")
    if len(matches) > 1:
        print(f"\n({len(matches)-1} other notes also matched '{a.file}')")
    return 0


def cmd_status(a, routing, root):
    rows = ensure_index(root)
    last = None
    if MARKER.exists():
        try:
            last = json.loads(MARKER.read_text())
        except Exception:                                      # noqa: BLE001
            last = None
    info = {"docs_root": str(root), "root_exists": root.is_dir(),
            "notes_indexed": len(rows), "index": str(INDEX),
            "areas": len(routing["areas"]),
            "not_covered": len(routing.get("not_covered", [])),
            "last_lookup": last}
    if a.json:
        print(json.dumps(info, indent=2))
        return 0
    for k, v in info.items():
        print(f"  {k:16} {v}")
    return 0


def cmd_index(a, routing, root):
    rows = ensure_index(root, force=True)
    print(f"  indexed {len(rows)} notes -> {INDEX}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(prog="gedocs.py", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("areas", help="list mapped areas and known gaps").set_defaults(fn=cmd_areas)

    p = sub.add_parser("topic", help="authoritative files for an area")
    p.add_argument("area")
    p.set_defaults(fn=cmd_topic)

    p = sub.add_parser("for", help="which areas govern a source file")
    p.add_argument("path")
    p.set_defaults(fn=cmd_for)

    p = sub.add_parser("search", help="rank the corpus for a phrase")
    p.add_argument("terms", nargs="+")
    p.add_argument("--content", action="store_true", help="also grep file contents")
    p.add_argument("--limit", type=int, default=12)
    p.set_defaults(fn=cmd_search)

    p = sub.add_parser("show", help="print a note, optionally filtered")
    p.add_argument("file")
    p.add_argument("pattern", nargs="?")
    p.add_argument("--context", type=int, default=2)
    p.add_argument("--limit", type=int, default=80)
    p.set_defaults(fn=cmd_show)

    sub.add_parser("status", help="index state and last lookup").set_defaults(fn=cmd_status)
    sub.add_parser("index", help="force an index rebuild").set_defaults(fn=cmd_index)

    a = ap.parse_args()
    routing = load_routing()
    return a.fn(a, routing, docs_root(routing))


if __name__ == "__main__":
    raise SystemExit(main())
