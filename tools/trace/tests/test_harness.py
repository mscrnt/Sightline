"""Unit tests for the trace harness.

Deliberately stdlib-only (unittest, no pytest) and ROM-free, so they run in CI
on any runner without the base ROM, the emulator, or a display.
"""

import hashlib
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from sltrace.symbols import SymbolTable
from sltrace.state import _word_swap, Memory, StateReader, SCHEMA_VERSION
from sltrace.traceio import Trace, TraceWriter, TraceHeader, FORMAT_VERSION
from sltrace.diff import find_divergences, format_report, _classify, Divergence


class FakeMem:
    """Backing store shaped like the emulator's RDRAM pointer."""

    def __init__(self, size=0x800000):
        self.buf = bytearray(size)

    def __getitem__(self, s):
        return self.buf[s]

    def write_word(self, addr, raw4):
        o = addr - 0x80000000
        # RDRAM holds words host-order, so store reversed relative to true value
        self.buf[o:o + 4] = raw4[::-1]


class TestSymbols(unittest.TestCase):
    def _table(self, text):
        with tempfile.NamedTemporaryFile("w", suffix=".map", delete=False) as f:
            f.write(text)
            f.flush()              # from_map reads the path, so flush first
            name = f.name
        return SymbolTable.from_map(name)

    def test_parses_rdram_symbols(self):
        st = self._table("                0x80024460                g_randomSeed\n")
        self.assertEqual(st.addr("g_randomSeed"), 0x80024460)

    def test_ignores_non_rdram_addresses(self):
        # Code at 0x7000xxxx is not RDRAM-resident state and must not be offered.
        with self.assertRaises(RuntimeError):
            self._table("                0x7000a450                randomGetNext\n")

    def test_missing_symbol_explains_itself(self):
        st = self._table("                0x80024460                g_randomSeed\n")
        with self.assertRaises(KeyError) as cm:
            st.addr("nope")
        self.assertIn("link map", str(cm.exception))

    def test_require_reports_all_missing_at_once(self):
        st = self._table("                0x80024460                g_randomSeed\n")
        with self.assertRaises(KeyError) as cm:
            st.require(["a_missing", "b_missing", "g_randomSeed"])
        msg = str(cm.exception)
        self.assertIn("a_missing", msg)
        self.assertIn("b_missing", msg)


class TestMemory(unittest.TestCase):
    def test_word_swap_is_per_word(self):
        self.assertEqual(_word_swap(b"\x01\x02\x03\x04\x05\x06\x07\x08"),
                         b"\x04\x03\x02\x01\x08\x07\x06\x05")

    def test_reads_are_byte_order_correct(self):
        fake = FakeMem()
        fake.write_word(0x80001000, struct.pack(">I", 0xDEADBEEF))
        mem = Memory(fake)
        self.assertEqual(mem.u32(0x80001000), 0xDEADBEEF)

    def test_float_roundtrip(self):
        fake = FakeMem()
        fake.write_word(0x80002000, struct.pack(">f", -12.5))
        self.assertAlmostEqual(Memory(fake).f32(0x80002000), -12.5, places=5)

    def test_out_of_range_address_rejected(self):
        with self.assertRaises(ValueError):
            Memory(FakeMem()).u32(0x90000000)

    def test_valid_ptr_bounds(self):
        mem = Memory(FakeMem())
        self.assertTrue(mem.valid_ptr(0x80000010))
        self.assertFalse(mem.valid_ptr(0x00000000))
        self.assertFalse(mem.valid_ptr(0x90000000))


