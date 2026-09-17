# Docs-first, enforced rather than remembered

## The problem this solves

The notes (`kholdfuzion/goldeneye_docs`) were cloned, indexed, and named in a
standing note that said *check the docs before inferring structure*. They were
still skipped four times in a single session (2026-08-24), at a cost of several
hours and two published-then-retracted root causes.

So the gap was never availability, and it was never knowledge. It is that
**consulting the docs is a decision**, and it has to be made at exactly the
moment momentum is strongest — when a hypothesis about a format has just formed
and reading more source feels like progress. Under that momentum the decision
does not get made.

Adding another note saying "remember to check the docs" would have been a fifth
copy of a message already ignored four times.

## What it cost, concretely

The collision-DL crash. Two root causes were published to the backlog and
retracted:

1. *"`rodata->Vertices` is an unpromoted file offset."* Measured: already a
   native pointer. The proposed fix would have broken a working path.
2. *"`rwdata->gdl` is unpromoted."* It is — but by design, as a sentinel the
   game compares against to detect "nobody replaced this". Promoting at init
   would have silently defeated that comparison.

The actual answer — the opcode is read from byte 0 of a display list that has
already been rewritten to native word order, so it is at byte 3 — took one
lookup in `ucode05.txt` once the file was finally opened, prompted by the
project owner rather than by anything in the system.

## The design

Four layers, ordered by how little they depend on anyone remembering anything.

**1. Routing table** — `docs/doc-routing.json`. Subsystem to authoritative
files, plus which source files each area governs, plus a `not_covered` list of
questions the corpus has been searched for and found silent on. This is the
asset; the rest is delivery. It turns "which of 496 notes covers this?" from a
judgement into a lookup.

**2. CLI** — `tools/docs/gedocs.py`. `for <src>` / `topic <area>` /
`search <terms>` / `show <file> <pattern>`, over a cached index that rebuilds
when the corpus changes. Stdlib only. `--json` on every subcommand, so an MCP
server can wrap it without reimplementing anything. Every successful lookup
stamps a marker.

**3. Hooks** — local editor/agent hooks, configured per machine and not part
of the tracked tree. The enforcing layer, because hooks run whether or not
anyone decides anything.
- A session-start hook puts the routing and the rule in context at turn one.
- A pre-edit hook on writes under `src/` prints the governing notes
  before the edit, and reports how long ago a lookup actually happened — so the
  reminder is specific ("no doc lookup recorded") rather than ambient.

**4. Project rules, non-negotiable #7** (`docs/project-rules.md`) — the rule
itself, sitting with the trace-harness and no-assets rules.

## Choices worth defending

**Hooks over MCP.** An MCP server makes lookup structured and available, but
availability was never the gap: `grep` and an index were already there. MCP
tools still require choosing to call them, which is the exact failure. Hooks
fire unbidden. The CLI is built MCP-ready regardless, so a server is a thin
wrapper if one is wanted later.

**Inject, don't block.** The `Edit`/`Write` hook prints pointers; it does not
refuse the edit. A hook that blocks trivial changes gets disabled, and a
disabled hook protects nothing. The escalation is informational instead: it
states plainly when no lookup has been recorded.

**Hook Edit/Write, not Read/Grep.** Source is read constantly; a hook firing
fifty times a turn becomes wallpaper — which is what this replaces. Edit and
Write are the point of no return, where an assumption becomes committed code.

**Exclude Rand++ from the index.** Those 121 files document a ROM hack's
modified formats. Indexing them invites citing a hack as authority for the base
game. 496 notes are indexed, not 617.

## Verification

Not "it should work" — measured at build time:

- The session-start hook prints the rule, 13 areas and 4 known gaps.
- The pre-edit hook on `src/game/propobj.c` prints all three governing areas with
  the display-list lesson attached, plus the lookup age.
- Un-governed paths (`tools/trace/foo.py`) produce no output.
- Malformed JSON and empty stdin both exit 0 — the hook cannot break the
  toolchain.

## The part that compounds

The routing table is only as good as what gets fed back into it. Every answered
question adds a mapping; every dead-end search adds a `not_covered` entry. Today
seeded 13 areas and 4 gaps. A mapping is a one-line edit, and it is the
difference between this decaying into another ignored note and it getting
sharper each time it is used.
