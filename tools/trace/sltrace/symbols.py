"""Resolve game symbols to RDRAM addresses from the matching build's link map.

The decomp gives us a real symbol table, which is what lets the harness read
defined game state instead of guessing at memory. Nothing here needs the game
to be modified: the ROM stays byte-identical and we read it from outside.
"""

from __future__ import annotations

import re
from pathlib import Path

# A map line for a plain symbol looks like:      0x80024460                g_randomSeed
SYM_RE = re.compile(r"^\s+0x([0-9a-f]{8,16})\s+([A-Za-z_][A-Za-z0-9_]*)\s*$")

KSEG0 = 0x80000000


class SymbolTable:
    def __init__(self, symbols: dict[str, int]):
        self._syms = symbols

    @classmethod
    def from_map(cls, map_path: str | Path) -> "SymbolTable":
        syms: dict[str, int] = {}
        for line in Path(map_path).read_text(errors="replace").splitlines():
            m = SYM_RE.match(line)
            if not m:
                continue
            addr = int(m.group(1), 16)
            # Only RDRAM-resident symbols are readable state.
            if 0x80000000 <= addr < 0x80800000:
                syms.setdefault(m.group(2), addr)
        if not syms:
            raise RuntimeError(f"no symbols parsed from {map_path}")
        return cls(syms)

    def __contains__(self, name: str) -> bool:
        return name in self._syms

    def __len__(self) -> int:
        return len(self._syms)

    def addr(self, name: str) -> int:
        try:
            return self._syms[name]
        except KeyError:
            raise KeyError(
                f"symbol {name!r} not in the link map. The map is produced by "
                f"'make matching'; if the symbol was renamed upstream, the state "
                f"schema needs updating rather than the map."
            ) from None

    def require(self, names: list[str]) -> None:
        """Fail loudly and all at once, rather than one symbol per run."""
        missing = [n for n in names if n not in self._syms]
        if missing:
            raise KeyError(
                "state schema references symbols absent from the link map: "
                + ", ".join(sorted(missing))
            )