class TestTraceIO(unittest.TestCase):
    def _hdr(self, **kw):
        base = dict(rom_sha1="abc123", schema_version=SCHEMA_VERSION,
                    level="facility", emu_fingerprint="fp", tick_count=0)
        base.update(kw)
        return TraceHeader(**base)

    def _write(self, path, n=5, hdr=None):
        w = TraceWriter(path, hdr or self._hdr())
        for i in range(n):
            w.add(i, i + 100, bytes([i]) * 16, {0: bytes([i]) * 8, 1: b"\x00" * 8})
        w.close()

    def test_roundtrip_preserves_ticks(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "t.sltrace"
            self._write(p, 7)
            t = Trace.load(p)
            self.assertEqual(len(t.ticks), 7)
            self.assertEqual(t.header.tick_count, 7)
            self.assertEqual(t.ticks[3].frame_counter, 103)
            self.assertEqual(t.ticks[3].entities[0], bytes([3]) * 8)

    def test_rejects_foreign_file(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "bad.sltrace"
            p.write_bytes(b"NOTATRACE" + b"\x00" * 40)
            with self.assertRaises(ValueError):
                Trace.load(p)

    def test_mismatched_cartridge_save_is_reported(self):
        # The failure this exists for: a replay started from different save
        # progress diverges in the menus, and nothing in the trace points at
        # the save - it reads as the game misbehaving. Cost a real debugging
        # detour once already.
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d) / "a", Path(d) / "b"
            self._write(a, hdr=self._hdr(eeprom_sha1="aaaa"))
            self._write(b, hdr=self._hdr(eeprom_sha1="bbbb"))
            problems = Trace.load(a).comparable_with(Trace.load(b))
            self.assertTrue(any("cartridge save" in p for p in problems))

    def test_absent_save_hash_does_not_block_comparison(self):
        # Traces recorded before saves were tracked, and runs with no save at
        # all, must still compare - the check is a guard, not a new demand.
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d) / "a", Path(d) / "b"
            self._write(a, hdr=self._hdr(eeprom_sha1=""))
            self._write(b, hdr=self._hdr(eeprom_sha1="bbbb"))
            problems = Trace.load(a).comparable_with(Trace.load(b))
            self.assertFalse(any("cartridge save" in p for p in problems))

    def test_incomparable_rom(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d) / "a", Path(d) / "b"
            self._write(a)
            self._write(b, hdr=self._hdr(rom_sha1="different"))
            problems = Trace.load(a).comparable_with(Trace.load(b))
            self.assertTrue(any("ROM" in p for p in problems))

    def test_incomparable_schema_and_emu(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d) / "a", Path(d) / "b"
            self._write(a)
            self._write(b, hdr=self._hdr(schema_version=99, emu_fingerprint="other"))
            problems = Trace.load(a).comparable_with(Trace.load(b))
            self.assertEqual(len(problems), 2)

    def test_identical_traces_are_comparable(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d) / "a", Path(d) / "b"
            self._write(a)
            self._write(b)
            self.assertEqual(Trace.load(a).comparable_with(Trace.load(b)), [])


class _T:
    """Minimal stand-in for a loaded trace."""
    def __init__(self, ticks):
        self.ticks = ticks
        self.header = TraceHeader("rom", SCHEMA_VERSION, "facility", "fp", len(ticks))


class _Tick:
    def __init__(self, tick, fc, composite, entities):
        self.tick, self.frame_counter = tick, fc
        self.composite, self.entities = composite, entities


def _mk(n, mutate=None):
    ticks = []
    for i in range(n):
        ents = {0: b"a" * 8, 1: b"b" * 8, 2: b"c" * 8}
        comp = bytes([i % 251]) * 16
        if mutate:
            comp, ents = mutate(i, comp, ents)
        ticks.append(_Tick(i, i + 10, comp, ents))
    return _T(ticks)


class TestDiff(unittest.TestCase):
    def test_identical_traces_have_no_divergence(self):
        self.assertEqual(find_divergences(_mk(20), _mk(20)), [])

    def test_finds_first_divergent_tick(self):
        def m(i, c, e):
            if i >= 7:
                e = dict(e); e[1] = b"X" * 8; c = b"\xff" * 16
            return c, e
        divs = find_divergences(_mk(20), _mk(20, m))
        self.assertEqual(divs[0].tick, 7)
        self.assertEqual(divs[0].entities, [1])

    def test_globals_only_when_no_entity_moves(self):
        def m(i, c, e):
            return (b"\xff" * 16, e) if i >= 3 else (c, e)
        divs = find_divergences(_mk(10), _mk(10, m))
        self.assertTrue(divs[0].globals_differ)
        self.assertEqual(_classify(divs, 10)[0], "global state only")

    def test_entity_count_mismatch_is_classified(self):
        def m(i, c, e):
            if i >= 4:
                e = dict(e); e.pop(2); c = b"\xff" * 16
            return c, e
        divs = find_divergences(_mk(10), _mk(10, m))
        self.assertEqual(divs[0].only_in_a, [2])
        self.assertIn("entity count", _classify(divs, 10)[0])

    def test_single_entity_reads_as_local(self):
        divs = [Divergence(5, 15, False, [7], [], []) for _ in range(3)]
        headline, guidance = _classify(divs, 100)
        self.assertIn("single entity", headline)
        self.assertIn("local", guidance)

    def test_cascade_is_distinguished_from_local(self):
        divs = [Divergence(5 + i, 15 + i, False, list(range(1 + i * 2)), [], [])
                for i in range(6)]
        self.assertIn("cascade", _classify(divs, 100)[0])

    def test_report_states_identical_when_matching(self):
        out = format_report(_mk(5), _mk(5), "a", "b", [], color=False)
        self.assertIn("IDENTICAL", out)

    def test_report_names_tick_and_next_step(self):
        def m(i, c, e):
            if i >= 6:
                e = dict(e); e[0] = b"Z" * 8; c = b"\xff" * 16
            return c, e
        a, b = _mk(20), _mk(20, m)
        out = format_report(a, b, "a", "b", find_divergences(a, b), color=False)
        self.assertIn("DIVERGED at tick 6", out)
        self.assertIn("TICK=6", out)

    def test_differing_tick_counts_reported_without_hash_divergence(self):
        out = format_report(_mk(10), _mk(5), "a", "b", [], color=False)
        self.assertIn("tick counts differ", out)


if __name__ == "__main__":
    unittest.main(verbosity=2)
