"""Verify a production-buffer-diagnostic record, and PROVE the comparison
contract bites.

WHAT THIS IS FOR. src/gfx/sl_gfx_bgtf.c writes four files per record: the actual
transform-feedback clip pool, the CPU clip pool the original path would have
submitted for the same draw, that draw's exact index sequence, and a sidecar
that declares the layout, the identity and the lengths and sha256 of the three
payloads. This reads a record and answers two different questions:

  1. IS THE RECORD SOUND? Every declared length equals the file's own length,
     every declared sha256 equals the file's own digest, the sizes agree with the
     declared element widths and counts, every index is inside the vertex count,
     no unpublished sidecar staging file sits beside it, and the sidecar's seal
     is consistent with its status.

A RECORD IS THE SIDECAR AT <base>.json AND NOTHING ELSE. The writer composes the
sidecar at <base>.json.staging and moves it to the final name only after its own
write, flush and close have all succeeded - because the sidecar states whether
the record is complete, and that statement has to be composed before its own
write can fail. A staging file therefore parses, carries matching payload digests
and may say complete: true while the runtime reported a failure. It is not a
record, it is never read as one here, and its presence beside a published record
is itself a fault.

THE CLOCKS. The gate is the draw-site tuple and nothing else. The draw site's own
sl_frames_completed() sample is recorded as draw_site_pumped and is required to
be present and numeric but is NEVER required to hold any particular value: it is
a different sample of a mutable counter from the one the screenshot line prints,
taken during the display-list walk rather than at the screenshot, and no
measurement fixes the relation between them. The screenshot's pumped frame is
observed and gated separately, from the screenshot evidence.

  2. DOES THE COMPARISON CONTRACT DETECT A DIFFERENCE? The contract is a bitwise
     comparison of all four uint32 component patterns, per index, in the index
     file's exact order, with no tolerance and no padding. A comparison that
     cannot be shown to fail is not evidence, so this applies a battery of
     single, minimal corruptions to IN-MEMORY COPIES - one ULP in each of x, y,
     z and w, two components swapped, +0.0 against -0.0, one index changed, the
     index order permuted, the index count truncated, and an unwritten tail -
     and REQUIRES each one to be reported. Nothing on disk is modified.

IT IS A VERIFIER AND A REFERENCE, NOT A PRODUCT PATH. It computes no clip
position and reimplements no selection: it reads bytes the renderer wrote and
compares them. The same comparison is what the record is for, which is why the
rule lives in the sidecar and this follows it rather than inventing one.

NOTHING HERE DECIDES ANYTHING. Equal pools would exclude differing submitted
clip bits at the recorded draw; they would not identify a raster, interpolation
or state mechanism. Unequal pools would localise a transform or output gap. The
exit status is about the record and the contract, never about a cause.

TWO SCOPES, AND A PASS IN ONE IS NOT A PASS IN THE OTHER. This tool answers the
two questions above by default, and those two questions are about BYTES: the
payload lengths and digests the sidecar declares, the index sequence's own
integrity, and whether the comparison contract can be shown to bite. A default
PASS therefore says "these payloads are the ones this sidecar describes, and a
difference in them would be reported". IT DOES NOT SAY THE RECORD CAME FROM THE
DRAW A CAPTURE WAS ARMED FOR. The identity assertions - the pool's slot,
generation, capacity and bytes_read, the readback's own outcome flags, the
selector's agreement with the draw it names, and the build provenance measured
against the image and source that actually ran - are not byte properties and
cannot be derived from the payloads, whose digests protect their contents and
say nothing about their origin.

So the scope is stated in the output rather than left to be inferred, and the
banner carries it:

  SCOPE=PAYLOAD_AND_CONTRACT_ONLY (the default)
      Payload lengths and digests, layout declarations, index integrity, clock
      STRUCTURE (the draw-site fields are uint32s, the gate tuple is built from
      them, the draw-site pumped sample is recorded, no screenshot sample is
      claimed), the publication contract, and the seal's internal consistency.
      NOT validated here, and owned elsewhere: pool slot / generation /
      capacity / bytes_read agreement, the readback outcome flags, selector
      versus draw agreement, the clock's actual VALUES, and whether the build
      digests are the ones that ran. Those belong to the live capture control's
      record gate, which binds them against values it measured itself - or to
      this tool under --strict-identity, which is the same validator shared.

  SCOPE=PAYLOAD_AND_CONTRACT_AND_STRUCTURAL_IDENTITY (--strict-identity)
      Adds strict_identity below: canonical schema and version, the pool's own
      internal agreement, the readback outcome, selector versus draw, the clock
      values, and the provenance - with the expected selector, clock, slot
      bound, draw identity and build digests bound EXTERNALLY through
      --expect-* rather than taken from the record being judged. An expectation
      that is not supplied is reported as UNBOUND by name, because a validator
      that silently skips an assertion reads exactly like one that made it.

The division exists because the two scopes have different owners. A record read
after the fact has no measured image or source digest to compare against; the
control that launched the capture does. Sharing one validator and binding its
expectations from outside keeps both readings the same code without letting the
weaker one be reported as the stronger.

Usage:
    python -I -B tools/windows/bgtfdump-verify.py --dir <record directory>
                                                 [--expect-complete]
                                                 [--expect-incomplete]
                                                 [--expect-none]
                                                 [--expect-mismatches N]
                                                 [--prove-contract]
                                                 [--strict-identity]
                                                 [--expect-selector S]
                                                 [--expect-clock R:F:C]
                                                 [--expect-draw MESH:MAT:DMODE]
                                                 [--expect-exe-sha HEX]
                                                 [--expect-src-sha HEX]
                                                 [--expect-schema-version N]
                                                 [--expect-slot-max N]
Exit 0 when every expectation held, 1 otherwise.
"""

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

