"""trace-diff: localise a divergence, don't just report one.

A harness that says "FAILED" is worthless. The question this has to answer is
"which subsystem, and from when", without further instrumentation. So the diff
reports the first divergent tick, which entities diverged there, and how the
divergence behaves afterwards - a single entity that stays wrong points at that
entity's logic, while a cascade across all entities usually means shared state
(RNG cursor, alarm flags, allocator) moved earlier than the visible symptom.
"""

from __future__ import annotations

from dataclasses import dataclass

BOLD, DIM, RED, YEL, GRN, RST = "\033[1m", "\033[2m", "\033[31m", "\033[33m", "\033[32m", "\033[0m"


@dataclass
class Divergence:
    tick: int
    frame_counter: int
    globals_differ: bool
    entities: list[int]
    only_in_a: list[int]
    only_in_b: list[int]


def _entity_delta(ta, tb):
    a, b = ta.entities, tb.entities
    common = sorted(set(a) & set(b))
    return ([i for i in common if a[i] != b[i]],
            sorted(set(a) - set(b)), sorted(set(b) - set(a)))


def _keyed(ticks):
    """Index ticks by (frame_counter, nth-occurrence-of-that-counter).

    Frame counters are not unique: boot writes garbage to the counter before it
    is initialised, and the real counter later climbs through those same values.
    Keying on the counter alone collapses duplicates and compares one run's
    first occurrence against the other's second - which manufactures
    divergences at frames whose contents are identical.
    """
    seen, out = {}, {}
    for t in ticks:
        n = seen.get(t.frame_counter, 0)
        seen[t.frame_counter] = n + 1
        out[(t.frame_counter, n)] = t
    return out


def find_divergences(trace_a, trace_b, limit: int = 200) -> list[Divergence]:
    """Align on the GAME's clock, not on sample order.

    The sampler records a tick each time currentFrameCounter changes, but the
    counter can advance by more than one between VI callbacks, so a sample is
    occasionally skipped. Sequential tick indices then drift apart and tick N in
    one run is a different game moment to tick N in another - which the diff
    reported as divergence, complete with plausible-looking position and RNG
    differences. Comparing only ticks that share a frame counter makes a skipped
    sample harmless instead of catastrophic.
    """
    # Frame counters are NOT unique: boot writes garbage values to the counter
    # before it is initialised, and the real counter later climbs through those
    # same numbers. Keying a dict on the counter alone collapses the duplicates
    # and compares one run's first occurrence against the other's second, which
    # manufactures divergences at frames whose contents are in fact identical.
    # Match the Nth occurrence of a counter with the Nth occurrence in the other.
    by_b = _keyed(trace_b.ticks)
    seen_a = {}
    out = []
    for ta in trace_a.ticks:
        n = seen_a.get(ta.frame_counter, 0)
        seen_a[ta.frame_counter] = n + 1
        tb = by_b.get((ta.frame_counter, n))
        if tb is None:
            continue                  # not sampled in B; nothing to compare
        if ta.composite == tb.composite:
            continue
        ents, only_a, only_b = _entity_delta(ta, tb)
        # Globals are implicated when the composite moved but no entity did.
        out.append(Divergence(ta.tick, ta.frame_counter,
                              not ents and not only_a and not only_b,
                              ents, only_a, only_b))
        if len(out) >= limit:
            break
    return out


def _classify(divs: list[Divergence], total_ticks: int) -> tuple[str, str]:
    """Return (headline, guidance) describing the divergence's shape."""
    first = divs[0]
    if first.only_in_a or first.only_in_b:
        return ("entity count differs",
                "One run has entities the other does not. Look at spawning, "
                "despawning, or the allocator - not at per-entity logic.")
    if first.globals_differ:
        return ("global state only",
                "No entity hash moved, so this is RNG cursor, alarm/objective "
                "flags, or the tick counter itself. Check anything that consumes "
                "randomGetNext() a variable number of times.")

    widths = [len(d.entities) for d in divs[:40]]
    if len(first.entities) == 1 and max(widths) <= 2:
        return (f"single entity ({first.entities[0]})",
                "Divergence stays local, which points at that entity's own logic "
                "- AI state, pathing, or animation - rather than shared state.")
    if len(first.entities) == 1 and max(widths) > 2:
        return (f"cascade from entity {first.entities[0]}",
                "Started in one entity and spread. The first tick is the real "
                "lead; later entities are almost certainly downstream effects.")
    return (f"{len(first.entities)} entities at once",
            "A broad first divergence usually means shared state moved - RNG "
            "cursor, a global timer, or the allocator - even though entities "
            "are what visibly differ.")


