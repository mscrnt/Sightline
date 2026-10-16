#!/usr/bin/env python3
"""Synthetic tests for gedocs.py indexing on Windows defaults.

NO REAL NOTES. Every fixture is generated here - Unicode filenames, UTF-8
text, cp1252 bytes that are invalid UTF-8, an excluded Rand++ note - and the
index cache is redirected into a temporary directory, so build/docs is never
touched. Run it with:

    .venv\\Scripts\\python.exe tools\\docs\\test_gedocs.py
    .venv\\Scripts\\python.exe tools\\docs\\test_gedocs.py --fixture-out DIR

--fixture-out also leaves an UNREPAIRED truncated cache beside a synthetic note
root in DIR, newer than every note, so index_is_stale can be checked read-only.

The regressions it pins, both measured 2026-10-12 on Windows: the index write
died on U+FFFD under the cp1252 default, and the 18-row index it left behind
was judged fresh because only mtimes were compared.
"""

from __future__ import annotations

import hashlib
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import gedocs  # noqa: E402

FAILURES: list[str] = []
PASSES = 0

# Ω is outside cp1252, so the old cp1252 index write cannot encode this name.
UNICODE_NAME = "Ünïcode ñotes – Ω.txt"
UNICODE_TEXT = "Résumé — clipping ✓ Ω\n"
LEGACY_NAME = "legacy-cp1252.txt"
# Invalid UTF-8, and 0x81/0x9D are also UNDEFINED in cp1252: decoded with
# errors="replace" under either codec they become U+FFFD - the character the
# real corpus's index write died on.
LEGACY_BYTES = b"\x93Smart quoted\x94 \x81undefined\x9d clipping header\r\nbody\r\n"
EXCLUDED = Path(gedocs.EXCLUDE_PREFIX) / "hack-format.txt"


def check(name: str, cond: bool, detail: str = "") -> None:
    global PASSES
    if cond:
        PASSES += 1
        print("  PASS  %s" % name)
    else:
        FAILURES.append(name)
        print("  FAIL  %s%s" % (name, ("  -- " + detail) if detail else ""))


def make_notes(root: Path) -> list[str]:
    """Write the synthetic corpus; return the eligible relpaths."""
    files = {
        Path("ucode05.txt"): "Microcode command notes\nG_VTX vertex\n".encode("utf-8"),
        Path(UNICODE_NAME): UNICODE_TEXT.encode("utf-8"),
        Path(LEGACY_NAME): LEGACY_BYTES,
        Path("sub") / "dir" / "nested.txt": "\f\tNested\tnote with tabs\n".encode("utf-8"),
        EXCLUDED: "ROM hack format - must never be indexed\n".encode("utf-8"),
    }
    for rel, data in files.items():
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_bytes(data)
    return sorted(str(rel) for rel in files if rel != EXCLUDED)


def use_cache(cache: Path) -> None:
    gedocs.CACHE_DIR = cache
    gedocs.INDEX = cache / "index.tsv"
    gedocs.MARKER = cache / "last-lookup.json"


def age_notes(root: Path, seconds: float) -> None:
    then = time.time() - seconds
    for dp, _dn, fns in os.walk(root):
        for f in fns:
            os.utime(Path(dp) / f, (then, then))


def truncate_newer(index: Path, keep: int) -> None:
    """Keep the first `keep` valid rows; stamp the cache newer than any note."""
    lines = index.read_text(encoding="utf-8").splitlines()[:keep]
    index.write_text("".join(l + "\n" for l in lines), encoding="utf-8", newline="\n")
    now = time.time() + 5
    os.utime(index, (now, now))