CLIP_STRIDE = 16
COMPONENTS = ("x", "y", "z", "w")

U32_MAX = 0xFFFFFFFF

# The writer's own slot array bound, cited rather than guessed:
# src/gfx/sl_gfx_bgtf.c declares `#define TF_SLOTS 4` and both the capture site
# and the readback refuse a slot outside [0, TF_SLOTS). A record cannot state
# this bound - it is a property of the writer, not of the draw - so it is named
# here and may be rebound from outside with --expect-slot-max.
TF_SLOTS_DECLARED = 4

# The one canonical sidecar version this validator knows how to read. A record
# claiming another version is refused rather than read with these rules, because
# "a numeric version" is satisfied by every integer including ones whose field
# meanings this code does not implement.
SCHEMA_VERSION_CANONICAL = 1

STAGING_SUFFIX = ".json.staging"

SCOPE_PAYLOAD = "PAYLOAD_AND_CONTRACT_ONLY"
SCOPE_STRICT = "PAYLOAD_AND_CONTRACT_AND_STRUCTURAL_IDENTITY"

SCOPE_NOT_COVERED = (
    "pool slot/generation/capacity/bytes_read agreement, the readback outcome "
    "flags, selector-versus-draw agreement, the clock's actual values, and "
    "whether the build digests are the ones that ran. A payload PASS is NOT "
    "live provenance: those assertions are owned by the live capture control's "
    "record gate, which binds them against digests and clocks it measured "
    "itself, or by this tool under --strict-identity with --expect-* bound."
)


class Fail(Exception):
    pass


def _is_u32(v):
    return isinstance(v, int) and not isinstance(v, bool) and 0 <= v <= U32_MAX