def format_report(trace_a, trace_b, name_a: str, name_b: str,
                  divs: list[Divergence], color: bool = True) -> str:
    def c(code, s):
        return f"{code}{s}{RST}" if color else str(s)

    L = []
    ta, tb = trace_a.header, trace_b.header
    L.append(c(BOLD, "trace-diff"))
    L.append(f"  A: {name_a}  ({len(trace_a.ticks)} ticks)")
    L.append(f"  B: {name_b}  ({len(trace_b.ticks)} ticks)")
    L.append(f"  level={ta.level}  schema=v{ta.schema_version}  rom={ta.rom_sha1[:12]}")
    L.append("")

    fa = set(_keyed(trace_a.ticks))
    fb = set(_keyed(trace_b.ticks))
    shared = len(fa & fb)
    # Aligning on frame counter compares only the overlap, so a run that ended
    # early would otherwise report as IDENTICAL on the part that does overlap.
    # Coverage has to be part of the verdict, not a footnote.
    unmatched_a, unmatched_b = len(fa - fb), len(fb - fa)

    if not divs and not unmatched_a and not unmatched_b and shared:
        L.append(c(GRN, f"  IDENTICAL - {shared} ticks match "
                        f"(compared on frame counter)"))
        return "\n".join(L)

    if not divs and (unmatched_a or unmatched_b):
        L.append(c(YEL, "  no state divergence, but tick counts differ"))
        L.append(f"  {shared} ticks compared and matched.")
        if unmatched_a:
            L.append(f"  {unmatched_a} tick(s) only in A - B ended early or "
                     f"skipped samples.")
        if unmatched_b:
            L.append(f"  {unmatched_b} tick(s) only in B - A ended early or "
                     f"skipped samples.")
        L.append("  A run that stopped short is not a pass. Check for a crash "
                 "or an exhausted input stream.")
        return "\n".join(L)

    if not divs:
        L.append(c(YEL, "  no hash divergence, but tick counts differ"))
        L.append(f"  A ran {len(trace_a.ticks)} ticks, B ran {len(trace_b.ticks)}.")
        L.append("  One run ended early - check for a crash or a stalled game.")
        return "\n".join(L)

    first = divs[0]
    headline, guidance = _classify(divs, len(trace_a.ticks))

    L.append(c(RED, f"  DIVERGED at tick {first.tick} "
                    f"(currentFrameCounter={first.frame_counter})"))
    L.append(f"  {first.tick} ticks matched before this point.")
    L.append("")
    L.append(f"  {c(BOLD, 'what moved:')} {headline}")

    if first.globals_differ:
        L.append(f"    globals   {c(RED, 'differ')}  "
                 f"(rng cursor / alarm / objective / slot count)")
    if first.entities:
        shown = first.entities[:12]
        more = "" if len(first.entities) <= 12 else f"  (+{len(first.entities)-12} more)"
        L.append(f"    entities  {c(RED, ', '.join(str(i) for i in shown))}{more}")
    if first.only_in_a:
        L.append(f"    only in A {c(YEL, ', '.join(str(i) for i in first.only_in_a[:12]))}")
    if first.only_in_b:
        L.append(f"    only in B {c(YEL, ', '.join(str(i) for i in first.only_in_b[:12]))}")

    L.append("")
    L.append(f"  {c(BOLD, 'reading:')} {guidance}")

    # How the divergence evolves tells you lead vs downstream.
    L.append("")
    L.append(f"  {c(BOLD, 'spread over the next ticks:')}")
    for d in divs[:6]:
        ents = ",".join(str(i) for i in d.entities[:8]) or ("globals" if d.globals_differ else "-")
        L.append(f"    tick {d.tick:<7} fc={d.frame_counter:<7} entities: {ents}")
    if len(divs) > 6:
        L.append(f"    {c(DIM, f'... {len(divs)-6} more divergent ticks')}")

    L.append("")
    L.append(f"  {c(BOLD, 'next:')} re-run with field detail at the lead tick -")
    L.append(f"    make trace-diff LEVEL={ta.level} TICK={first.tick} DETAIL=1")
    L.append(f"  {c(DIM, 'Replay is deterministic, so full field values can always')}")
    L.append(f"  {c(DIM, 'be recovered by re-running; traces stay small on purpose.')}")
    return "\n".join(L)