def sha256(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def test_index(tmp: Path) -> None:
    print("index build, encoding and exclusion")
    root, cache = tmp / "notes", tmp / "cache"
    eligible = make_notes(root)
    age_notes(root, 3600)
    use_cache(cache)

    rows = gedocs.ensure_index(root, force=True)
    rels = [r for r, _s, _h in rows]
    check("rows equal the eligible walk", rels == eligible, f"{rels} vs {eligible}")
    check("excluded note is not indexed", str(EXCLUDED) not in rels)
    raw = gedocs.INDEX.read_bytes()
    text = raw.decode("utf-8")                     # strict: the cache is UTF-8
    check("cache is strict UTF-8 with the Unicode filename", UNICODE_NAME in text)
    check("invalid UTF-8 bytes become U+FFFD, not a crash", "�" in text)
    check("cache uses LF only", b"\r" not in raw)
    check("no temp file left behind", not (cache / "index.tsv.tmp").exists())
    heads = {r: h for r, _s, h in gedocs.read_index()[0]}
    check("Unicode head round-trips", heads.get(UNICODE_NAME) == UNICODE_TEXT.strip(),
          repr(heads.get(UNICODE_NAME)))
    check("tabs and form feeds collapse in the head",
          heads.get(str(Path("sub") / "dir" / "nested.txt")) == "Nested note with tabs")
    check("fresh index is not stale", gedocs.index_is_stale(root) is False)

    print("truncation recovery")
    truncate_newer(gedocs.INDEX, keep=2)
    mtime_only = max((root / r).stat().st_mtime for r in eligible) > gedocs.INDEX.stat().st_mtime
    check("old mtime-only check would call it fresh", mtime_only is False)
    check("truncated newer cache is stale", gedocs.index_is_stale(root) is True)
    rows = gedocs.ensure_index(root)
    check("ensure_index rebuilds to the walked count", len(rows) == len(eligible),
          f"{len(rows)} vs {len(eligible)}")
    check("rebuilt index is fresh", gedocs.index_is_stale(root) is False)

    print("other mismatches")
    text = gedocs.INDEX.read_text(encoding="utf-8")
    gedocs.INDEX.write_text(text.replace("ucode05.txt", "renamed.txt"), encoding="utf-8",
                            newline="\n")
    check("same count, different relpaths is stale", gedocs.index_is_stale(root) is True)
    gedocs.INDEX.write_text(text + "garbage-without-tab\n", encoding="utf-8", newline="\n")
    check("an unparsable row is stale", gedocs.index_is_stale(root) is True)
    gedocs.ensure_index(root, force=True)
    later = time.time() + 30
    os.utime(root / EXCLUDED, (later, later))
    check("touching an excluded note does not make it stale",
          gedocs.index_is_stale(root) is False)


DRIVER = r"""
import sys, pathlib
sys.stderr.write('pre-main stdout encoding: %s\n' % sys.stdout.encoding)
sys.path.insert(0, sys.argv[1])
import gedocs
cache = pathlib.Path(sys.argv[2])
gedocs.CACHE_DIR, gedocs.INDEX, gedocs.MARKER = cache, cache / 'index.tsv', cache / 'last-lookup.json'
sys.argv = ['gedocs.py'] + sys.argv[3:]
raise SystemExit(gedocs.main())
"""


def run_cli(tmp: Path, *args: str) -> tuple[int, bytes, str]:
    env = dict(os.environ, GEDOCS_DOCS_ROOT=str(tmp / "notes"),
               PYTHONIOENCODING="cp1252", PYTHONUTF8="0")
    p = subprocess.run([sys.executable, "-X", "utf8=0", "-c", DRIVER,
                        str(Path(gedocs.__file__).parent), str(tmp / "cache"), *args],
                       capture_output=True, env=env)
    return p.returncode, p.stdout, p.stderr.decode("utf-8", "replace")


def test_cli(tmp: Path) -> None:
    print("CLI output under forced cp1252 stdout")
    code, out, err = run_cli(tmp, "search", "clipping", "--content")
    check("caller stdout really was cp1252", "pre-main stdout encoding: cp1252" in err, err)
    check("search exits 0", code == 0, err)
    try:
        decoded = out.decode("utf-8")
        ok = True
    except UnicodeDecodeError as exc:
        decoded, ok = "", False
        err += repr(exc)
    check("search output is strict UTF-8", ok, err)
    check("search output carries the Unicode filename", UNICODE_NAME in decoded)
    check("search --content hits the legacy note", LEGACY_NAME in decoded)
    code, out, err = run_cli(tmp, "show", "legacy-cp1252")
    check("show exits 0 and emits UTF-8 U+FFFD",
          code == 0 and "�" in out.decode("utf-8", "strict"), err)
    code, out, err = run_cli(tmp, "show", "Ünïcode")
    check("show of the Unicode note prints its text",
          code == 0 and "Résumé — clipping ✓" in out.decode("utf-8", "strict"), err)


def write_fixture(dest: Path) -> None:
    """Leave an unrepaired truncated cache for read-only reviewer checks."""
    root, cache = dest / "notes", dest / "cache"
    eligible = make_notes(root)
    age_notes(root, 3600)
    use_cache(cache)
    gedocs.ensure_index(root, force=True)
    truncate_newer(gedocs.INDEX, keep=2)
    print("fixture: %s" % dest)
    print("  eligible notes: %d (excluded: %s)" % (len(eligible), EXCLUDED))
    print("  truncated cache rows: %d" % len(gedocs.read_index()[0]))
    print("  index.tsv sha256: %s" % sha256(gedocs.INDEX))
    for dp, _dn, fns in sorted(os.walk(root)):
        for f in sorted(fns):
            p = Path(dp) / f
            print("  note %s sha256: %s" % (p.relative_to(root), sha256(p)))
    print("  index_is_stale (expect True): %s" % gedocs.index_is_stale(root))


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if len(sys.argv) == 3 and sys.argv[1] == "--fixture-out":
        write_fixture(Path(sys.argv[2]).resolve())
        return 0
    with tempfile.TemporaryDirectory(prefix="gedocs-test-") as d:
        test_index(Path(d))
        test_cli(Path(d))
    print("\n%d passed, %d failed" % (PASSES, len(FAILURES)))
    for f in FAILURES:
        print("  FAILED: %s" % f)
    return 1 if FAILURES else 0


if __name__ == "__main__":
    raise SystemExit(main())