def strict_identity(sidecar, names, expect=None):
    """THE STRICT STRUCTURAL IDENTITY VALIDATOR, SHARED, AND COMPARING NOTHING.

    Takes a parsed sidecar object and the directory's file NAMES - not the
    directory - so the same function serves a live control, this tool's CLI and
    a synthetic listing without any of them needing a disk. It reads no payload
    byte and performs no GPU-versus-CPU comparison: an UNEQUAL record is a valid
    record, and the comparison belongs to compare() above and to whoever asked
    for it.

    WHAT IT ADDS OVER THE PAYLOAD CHECKS. The payload digests protect contents
    and say nothing about origin, so everything here is an origin assertion: the
    publication contract, the canonical version, the pool's internal agreement
    with the draw, the readback's own outcome, the selector against the draw it
    names, and the provenance. The values that cannot come from the record being
    judged - the expected selector, clock, draw identity, slot bound and build
    digests - are bound through `expect`, and each one left unbound is reported
    as UNBOUND by name rather than passed over in silence.

    Returns (rows, summary): rows are (ok, message) pairs in check order, and
    summary carries what was bound and what was not.
    """
    expect = dict(expect or {})
    rows = []
    unbound = []

    def want(cond, msg):
        rows.append((bool(cond), msg))

    def note(msg):
        rows.append((True, msg))

    names = list(names)

    # ---- the publication contract ----------------------------------------
    # A record is the sidecar at its final name. The writer stages at
    # <base>.json.staging and moves it to <base>.json only after a successful
    # write AND flush AND close, so a staging file is either a failed
    # publication - whose contents are not a record whatever they say about
    # themselves - or two attempts in one directory.
    staged = sorted(n for n in names if n.endswith(STAGING_SUFFIX))
    want(not staged,
         "no unpublished sidecar staging file is present (%s)" % ", ".join(staged))
    final_name = expect.get("sidecar_name")
    if final_name is None:
        unbound.append("sidecar_name")
        note("the expected final sidecar name is UNBOUND (--expect-sidecar-name): "
             "the name a capture was armed for is the control's to supply")
    else:
        want(final_name in names,
             "the sidecar is present at its FINAL name (%s)" % final_name)

    # ---- the canonical version -------------------------------------------
    want(sidecar.get("schema") == "sightline.bgtf.clipdump",
         "the sidecar names the clipdump schema (%r)" % sidecar.get("schema"))
    version = sidecar.get("schema_version")
    wanted_version = expect.get("schema_version", SCHEMA_VERSION_CANONICAL)
    want(version == wanted_version,
         "the schema version is the canonical %r this validator implements (%r)"
         % (wanted_version, version))

    draw = sidecar.get("draw") or {}
    pool = sidecar.get("pool") or {}
    clock = sidecar.get("clock") or {}
    build = sidecar.get("build") or {}

    nverts = draw.get("vertices")
    nidx = draw.get("indices")
    want(_is_u32(nverts) and nverts > 0,
         "the draw declares a positive uint32 vertex count (%r)" % nverts)
    want(_is_u32(nidx) and nidx > 0,
         "and a positive uint32 index count (%r)" % nidx)
    lo, hi = draw.get("index_min"), draw.get("index_max")
    want(_is_u32(lo) and _is_u32(hi) and _is_u32(nverts)
         and 0 <= lo <= hi < nverts,
         "the declared index span is inside the vertex count (%r..%r of %r)"
         % (lo, hi, nverts))

    # ---- the pool's own identity, and its agreement with the draw ---------
    for key in ("slot", "buffer_name", "walk_generation", "slot_dmode",
                "slot_nverts", "slot_capacity_verts", "bytes_read",
                "untouched_vertices"):
        v = pool.get(key)
        want(isinstance(v, int) and not isinstance(v, bool),
             "the pool records %s as an integer (%r)" % (key, v))
    slot_max = expect.get("slot_max", TF_SLOTS_DECLARED)
    slot = pool.get("slot")
    want(isinstance(slot, int) and not isinstance(slot, bool)
         and 0 <= slot < slot_max,
         "the slot is inside the writer's declared [0, %d) bound (%r)"
         % (slot_max, slot))
    want(pool.get("slot_dmode") == draw.get("dmode"),
         "the slot's depth mode is the draw's (%r / %r)"
         % (pool.get("slot_dmode"), draw.get("dmode")))
    want(pool.get("slot_nverts") == nverts,
         "the slot's vertex count is the draw's (%r / %r)"
         % (pool.get("slot_nverts"), nverts))
    want(isinstance(pool.get("slot_capacity_verts"), int)
         and not isinstance(pool.get("slot_capacity_verts"), bool)
         and isinstance(nverts, int)
         and pool["slot_capacity_verts"] >= nverts,
         "the slot's capacity is not below the active count (%r >= %r)"
         % (pool.get("slot_capacity_verts"), nverts))
    want(isinstance(nverts, int) and not isinstance(nverts, bool)
         and pool.get("bytes_read") == nverts * CLIP_STRIDE,
         "bytes_read is exactly the ACTIVE pool, not the capacity (%r / %r)"
         % (pool.get("bytes_read"), (nverts * CLIP_STRIDE)
            if isinstance(nverts, int) and not isinstance(nverts, bool) else None))

    # ---- the readback's own outcome ---------------------------------------
    # An untouched destination, an all-zero destination and a vertex left at
    # the sentinel are each a withheld seal and never a zero-difference result,
    # so a record that carries one of them and still claims the seal is
    # contradicting itself.
    want(pool.get("readback_wrote_nothing") is False,
         "the readback wrote something (readback_wrote_nothing %r)"
         % pool.get("readback_wrote_nothing"))
    want(pool.get("gpu_all_zero") is False,
         "the destination is not all zeros (gpu_all_zero %r)"
         % pool.get("gpu_all_zero"))
    want(pool.get("untouched_vertices") == 0,
         "no vertex was left at the readback sentinel (%r)"
         % pool.get("untouched_vertices"))

    # ---- the clock VALUES, not just their shape ---------------------------
    site = (clock.get("draw_site_read"), clock.get("draw_site_dl_frame"),
            clock.get("draw_site_cloud_phase"))
    want(all(_is_u32(v) for v in site),
         "the three draw-site clocks are uint32s and not nulls (%r)" % (site,))
    want(clock.get("gate_tuple") == "%s:%s:%s" % site,
         "the gate tuple is built from the draw-site clocks (%r)"
         % clock.get("gate_tuple"))
    want(_is_u32(clock.get("draw_site_pumped")),
         "the DRAW SITE's pumped sample is recorded as a uint32 (%r)"
         % clock.get("draw_site_pumped"))
    want("screenshot_frame_pumped" not in clock,
         "and no screenshot sample is claimed, which this record never took")
    expected_clock = expect.get("clock")
    if expected_clock is None:
        unbound.append("clock")
        note("the expected draw-site tuple is UNBOUND (--expect-clock): the draw a "
             "capture was armed for is the control's to supply")
    else:
        want(tuple(site) == tuple(expected_clock),
             "the draw-site tuple is the externally bound one (%r / %r)"
             % (site, tuple(expected_clock)))
    # The pumped sample is RECORDED AND NEVER SELECTED ON, in either scope. It
    # is a second sample of a mutable counter taken during the display-list
    # walk, while the screenshot samples it later in the frame, and no
    # measurement fixes the relation between the two.
    note("the draw-site pumped sample %r is recorded and NOT gated here, in either "
         "scope: no value of it is required and none is refused"
         % clock.get("draw_site_pumped"))

    # ---- the selector against the draw it names ---------------------------
    selector = sidecar.get("selector")
    want(isinstance(selector, str) and selector,
         "a selector string is recorded (%r)" % selector)
    if isinstance(selector, str) and selector:
        parts = selector.split(":")
        want(len(parts) >= 6,
             "the selector carries at least read:frame:cloud:mesh:material:dmode "
             "(%r)" % selector)
        if len(parts) >= 6:
            against = [("read", site[0]), ("frame", site[1]), ("cloud", site[2]),
                       ("mesh", draw.get("mesh")), ("material", draw.get("material")),
                       ("dmode", draw.get("dmode"))]
            for i, (label, actual) in enumerate(against):
                field = parts[i]
                if field == "*":
                    note("the selector's %s field is the wildcard, so no agreement "
                         "with the recorded %r is required" % (label, actual))
                    continue
                try:
                    asked = int(field, 10)
                except ValueError:
                    want(False, "the selector's %s field is an integer or '*' (%r)"
                         % (label, field))
                    continue
                want(asked == actual,
                     "the selector's %s field is the one the record names (%r / %r)"
                     % (label, asked, actual))
    expected_selector = expect.get("selector")
    if expected_selector is None:
        unbound.append("selector")
        note("the expected selector is UNBOUND (--expect-selector): the value a "
             "capture armed the diagnostic with is the control's to supply")
    else:
        want(selector == expected_selector,
             "the selector is the externally bound one (%r / %r)"
             % (selector, expected_selector))

    expected_draw = expect.get("draw")
    if expected_draw is None:
        unbound.append("draw")
        note("the expected mesh/material/dmode is UNBOUND (--expect-draw)")
    else:
        want((draw.get("mesh"), draw.get("material"), draw.get("dmode"))
             == tuple(expected_draw),
             "the draw identity is the externally bound one (%r / %r)"
             % ((draw.get("mesh"), draw.get("material"), draw.get("dmode")),
                tuple(expected_draw)))

    # ---- the GL readings, both of them ------------------------------------
    gl = sidecar.get("gl") or {}
    want(gl.get("readbacks") == 1,
         "exactly one readback is recorded (%r)" % gl.get("readbacks"))
    want(gl.get("glfinish") is False, "no glFinish was used (%r)" % gl.get("glfinish"))
    want(gl.get("queries") is False,
         "no query campaign was used (%r)" % gl.get("queries"))
    before = gl.get("error_before_readback")
    after = gl.get("error_after_readback")
    want(isinstance(before, int) and not isinstance(before, bool),
         "the pre-existing GL error state is recorded (%r)" % before)
    # -1 IS "NOT OBSERVABLE" AND NOT SUCCESS. glGetError is called directly as
    # the core export rather than resolved through the extension loader, which
    # answers NULL for core functions on some drivers, so a -1 in either field
    # is a defect rather than a clean reading.
    want(before != -1,
         "the GL error state was OBSERVABLE before the readback (%r)" % before)
    want(after == 0,
         "and no error was raised across the readback (%r)" % after)
    if isinstance(before, int) and not isinstance(before, bool) and before > 0:
        note("a non-zero reading BEFORE the readback (%r) belongs to whoever raised "
             "it and is recorded, never attributed to the readback" % before)

    # ---- the provenance ---------------------------------------------------
    for key, expect_key in (("exe_sha256", "exe_sha256"), ("src_sha256", "src_sha256")):
        v = build.get(key, "")
        want(isinstance(v, str) and len(v) == 64
             and all(c in "0123456789abcdef" for c in v) and set(v) != {"0"},
             "the build's %s is 64 lowercase hex and not all zeros (%r)" % (key, v))
        wanted = expect.get(expect_key)
        if wanted is None:
            unbound.append(expect_key)
            note("the expected %s is UNBOUND (--expect-%s): only the launcher that "
                 "measured the image and the source can bind it, so syntax is all "
                 "this reading proves" % (key, expect_key.replace("_sha256", "-sha")))
        else:
            want(v == wanted,
                 "the build's %s is the externally bound digest (%s)" % (key, v))

    # ---- the seal, last ---------------------------------------------------
    want(sidecar.get("write_failure") is None,
         "no write failure is recorded (%r)" % sidecar.get("write_failure"))
    want(isinstance(sidecar.get("complete"), bool),
         "completeness is a boolean (%r)" % sidecar.get("complete"))
    want((sidecar.get("status") == "complete") == bool(sidecar.get("complete")),
         "the status and the seal agree (%r / %r)"
         % (sidecar.get("status"), sidecar.get("complete")))

    summary = {
        "scope": SCOPE_STRICT,
        "checks": len(rows),
        "failed": [m for ok_, m in rows if not ok_],
        "externally_bound": sorted(k for k in ("sidecar_name", "clock", "selector",
                                               "draw", "exe_sha256", "src_sha256",
                                               "schema_version", "slot_max")
                                   if k in expect),
        "unbound": sorted(set(unbound)),
        "compared_the_pools": False,
        "rule": "origin assertions only. No payload byte is read here and no "
                "GPU-versus-CPU comparison is performed: an UNEQUAL record is a "
                "valid record, and the comparison is a separate question with a "
                "separate owner.",
    }
    return rows, summary


