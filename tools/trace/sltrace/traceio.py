"""Versioned trace file format.

A trace is an input stream plus the per-tick state hashes it produced. Traces
are committed as build artifacts, so the format is explicitly versioned and
carries the full reproduction context: which ROM, which schema, which emulator
configuration. A trace recorded against a different ROM or a different state
schema is not comparable, and the reader refuses rather than reporting a
meaningless divergence.

Layout:
    magic   8    b"SLTRACE\\0"
    fmtver  u16  format version
    hdrlen  u32  length of the JSON header
    header  ...  JSON: rom_sha1, schema_version, level, emu fingerprint, counts
    ticks   ...  repeated records, see _pack_tick
"""

from __future__ import annotations

import json
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path

MAGIC = b"SLTRACE\0"
FORMAT_VERSION = 2


@dataclass
class TraceHeader:
    rom_sha1: str
    schema_version: int
    level: str
    emu_fingerprint: str          # what must match for hashes to be comparable
    tick_count: int
    input_stream: str = ""        # path/id of the input recording, when one exists
    # Hash of the cartridge save the run STARTED from. A replay that begins
    # from different progress diverges in the menus, and the trace has no way
    # to show that the save was the cause - it looks like the game misbehaved.
    # Recording it here is what turns "mystery divergence" into "wrong save".
    eeprom_sha1: str = ""
    notes: str = ""


class TraceWriter:
    """Writes incrementally so a killed recorder does not lose the session.

    v1 buffered every tick and compressed at close(), which meant a crash - or
    simply closing the emulator window in a way that skipped cleanup - threw the
    whole recording away. A record-trace is the one artifact replay cannot
    regenerate (comparing replay against itself proves nothing), so losing it
    costs a real playthrough. v2 writes each tick straight out and only rewrites
    the tick count on close; a truncated file is still readable up to its last
    complete record.
    """

    def __init__(self, path, header: TraceHeader):
        self.path = Path(path)
        self.header = header
        self._n = 0
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._hdr_bytes = json.dumps(self.header.__dict__, sort_keys=True).encode()
        self._f = self.path.open("wb")
        self._f.write(MAGIC)
        self._f.write(struct.pack(">H", FORMAT_VERSION))
        self._f.write(struct.pack(">I", len(self._hdr_bytes)))
        self._hdr_off = self._f.tell()
        self._f.write(self._hdr_bytes)
        self._f.flush()

    def add(self, tick: int, frame_counter: int, composite: bytes,
            entity_hashes: dict[int, bytes]) -> None:
        rec = struct.pack(">IIH", tick, frame_counter & 0xFFFFFFFF, len(entity_hashes))
        rec += composite
        for idx in sorted(entity_hashes):
            rec += struct.pack(">H", idx) + entity_hashes[idx]
        self._f.write(rec)
        self._n += 1
        if self._n % 120 == 0:          # ~ every 2 seconds of gameplay
            self._f.flush()

    def close(self) -> None:
        if self._f.closed:
            return
        self._f.flush()
        # Rewrite the header with the final tick count; readers tolerate a stale
        # count by reading to EOF, so a killed writer still yields a usable file.
        self.header.tick_count = self._n
        hdr = json.dumps(self.header.__dict__, sort_keys=True).encode()
        if len(hdr) == len(self._hdr_bytes):
            self._f.seek(self._hdr_off)
            self._f.write(hdr)
        self._f.close()


@dataclass
class Tick:
    tick: int
    frame_counter: int
    composite: bytes
    entities: dict[int, bytes]


class Trace:
    def __init__(self, header: TraceHeader, ticks: list[Tick]):
        self.header = header
        self.ticks = ticks

    @classmethod
    def load(cls, path) -> "Trace":
        raw = Path(path).read_bytes()
        if raw[:8] != MAGIC:
            raise ValueError(f"{path}: not a Sightline trace (bad magic)")
        fmtver = struct.unpack(">H", raw[8:10])[0]
        if fmtver != FORMAT_VERSION:
            raise ValueError(
                f"{path}: trace format v{fmtver}, this build reads v{FORMAT_VERSION}"
            )
        hlen = struct.unpack(">I", raw[10:14])[0]
        header = TraceHeader(**json.loads(raw[14:14 + hlen]))
        body = raw[14 + hlen:]

        ticks, off = [], 0
        while off + 10 <= len(body):
            tick, fc, n = struct.unpack(">IIH", body[off:off + 10])
            if off + 26 + n * 10 > len(body):
                break               # truncated tail: keep what is complete
            off += 10
            composite = body[off:off + 16]
            off += 16
            ents = {}
            for _ in range(n):
                idx = struct.unpack(">H", body[off:off + 2])[0]
                ents[idx] = body[off + 2:off + 10]
                off += 10
            ticks.append(Tick(tick, fc, composite, ents))
        return cls(header, ticks)

    def comparable_with(self, other: "Trace") -> list[str]:
        """Return reasons these traces cannot be compared, empty if they can."""
        problems = []
        a, b = self.header, other.header
        if a.rom_sha1 != b.rom_sha1:
            problems.append(f"different ROM: {a.rom_sha1[:12]} vs {b.rom_sha1[:12]}")
        if a.schema_version != b.schema_version:
            problems.append(
                f"different state schema: v{a.schema_version} vs v{b.schema_version}")
        if a.eeprom_sha1 and b.eeprom_sha1 and a.eeprom_sha1 != b.eeprom_sha1:
            problems.append(
                f"different cartridge save: {a.eeprom_sha1[:12]} vs "
                f"{b.eeprom_sha1[:12]}\n"
                f"      the replay did not start from the progress this was "
                f"recorded with,\n"
                f"      so any divergence below is about the save, not the game")
        if a.emu_fingerprint != b.emu_fingerprint:
            problems.append(
                f"different emulator config:\n"
                f"      {a.emu_fingerprint}\n"
                f"      {b.emu_fingerprint}")
        return problems