def u32s(blob):
    """Every little-endian uint32 of a blob, as a tuple."""
    if len(blob) % 4:
        raise Fail("payload length %d is not a whole number of uint32" % len(blob))
    return struct.unpack("<%dI" % (len(blob) // 4), blob)


def compare(gpu, cpu, indices, nverts):
    """THE CONTRACT, exactly as the sidecar states it.

    All four uint32 component patterns of each indexed vertex, in the index
    sequence's own order, compared bit for bit. No tolerance, no normalisation,
    no padding; +0.0 and -0.0 are different patterns because they are different
    bits. Returns the list of (position in the index sequence, vertex, component
    name, gpu pattern, cpu pattern).

    A STRUCTURAL PROBLEM IS AN ERROR AND NOT A MISMATCH COUNT: unequal pool
    lengths, a length that is not a whole number of vertices, or an index past
    the vertex count mean the record cannot be compared at all, which is a
    different answer from "it compares unequal".
    """
    if len(gpu) != len(cpu):
        raise Fail("the two pools are %d and %d bytes: a record whose pools are "
                   "different lengths cannot be compared" % (len(gpu), len(cpu)))
    if len(gpu) % CLIP_STRIDE:
        raise Fail("a pool of %d bytes is not a whole number of %d-byte vertices"
                   % (len(gpu), CLIP_STRIDE))
    if len(gpu) // CLIP_STRIDE != nverts:
        raise Fail("a pool of %d bytes holds %d vertices, not the declared %d"
                   % (len(gpu), len(gpu) // CLIP_STRIDE, nverts))
    out = []
    for pos, v in enumerate(indices):
        if v >= nverts:
            raise Fail("index %d at position %d is past the vertex count %d"
                       % (v, pos, nverts))
        base = v * CLIP_STRIDE
        g = struct.unpack("<4I", gpu[base:base + CLIP_STRIDE])
        c = struct.unpack("<4I", cpu[base:base + CLIP_STRIDE])
        for k in range(4):
            if g[k] != c[k]:
                out.append((pos, v, COMPONENTS[k], g[k], c[k]))
    return out


def bump(blob, vertex, component, delta=1):
    """One ULP away in one component of one vertex, as a new bytes object. A ULP
    is an increment of the stored pattern, which is exactly what makes it the
    smallest representable difference and the hardest case for a comparison that
    rounds anywhere."""
    out = bytearray(blob)
    off = vertex * CLIP_STRIDE + component * 4
    v = struct.unpack_from("<I", out, off)[0]
    struct.pack_into("<I", out, off, (v + delta) & 0xFFFFFFFF)
    return bytes(out)


def swap(blob, vertex, a, b):
    out = bytearray(blob)
    base = vertex * CLIP_STRIDE
    pa, pb = base + a * 4, base + b * 4
    va = bytes(out[pa:pa + 4])
    out[pa:pa + 4] = out[pb:pb + 4]
    out[pb:pb + 4] = va
    return bytes(out)


def set_pattern(blob, vertex, component, pattern):
    out = bytearray(blob)
    struct.pack_into("<I", out, vertex * CLIP_STRIDE + component * 4, pattern)
    return bytes(out)


def check_record(sidecar_path, args, say):
    sidecar = json.loads(sidecar_path.read_text(encoding="utf-8"))
    base = sidecar_path.parent
    problems = []

    def want(cond, msg):
        if cond:
            say("    ok   %s" % msg)
        else:
            problems.append(msg)
            say("    FAIL %s" % msg)

    # NO UNPUBLISHED STAGING FILE MAY SIT BESIDE A RECORD. The writer composes
    # the sidecar at <base>.json.staging and moves it to <base>.json only after
    # its own write, flush and close all succeeded, so a published record never
    # leaves one behind. One present means either a failed publication - in
    # which case its contents are not a record whatever they say about
    # themselves - or two capture attempts in one directory.
    staged = sorted(base.glob("tfclip-*.json.staging"))
    want(not staged,
         "no unpublished sidecar staging file is present (%s)"
         % ", ".join(p.name for p in staged))

    want(sidecar.get("schema") == "sightline.bgtf.clipdump",
         "the sidecar names the clipdump schema (%r)" % sidecar.get("schema"))
    want(isinstance(sidecar.get("schema_version"), int),
         "and a numeric schema version (%r)" % sidecar.get("schema_version"))

    layout = sidecar.get("layout", {})
    want(layout.get("clip_element") == "binary32",
         "the clip element width is declared as binary32 (%r)"
         % layout.get("clip_element"))
    want(layout.get("clip_components") == 4 and layout.get("clip_stride_bytes") == 16,
         "four components at a 16-byte stride (%r, %r)"
         % (layout.get("clip_components"), layout.get("clip_stride_bytes")))
    want(layout.get("clip_padding_bytes") == 0, "with no padding")
    want(layout.get("index_element") == "uint32"
         and layout.get("index_stride_bytes") == 4,
         "indices are uint32 (%r)" % layout.get("index_element"))
    want(layout.get("endianness") == "little",
         "and the byte order is declared (%r)" % layout.get("endianness"))

    files = sidecar.get("files", {})
    blobs = {}
    for key in ("gpuclip", "cpuclip", "indices"):
        entry = files.get(key)
        if not isinstance(entry, dict):
            problems.append("the sidecar has no %s entry" % key)
            say("    FAIL the sidecar has no %s entry" % key)
            continue
        path = base / entry["name"]
        if not path.is_file():
            problems.append("%s is missing: %s" % (key, path))
            say("    FAIL %s is missing: %s" % (key, path))
            continue
        blob = path.read_bytes()
        blobs[key] = blob
        want(len(blob) == entry.get("bytes"),
             "%s is %d bytes, as declared (%r)" % (key, len(blob), entry.get("bytes")))
        digest = hashlib.sha256(blob).hexdigest()
        want(digest == entry.get("sha256"),
             "%s's sha256 matches the sidecar (%s)" % (key, digest))

    build = sidecar.get("build", {})
    for key in ("exe_sha256", "src_sha256"):
        v = build.get(key, "")
        want(isinstance(v, str) and len(v) == 64
             and all(c in "0123456789abcdef" for c in v) and set(v) != {"0"},
             "the build's %s is 64 lowercase hex and not all zeros" % key)

    clock = sidecar.get("clock", {})
    gate = "%s:%s:%s" % (clock.get("draw_site_read"), clock.get("draw_site_dl_frame"),
                         clock.get("draw_site_cloud_phase"))
    want(clock.get("gate_tuple") == gate,
         "the gate tuple is built from the DRAW-SITE clocks (%r == %r)"
         % (clock.get("gate_tuple"), gate))
    # THE THREE GATE CLOCKS MUST BE NUMBERS, not nulls that stringify into a
    # tuple built from the same nulls: "None:None:None" equals itself, so the
    # agreement check above passes on a record with no draw identity at all.
    for key in ("draw_site_read", "draw_site_dl_frame", "draw_site_cloud_phase"):
        v = clock.get(key)
        want(isinstance(v, int) and not isinstance(v, bool) and 0 <= v <= 0xFFFFFFFF,
             "%s is a uint32 and not null (%r)" % (key, v))
    # AND THE DRAW SITE'S PUMPED SAMPLE IS RECORDED, AS A NUMBER, AND NOT GATED.
    # It is a different sample of a mutable counter from the one the screenshot
    # line prints - taken during the display-list walk rather than at the
    # screenshot - so no value of it is required or refused here. What is
    # required is that it is present, numeric, and named for the site it came
    # from, so a reader cannot mistake it for the screenshot's own reading.
    pumped = clock.get("draw_site_pumped")
    want(isinstance(pumped, int) and not isinstance(pumped, bool)
         and 0 <= pumped <= 0xFFFFFFFF,
         "the DRAW SITE's pumped sample is recorded as a uint32 (%r)" % pumped)
    want("screenshot_frame_pumped" not in clock,
         "and the record claims no screenshot sample, which it never took")
    say("    clocks: draw-site read %r, dl frame %r, cloud %r; draw-site pumped %r "
        "(recorded, not gated)"
        % (clock.get("draw_site_read"), clock.get("draw_site_dl_frame"),
           clock.get("draw_site_cloud_phase"), pumped))

    # ---- the strict structural identity validator, when it was asked for ---
    # Opt-in, and its absence is announced rather than left to be inferred: the
    # default scope ends at the payloads and the contract, and an identity
    # assertion nobody ran must not be readable as one that passed.
    if args.strict_identity:
        expect = {}
        if args.expect_selector is not None:
            expect["selector"] = args.expect_selector
        if args.expect_clock is not None:
            expect["clock"] = tuple(int(x, 10) for x in args.expect_clock.split(":"))
        if args.expect_draw is not None:
            expect["draw"] = tuple(int(x, 10) for x in args.expect_draw.split(":"))
        if args.expect_exe_sha is not None:
            expect["exe_sha256"] = args.expect_exe_sha
        if args.expect_src_sha is not None:
            expect["src_sha256"] = args.expect_src_sha
        if args.expect_schema_version is not None:
            expect["schema_version"] = args.expect_schema_version
        if args.expect_slot_max is not None:
            expect["slot_max"] = args.expect_slot_max
        if args.expect_sidecar_name is not None:
            expect["sidecar_name"] = args.expect_sidecar_name
        names = sorted(p.name for p in base.iterdir() if p.is_file())
        rows, summary = strict_identity(sidecar, names, expect)
        say("    --- STRICT STRUCTURAL IDENTITY (%d checks) ---" % summary["checks"])
        for ok_, msg in rows:
            if ok_:
                say("    ok   %s" % msg)
            else:
                problems.append(msg)
                say("    FAIL %s" % msg)
        say("    strict identity: externally bound %s; UNBOUND %s"
            % (summary["externally_bound"] or "nothing",
               summary["unbound"] or "nothing"))
    else:
        say("    (the strict structural identity validator was NOT requested: this "
            "reading does not cover %s)" % SCOPE_NOT_COVERED)

    status = sidecar.get("status")
    complete = sidecar.get("complete")
    say("    status %r, complete %r" % (status, complete))
    want(isinstance(complete, bool), "completeness is a boolean")
    want((status == "complete") == bool(complete),
         "the status and the seal agree (%r / %r)" % (status, complete))

    if args.expect_complete:
        want(complete is True, "the record is sealed COMPLETE as expected")
    if args.expect_incomplete:
        want(complete is False,
             "the record's SEAL IS WITHHELD as expected, with its own named reason "
             "(%r) - it is not a zero-difference result" % status)
        want(isinstance(status, str) and status != "complete" and ":" in status,
             "and the reason is a named outcome rather than a bare failure (%r)"
             % status)

    draw = sidecar.get("draw", {})
    nverts = draw.get("vertices")
    want(isinstance(nverts, int) and nverts > 0,
         "a positive vertex count is declared (%r)" % nverts)

    if len(blobs) != 3 or not isinstance(nverts, int):
        return problems

    indices = u32s(blobs["indices"])
    want(len(indices) == draw.get("indices"),
         "the index file holds the declared count (%d / %r)"
         % (len(indices), draw.get("indices")))
    want(indices and min(indices) == draw.get("index_min")
         and max(indices) == draw.get("index_max"),
         "and the declared index span is the sequence's own (%r..%r)"
         % (draw.get("index_min"), draw.get("index_max")))

    # ---- 1. the record as it stands --------------------------------------
    try:
        found = compare(blobs["gpuclip"], blobs["cpuclip"], indices, nverts)
    except Fail as exc:
        problems.append("the record could not be compared: %s" % exc)
        say("    FAIL the record could not be compared: %s" % exc)
        return problems
    baseline = len(found)
    say("    compared %d indices, %d of %d component patterns differ"
        % (len(indices), baseline, len(indices) * 4))
    for pos, v, comp, g, c in found[:8]:
        say("      index position %d, vertex %d, %s: gpu 0x%08x cpu 0x%08x"
            % (pos, v, comp, g, c))
    if args.expect_mismatches is not None:
        want(len(found) == args.expect_mismatches,
             "the record shows exactly %d component mismatches (%d)"
             % (args.expect_mismatches, len(found)))

    # The called-out vertices, reported whenever the record holds them, because
    # they are the ones the investigation asked about.
    called = sidecar.get("compare", {}).get("called_out_vertices", [])
    for v in called:
        if isinstance(v, int) and v < nverts:
            base_off = v * CLIP_STRIDE
            g = struct.unpack("<4I", blobs["gpuclip"][base_off:base_off + 16])
            c = struct.unpack("<4I", blobs["cpuclip"][base_off:base_off + 16])
            say("      called-out vertex %d: gpu %s cpu %s"
                % (v, " ".join("0x%08x" % x for x in g),
                   " ".join("0x%08x" % x for x in c)))
        else:
            say("      called-out vertex %r is outside this record's %d vertices"
                % (v, nverts))

    # ---- 2. the index sequence's own integrity ---------------------------
    # Order and count are NOT recoverable from the clip pools, so they are bound
    # by the sidecar's length and digest instead. A permutation, a changed index
    # and a truncation each have to change one of those, and this requires it.
    entry = files["indices"]
    permuted = struct.pack("<%dI" % len(indices), *reversed(indices))
    truncated = blobs["indices"][:-4]
    changed = bytearray(blobs["indices"])
    struct.pack_into("<I", changed, 0, (indices[0] + 1) % nverts)
    for name, blob in (("a reversed index order", permuted),
                       ("a truncated index count", truncated),
                       ("one changed index", bytes(changed))):
        same_len = len(blob) == entry.get("bytes")
        same_sha = hashlib.sha256(blob).hexdigest() == entry.get("sha256")
        want(not (same_len and same_sha),
             "%s fails the sidecar's length or digest" % name)

    # ---- 3. the contract has to BITE -------------------------------------
    # Every mutation below is the smallest one of its kind, applied to a copy,
    # and each MUST be reported. A contract that cannot be shown to fail proves
    # nothing about a record that compares equal.
    #
    # IT IS ONLY MEANINGFUL ON A RECORD THAT COMPARES EQUAL. The test is "one
    # more differing pattern than before", so on a record where the components
    # under test ALREADY differ - an incomplete one, whose pool was never fully
    # written - a single new difference cannot raise the count and the battery
    # would report its own arithmetic as a failure of the contract. So it runs
    # only when asked, and only on a baseline of zero.
    if not args.prove_contract:
        say("    (the corruption battery was not requested for this record)")
        return problems
    if baseline != 0:
        problems.append("the corruption battery needs a record that compares equal, "
                        "and this one already differs in %d patterns" % baseline)
        say("    FAIL the corruption battery needs a baseline of 0, not %d" % baseline)
        return problems
    victim = indices[0] if indices else 0
    battery = []
    for k, name in enumerate(COMPONENTS):
        battery.append(("one ULP up in %s of vertex %d" % (name, victim),
                        bump(blobs["gpuclip"], victim, k), blobs["cpuclip"], indices))
        battery.append(("one ULP down in %s of vertex %d" % (name, victim),
                        bump(blobs["gpuclip"], victim, k, -1), blobs["cpuclip"],
                        indices))
    battery.append(("x and y swapped in vertex %d" % victim,
                    swap(blobs["gpuclip"], victim, 0, 1), blobs["cpuclip"], indices))
    battery.append(("z and w swapped in vertex %d" % victim,
                    swap(blobs["gpuclip"], victim, 2, 3), blobs["cpuclip"], indices))
    # SIGNED ZERO. +0.0 and -0.0 compare EQUAL as floats and differ as bits, so a
    # comparison that went through float equality anywhere would miss this one.
    battery.append(("+0.0 against -0.0 in x of vertex %d" % victim,
                    set_pattern(blobs["gpuclip"], victim, 0, 0x00000000),
                    set_pattern(blobs["cpuclip"], victim, 0, 0x80000000), indices))
    # AN UNWRITTEN TAIL: the last vertex of the GPU pool left at the writer's own
    # sentinel, which is what a short readback leaves behind.
    tail = bytearray(blobs["gpuclip"])
    tail[(nverts - 1) * CLIP_STRIDE:nverts * CLIP_STRIDE] = b"\xa5" * CLIP_STRIDE
    battery.append(("the last vertex left at the readback sentinel",
                    bytes(tail), blobs["cpuclip"],
                    tuple(list(indices) + [nverts - 1])))
    # A CHANGED INDEX, which makes the comparison read a different vertex of each
    # pool and so changes the answer even though both pools are untouched.
    if nverts > 1:
        other = (victim + 1) % nverts
        moved = list(indices)
        moved[0] = other
        battery.append(("one index changed from %d to %d" % (victim, other),
                        bump(blobs["gpuclip"], other, 0), blobs["cpuclip"],
                        tuple(moved)))

    detected = 0
    for name, g, c, idx in battery:
        try:
            got = compare(g, c, idx, nverts)
        except Fail as exc:
            say("    ok   DETECTED (structurally): %s - %s" % (name, exc))
            detected += 1
            continue
        if len(got) > baseline:
            say("    ok   DETECTED: %s (%d differing patterns, baseline %d)"
                % (name, len(got), baseline))
            detected += 1
        else:
            problems.append("UNDETECTED by the comparison contract: %s" % name)
            say("    FAIL UNDETECTED: %s (%d differing patterns, baseline %d)"
                % (name, len(got), baseline))
    want(detected == len(battery),
         "every one of the %d minimal corruptions was detected (%d)"
         % (len(battery), detected))
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--expect-complete", action="store_true")
    ap.add_argument("--expect-incomplete", action="store_true",
                    help="require a record whose seal is WITHHELD, with a named reason")
    ap.add_argument("--expect-none", action="store_true",
                    help="require that the directory holds NO record at all")
    ap.add_argument("--expect-mismatches", type=int, default=None)
    ap.add_argument("--prove-contract", action="store_true",
                    help="require the comparison contract to report every minimal "
                         "corruption; only meaningful on a record that compares equal")
    # THE STRICT SCOPE, OPT-IN, WITH ITS EXPECTATIONS BOUND FROM OUTSIDE. A
    # record cannot supply the values it is being judged against; whoever armed
    # the capture can.
    ap.add_argument("--strict-identity", action="store_true",
                    help="also run the shared strict structural identity validator: "
                         "canonical version, pool agreement, readback outcome, "
                         "selector versus draw, clock values and provenance")
    ap.add_argument("--expect-selector", default=None)
    ap.add_argument("--expect-clock", default=None,
                    help="the expected draw-site tuple as READ:FRAME:CLOUD")
    ap.add_argument("--expect-draw", default=None,
                    help="the expected MESH:MATERIAL:DMODE")
    ap.add_argument("--expect-exe-sha", default=None)
    ap.add_argument("--expect-src-sha", default=None)
    ap.add_argument("--expect-schema-version", type=int, default=None)
    ap.add_argument("--expect-slot-max", type=int, default=None)
    ap.add_argument("--expect-sidecar-name", default=None)
    args = ap.parse_args()

    scope = SCOPE_STRICT if args.strict_identity else SCOPE_PAYLOAD

    out = []

    def say(line):
        out.append(line)

    directory = Path(args.dir)
    sidecars = sorted(directory.glob("tfclip-*.json")) if directory.is_dir() else []
    say("bgtfdump-verify: %s" % directory)
    say("  SCOPE=%s" % scope)
    if scope == SCOPE_PAYLOAD:
        say("  this reading covers the payloads, the layout declarations, the index "
            "integrity, the clock STRUCTURE, the publication contract and the seal's "
            "internal consistency. It is NOT live provenance and does NOT cover %s"
            % SCOPE_NOT_COVERED)
    else:
        say("  this reading additionally covers the strict structural identity "
            "assertions. Each expectation not supplied through --expect-* is "
            "reported as UNBOUND by name below.")
    say("  %d sidecar(s)" % len(sidecars))

    rc = 0
    if args.expect_none:
        if sidecars or (directory.is_dir() and any(directory.glob("tfclip-*"))):
            say("  FAIL a record exists where none was expected")
            rc = 1
        else:
            say("  ok   no record exists, as expected")
    elif not sidecars:
        say("  FAIL no sidecar found, so there is nothing to verify")
        rc = 1

    for sc in sidecars:
        say("  record %s" % sc.name)
        try:
            problems = check_record(sc, args, say)
        except Exception as exc:                       # noqa: BLE001
            say("    FAIL the sidecar could not be read: %r" % exc)
            problems = ["unreadable sidecar"]
        if problems:
            rc = 1

    print("\n".join(out))
    # THE BANNER CARRIES ITS SCOPE. A bare PASS on this tool has been read as a
    # statement about where a record came from, which the default scope cannot
    # make, so the scope is part of the verdict rather than a line above it.
    print("bgtfdump-verify: %s  [SCOPE=%s%s]"
          % ("PASS" if rc == 0 else "FAIL", scope,
             "" if args.strict_identity else " - PAYLOAD ONLY, NOT LIVE PROVENANCE"))
    return rc


if __name__ == "__main__":
    sys.exit(main())
