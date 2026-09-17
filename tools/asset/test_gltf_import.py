#!/usr/bin/env python3
"""Synthetic tests for the asset-override importer and the SLM1 format.

NO ROM-DERIVED FIXTURES. Every input is generated here - a triangle, a pair of
triangles, a 2x2 PNG - so the suite runs on any machine with no ROM, no assets
and no network. Run it with:

    .venv\\Scripts\\python.exe tools\\asset\\test_gltf_import.py

The C loader's validation is mirrored here as check_slm1(), which reads the
file back with the same bounds tests src/native/sl_asset_override.c performs.
That is what lets the malformed cases below assert a REJECTION rather than
merely "the importer did not crash".
"""

from __future__ import annotations

import base64
import json
import struct
import sys
import tempfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import gltf_import as gi  # noqa: E402

# The repository root, from this file's own location - never a literal path.
REPO = Path(__file__).resolve().parents[2]

FAILURES: list[str] = []
PASSES = 0


def check(name: str, cond: bool, detail: str = "") -> None:
    global PASSES
    if cond:
        PASSES += 1
        print("  PASS  %s" % name)
    else:
        FAILURES.append(name)
        print("  FAIL  %s %s" % (name, detail))


# ---------------------------------------------------------------- fixtures --


def png(w: int, h: int, rgba: bytes) -> bytes:
    """A minimal, valid, non-interlaced 8-bit RGBA PNG."""
    raw = b"".join(b"\x00" + rgba[y * w * 4 : (y + 1) * w * 4] for y in range(h))

    def chunk(tag: bytes, body: bytes) -> bytes:
        return (
            struct.pack(">I", len(body))
            + tag
            + body
            + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)
        )

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw))
        + chunk(b"IEND", b"")
    )


def data_uri(b: bytes) -> str:
    return "data:application/octet-stream;base64," + base64.b64encode(b).decode()


def build_gltf(prims, nodes=None, extra=None, images=None, materials=None):
    """Assemble a glTF 2.0 document from python lists, packing every accessor
    into one embedded buffer."""
    buf = bytearray()
    accessors: list[dict] = []
    views: list[dict] = []

    def add(values, ctype, atype):
        fmt, size = gi.COMPONENT[ctype]
        n = gi.NCOMP[atype]
        while len(buf) % 4:
            buf.append(0)
        off = len(buf)
        for v in values:
            buf.extend(struct.pack("<" + fmt * n, *(v if isinstance(v, (list, tuple)) else (v,))))
        views.append({"buffer": 0, "byteOffset": off, "byteLength": len(buf) - off})
        accessors.append(
            {"bufferView": len(views) - 1, "componentType": ctype, "count": len(values),
             "type": atype}
        )
        return len(accessors) - 1

    meshes = []
    for p in prims:
        attrs = {}
        for key, (vals, ctype, atype) in p["attributes"].items():
            attrs[key] = add(vals, ctype, atype)
        prim = {"attributes": attrs, "mode": p.get("mode", 4)}
        if "indices" in p:
            prim["indices"] = add(p["indices"], 5125, "SCALAR")
        if "material" in p:
            prim["material"] = p["material"]
        meshes.append({"primitives": [prim]})

    doc = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes or meshes)))}],
        "nodes": nodes if nodes is not None else [{"mesh": i} for i in range(len(meshes))],
        "meshes": meshes,
        "accessors": accessors,
        "bufferViews": views,
        "materials": materials if materials is not None else [{}],
    }
    if images:
        # image bytes go into their own bufferView at the end of the buffer
        img_views = []
        for raw in images:
            while len(buf) % 4:
                buf.append(0)
            off = len(buf)
            buf.extend(raw)
            views.append({"buffer": 0, "byteOffset": off, "byteLength": len(raw)})
            img_views.append(len(views) - 1)
        doc["images"] = [{"bufferView": v, "mimeType": "image/png"} for v in img_views]
        doc["textures"] = [{"source": i} for i in range(len(images))]
    doc["buffers"] = [{"byteLength": len(buf), "uri": data_uri(bytes(buf))}]
    if extra:
        doc.update(extra)
    return doc


TRI_POS = [(0.0, 0.0, 0.0), (10.0, 0.0, 0.0), (0.0, 20.0, 0.0)]
TRI_NRM = [(0.0, 0.0, 1.0)] * 3
TRI_UV = [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)]
TRI_COL = [(1.0, 0.5, 0.25, 1.0)] * 3
TRI_IDX = [0, 1, 2]


def simple_triangle(**kw):
    attrs = {"POSITION": (TRI_POS, 5126, "VEC3")}
    if kw.get("normals"):
        attrs["NORMAL"] = (TRI_NRM, 5126, "VEC3")
    if kw.get("uv"):
        attrs["TEXCOORD_0"] = (TRI_UV, 5126, "VEC2")
    if kw.get("colour"):
        attrs["COLOR_0"] = (TRI_COL, 5126, "VEC4")
    return {"attributes": attrs, "indices": TRI_IDX, "material": 0}


def convert_doc(doc, **kw):
    """Run the importer over an in-memory document, via a temp .gltf."""
    with tempfile.TemporaryDirectory() as td:
        p = Path(td) / "m.gltf"
        p.write_text(json.dumps(doc), encoding="utf-8")
        return gi.convert(gi.Gltf(p), verbose=False, **kw)


# --------------------------------------------------- the C loader, mirrored --


def check_slm1(blob: bytes) -> dict:
    """Read an SLM1 file back applying exactly the checks the C loader
    applies. Raises ValueError on anything the game would reject."""
    if len(blob) < gi.HEADER_SIZE or len(blob) > gi.MAX_FILE:
        raise ValueError("file length outside 128..MAX_FILE")
    if blob[:4] != gi.MAGIC:
        raise ValueError("bad magic")
    (ver, hdr, flags, nvert, nidx, nprim, nmat, ntex) = struct.unpack_from("<8I", blob, 4)[:8]
    if ver != gi.VERSION:
        raise ValueError("bad version %d" % ver)
    if hdr != gi.HEADER_SIZE:
        raise ValueError("bad header size")
    offs = struct.unpack_from("<9I", blob, 36)
    (o_pos, o_nrm, o_uv, o_col, o_idx, o_prim, o_mat, o_tex, declared) = offs
    if declared != len(blob):
        raise ValueError("declared size %d != actual %d" % (declared, len(blob)))
    if not (0 < nvert <= gi.MAX_VERTS):
        raise ValueError("vertex count out of range")
    if not (0 < nidx <= gi.MAX_INDICES) or nidx % 3:
        raise ValueError("index count out of range")
    if not (0 < nprim <= gi.MAX_PRIMS):
        raise ValueError("prim count out of range")
    if not (0 < nmat <= gi.MAX_MATS):
        raise ValueError("mat count out of range")
    if ntex > gi.MAX_TEXS:
        raise ValueError("tex count out of range")

    def span(off, ln, what):
        if off > len(blob) or ln > len(blob) - off:
            raise ValueError("%s does not fit the file" % what)

    span(o_pos, nvert * 12, "positions")
    if flags & gi.F_NORMALS:
        span(o_nrm, nvert * 12, "normals")
    if flags & gi.F_UV:
        span(o_uv, nvert * 8, "uvs")
    if flags & gi.F_COLOR:
        span(o_col, nvert * 4, "colours")
    span(o_idx, nidx * 4, "indices")
    span(o_prim, nprim * 16, "prims")
    span(o_mat, nmat * 32, "mats")
    if ntex:
        span(o_tex, ntex * 32, "tex table")

    idx = struct.unpack_from("<%dI" % nidx, blob, o_idx)
    for v in idx:
        if v >= nvert:
            raise ValueError("index %d outside %d vertices" % (v, nvert))

    texs = []
    total = 0
    for i in range(ntex):
        (w, h, off, ln, kind, noff, nlen,
         thash) = struct.unpack_from("<8I", blob, o_tex + i * 32)
        if not (0 < w <= gi.MAX_TEXDIM) or not (0 < h <= gi.MAX_TEXDIM):
            raise ValueError("texture dimension out of range")
        total += w * h * 4
        if total > gi.MAX_TEXBYTES:
            raise ValueError("textures over the byte limit")
        if kind == gi.TEX_EMBEDDED:
            if w * h * 4 != ln:
                raise ValueError("texture length does not match dimensions")
            span(off, ln, "texture pixels")
            if noff or nlen:
                raise ValueError("an embedded texture also names an identifier")
            texs.append({"w": w, "h": h, "kind": kind,
                         "rgba": blob[off : off + ln], "name": None,
                         "hash": thash})
        elif kind == gi.TEX_REF:
            if off or ln:
                raise ValueError("a texture reference also carries pixel data")
            if not (0 < nlen <= gi.MAX_TEXNAME):
                raise ValueError("texture identifier length out of range")
            span(noff, nlen + 1, "texture identifier")
            if blob[noff + nlen] != 0:
                raise ValueError("texture identifier is not NUL-terminated")
            nm = blob[noff : noff + nlen].decode("ascii", "replace")
            for c in nm:
                if not (c.islower() or c.isdigit() or c in "._"):
                    raise ValueError("texture identifier has a character that "
                                     "is not allowed: %r" % c)
            texs.append({"w": w, "h": h, "kind": kind, "rgba": b"",
                         "name": nm, "hash": thash})
        else:
            raise ValueError("texture slot has an unknown kind %d" % kind)

    mats = []
    for i in range(nmat):
        r, g, b, a, t, mf, cut, _ = struct.unpack_from("<ffffiIfI", blob, o_mat + i * 32)
        if t < -1 or (t >= 0 and t >= ntex):
            raise ValueError("material names a texture that does not exist")
        mats.append({"base": (r, g, b, a), "texture": t, "flags": mf, "cutoff": cut})

    prims = []
    for i in range(nprim):
        first, count, mi, _ = struct.unpack_from("<IIII", blob, o_prim + i * 16)
        if count == 0 or count % 3 or first > nidx or count > nidx - first:
            raise ValueError("primitive index range outside the index array")
        if mi >= nmat:
            raise ValueError("primitive names a material that does not exist")
        prims.append({"first": first, "count": count, "material": mi})

    out = {
        "flags": flags, "nvert": nvert, "nidx": nidx, "idx": idx,
        "prims": prims, "mats": mats, "tex": texs,
        "pos": struct.unpack_from("<%df" % (nvert * 3), blob, o_pos),
    }
    if flags & gi.F_NORMALS:
        out["nrm"] = struct.unpack_from("<%df" % (nvert * 3), blob, o_nrm)
    if flags & gi.F_UV:
        out["uv"] = struct.unpack_from("<%df" % (nvert * 2), blob, o_uv)
    if flags & gi.F_COLOR:
        out["col"] = blob[o_col : o_col + nvert * 4]
    return out


def expect_reject(name: str, fn) -> None:
    try:
        fn()
    except (gi.ImportError_, ValueError) as e:
        check(name, True)
        print("        rejected: %s" % str(e).split("\n")[0][:110])
        return
    except Exception as e:  # noqa: BLE001
        check(name, False, "raised the wrong kind of error: %r" % e)
        return
    check(name, False, "was ACCEPTED and should not have been")


# ============================================================== the tests ===


def t_minimal():
    print("\n[1] a synthetic glTF imports at all")
    blob = convert_doc(build_gltf([simple_triangle()]))
    m = check_slm1(blob)
    check("one triangle round-trips", m["nvert"] == 3 and m["nidx"] == 3)
    check("positions survive exactly",
          [round(v, 4) for v in m["pos"]] == [c for p in TRI_POS for c in p],
          str(m["pos"]))
    check("no attribute flags when only POSITION is supplied", m["flags"] == 0,
          hex(m["flags"]))


def t_attributes():
    print("\n[2] position / normal / uv / colour round-trip")
    blob = convert_doc(build_gltf([simple_triangle(normals=True, uv=True, colour=True)]))
    m = check_slm1(blob)
    check("all three attribute flags set",
          m["flags"] == (gi.F_NORMALS | gi.F_UV | gi.F_COLOR), hex(m["flags"]))
    check("normals survive", [round(v, 4) for v in m["nrm"]] == [c for p in TRI_NRM for c in p])
    check("uvs survive", [round(v, 4) for v in m["uv"]] == [c for p in TRI_UV for c in p])
    check("colour survives as RGBA8", tuple(m["col"][0:4]) == (255, 128, 64, 255),
          str(tuple(m["col"][0:4])))
    check("baseColorFactor is folded into COLOR_0, material goes white",
          m["mats"][0]["base"] == (1.0, 1.0, 1.0, 1.0))


def t_colour_fold():
    print("\n[3] baseColorFactor x COLOR_0 is folded exactly once")
    mats = [{"pbrMetallicRoughness": {"baseColorFactor": [0.5, 1.0, 1.0, 1.0]}}]
    blob = convert_doc(build_gltf([simple_triangle(colour=True)], materials=mats))
    m = check_slm1(blob)
    # COLOR_0 red is 1.0, factor red is 0.5 -> 128 (round(0.5*255)=128)
    check("red = COLOR_0 * baseColorFactor", m["col"][0] == 128, str(m["col"][0]))
    check("green untouched", m["col"][1] == 128, str(m["col"][1]))


def t_two_primitives():
    print("\n[4] two primitives with two materials survive")
    mats = [
        {"pbrMetallicRoughness": {"baseColorFactor": [1.0, 0.0, 0.0, 1.0]},
         "doubleSided": True},
        {"pbrMetallicRoughness": {"baseColorFactor": [0.0, 0.0, 1.0, 0.5]},
         "alphaMode": "BLEND"},
    ]
    p0 = simple_triangle(normals=True)
    p1 = simple_triangle(normals=True)
    p1["material"] = 1
    blob = convert_doc(build_gltf([p0, p1], materials=mats))
    m = check_slm1(blob)
    check("two primitives", len(m["prims"]) == 2, str(len(m["prims"])))
    check("disjoint, contiguous index ranges",
          m["prims"][0] == {"first": 0, "count": 3, "material": 0}
          and m["prims"][1]["first"] == 3 and m["prims"][1]["count"] == 3,
          str(m["prims"]))
    check("vertices are concatenated, not shared", m["nvert"] == 6, str(m["nvert"]))
    check("second primitive's indices were rebased", m["idx"][3:] == (3, 4, 5),
          str(m["idx"]))
    a = m["mats"][m["prims"][0]["material"]]
    b = m["mats"][m["prims"][1]["material"]]
    check("material colours survive", a["base"][:3] == (1.0, 0.0, 0.0)
          and b["base"][:3] == (0.0, 0.0, 1.0), str((a["base"], b["base"])))
    check("doubleSided survives", a["flags"] & gi.M_DOUBLESIDED != 0, hex(a["flags"]))
    check("alphaMode BLEND survives", b["flags"] & gi.M_ALPHA_BLEND != 0, hex(b["flags"]))


def t_alpha_mask():
    print("\n[5] alphaMode MASK and its cutoff survive")
    mats = [{"alphaMode": "MASK", "alphaCutoff": 0.25}]
    blob = convert_doc(build_gltf([simple_triangle()], materials=mats))
    m = check_slm1(blob)
    check("MASK flag", m["mats"][0]["flags"] & gi.M_ALPHA_MASK != 0)
    check("cutoff", abs(m["mats"][0]["cutoff"] - 0.25) < 1e-6, str(m["mats"][0]["cutoff"]))


def t_texture():
    print("\n[6] an embedded texture is converted to RGBA8 and survives")
    px = bytes([255, 0, 0, 255,  0, 255, 0, 255,
                0, 0, 255, 255,  9, 9, 9, 128])
    mats = [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}]
    blob = convert_doc(build_gltf([simple_triangle(uv=True)], materials=mats,
                                  images=[png(2, 2, px)]))
    m = check_slm1(blob)
    check("one texture", len(m["tex"]) == 1, str(len(m["tex"])))
    check("dimensions", m["tex"][0]["w"] == 2 and m["tex"][0]["h"] == 2)
    check("pixels survive the PNG round-trip", bytes(m["tex"][0]["rgba"]) == px,
          bytes(m["tex"][0]["rgba"]).hex())
    check("material points at it", m["mats"][0]["texture"] == 0)


def t_png_filters():
    print("\n[7] the PNG decoder handles all five row filters")
    # A 4x3 image encoded once per filter type; every one must decode alike.
    w, h = 4, 3
    px = bytes((x * 40 + y * 7) % 256 for y in range(h) for x in range(w) for _ in range(4))
    ok = True
    for ftype in range(5):
        rows = bytearray()
        prev = bytearray(w * 4)
        for y in range(h):
            row = bytearray(px[y * w * 4 : (y + 1) * w * 4])
            enc = bytearray(len(row))
            for i in range(len(row)):
                a = row[i - 4] if i >= 4 else 0
                b = prev[i]
                c = prev[i - 4] if i >= 4 else 0
                if ftype == 0:
                    pr = 0
                elif ftype == 1:
                    pr = a
                elif ftype == 2:
                    pr = b
                elif ftype == 3:
                    pr = (a + b) >> 1
                else:
                    p = a + b - c
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                enc[i] = (row[i] - pr) & 0xFF
            rows.append(ftype)
            rows.extend(enc)
            prev = row

        def chunk(tag, body):
            return (struct.pack(">I", len(body)) + tag + body
                    + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

        blob = (b"\x89PNG\r\n\x1a\n"
                + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(bytes(rows)))
                + chunk(b"IEND", b""))
        gw, gh, got = gi.png_decode(blob)
        if (gw, gh) != (w, h) or bytes(got) != px:
            ok = False
            print("        filter %d decoded wrong" % ftype)
    check("filters 0..4 all decode to the same image", ok)


def t_node_transform():
    print("\n[8] node transforms are baked into the mesh")
    nodes = [{"mesh": 0, "translation": [100.0, 0.0, 0.0], "scale": [2.0, 2.0, 2.0]}]
    blob = convert_doc(build_gltf([simple_triangle(normals=True)], nodes=nodes))
    m = check_slm1(blob)
    # (10,0,0) scaled by 2 then translated by +100 -> (120,0,0)
    check("scale then translate applied in glTF order",
          [round(v, 3) for v in m["pos"][3:6]] == [120.0, 0.0, 0.0],
          str(m["pos"][0:9]))
    check("first vertex moved to the node origin",
          [round(v, 3) for v in m["pos"][0:3]] == [100.0, 0.0, 0.0])
    check("normals stay unit length after a uniform scale",
          abs(sum(c * c for c in m["nrm"][0:3]) - 1.0) < 1e-5, str(m["nrm"][0:3]))


def t_reject_malformed_index():
    print("\n[9] a malformed index is rejected")
    p = simple_triangle()
    p["indices"] = [0, 1, 7]          # only three vertices exist
    expect_reject("index past the vertex count",
                  lambda: convert_doc(build_gltf([p])))


def t_reject_truncated():
    print("\n[10] a truncated native model is rejected")
    blob = convert_doc(build_gltf([simple_triangle(normals=True, uv=True)]))
    expect_reject("truncated file", lambda: check_slm1(blob[: len(blob) // 2]))
    expect_reject("header only", lambda: check_slm1(blob[:gi.HEADER_SIZE]))
    expect_reject("shorter than a header", lambda: check_slm1(blob[:64]))


def t_reject_bad_version():
    print("\n[11] a bad format version is rejected")
    blob = bytearray(convert_doc(build_gltf([simple_triangle()])))
    struct.pack_into("<I", blob, 4, gi.VERSION + 1)
    expect_reject("version + 1", lambda: check_slm1(bytes(blob)))
    struct.pack_into("<I", blob, 4, gi.VERSION)
    blob[0] = ord("X")
    expect_reject("bad magic", lambda: check_slm1(bytes(blob)))


def t_reject_excessive_count():
    print("\n[12] an excessive count is rejected")
    good = convert_doc(build_gltf([simple_triangle()]))

    def poke(off, value):
        b = bytearray(good)
        struct.pack_into("<I", b, off, value)
        return bytes(b)

    expect_reject("vertex count over the cap",
                  lambda: check_slm1(poke(16, gi.MAX_VERTS + 1)))
    expect_reject("index count over the cap",
                  lambda: check_slm1(poke(20, gi.MAX_INDICES + 1)))
    expect_reject("primitive count over the cap",
                  lambda: check_slm1(poke(24, gi.MAX_PRIMS + 1)))
    expect_reject("material count over the cap",
                  lambda: check_slm1(poke(28, gi.MAX_MATS + 1)))
    expect_reject("texture count over the cap",
                  lambda: check_slm1(poke(32, gi.MAX_TEXS + 1)))
    expect_reject("a count that overflows its array span",
                  lambda: check_slm1(poke(16, gi.MAX_VERTS)))
    expect_reject("a primitive index range past the index array",
                  lambda: check_slm1(poke(20, 300)))


def t_reject_unsupported_gltf():
    print("\n[13] unsupported glTF features are rejected, not mis-rendered")
    expect_reject("animation tracks", lambda: convert_doc(build_gltf(
        [simple_triangle()], extra={"animations": [{"channels": [], "samplers": []}]})))
    expect_reject("skins", lambda: convert_doc(build_gltf(
        [simple_triangle()], extra={"skins": [{"joints": [0]}]})))
    expect_reject("an unknown required extension", lambda: convert_doc(build_gltf(
        [simple_triangle()], extra={"extensionsRequired": ["KHR_draco_mesh_compression"]})))
    p = simple_triangle()
    p["mode"] = 1
    expect_reject("a non-triangle primitive mode", lambda: convert_doc(build_gltf([p])))
    expect_reject("a normal map", lambda: convert_doc(build_gltf(
        [simple_triangle(uv=True)], materials=[{"normalTexture": {"index": 0}}])))
    expect_reject("a metallicRoughness texture", lambda: convert_doc(build_gltf(
        [simple_triangle(uv=True)],
        materials=[{"pbrMetallicRoughness": {"metallicRoughnessTexture": {"index": 0}}}])))
    expect_reject("morph targets", lambda: convert_doc(build_gltf(
        [dict(simple_triangle(), **{"attributes": simple_triangle()["attributes"]})],
        extra={"meshes": None}) if False else _morph_doc()))
    expect_reject("a JPEG texture", lambda: _jpeg_doc())
    expect_reject("an interlaced PNG", lambda: gi.png_decode(_interlaced_png()))


def _morph_doc():
    doc = build_gltf([simple_triangle()])
    doc["meshes"][0]["primitives"][0]["targets"] = [{"POSITION": 0}]
    return convert_doc(doc)


def _jpeg_doc():
    doc = build_gltf([simple_triangle(uv=True)],
                     materials=[{"pbrMetallicRoughness":
                                 {"baseColorTexture": {"index": 0}}}],
                     images=[b"\xff\xd8\xff\xe0" + b"\x00" * 60])
    return convert_doc(doc)


def _interlaced_png():
    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 6, 0, 0, 1))
            + chunk(b"IDAT", zlib.compress(b"\x00" * 32))
            + chunk(b"IEND", b""))


def t_paths_agree():
    print("\n[14] the id -> path mapping matches the C loader's")
    c = (Path(__file__).resolve().parents[2] / "src" / "sl_asset_override.h")
    loader = (Path(__file__).resolve().parents[2] / "src" / "native"
              / "sl_asset_override.c").read_text(encoding="utf-8")
    ok = True
    for asset, rel in gi.ASSET_IDS.items():
        if '"%s"' % rel not in loader:
            ok = False
            print("        %s: C loader has no %r" % (asset, rel))
        if '"%s"' % asset not in loader:
            ok = False
            print("        %s: C loader has no id name for it" % asset)
    check("every id's relative path appears verbatim in the C loader", ok)
    hdr = c.read_text(encoding="utf-8")
    lims = [("SL_AMDL_MAX_VERTS", gi.MAX_VERTS), ("SL_AMDL_MAX_INDICES", gi.MAX_INDICES),
            ("SL_AMDL_MAX_PRIMS", gi.MAX_PRIMS), ("SL_AMDL_MAX_MATS", gi.MAX_MATS),
            ("SL_AMDL_MAX_TEXS", gi.MAX_TEXS), ("SL_AMDL_MAX_TEXDIM", gi.MAX_TEXDIM),
            ("SL_AMDL_MAX_TEXBYTES", gi.MAX_TEXBYTES), ("SL_AMDL_MAX_FILE", gi.MAX_FILE)]
    bad = [n for n, v in lims if ("#define %s" % n) not in hdr or ("%du" % v) not in hdr]
    check("every importer limit matches the header's", not bad, str(bad))
    check("format version matches", ("#define SL_AMDL_VERSION     %uu" % gi.VERSION) in hdr)
    check("header size matches", ("#define SL_AMDL_HEADER_SIZE %uu" % gi.HEADER_SIZE) in hdr)


def t_glb():
    print("\n[15] a GLB container reads the same as a .gltf")
    doc = build_gltf([simple_triangle(normals=True)])
    buf = base64.b64decode(doc["buffers"][0]["uri"].split(",", 1)[1])
    doc["buffers"][0] = {"byteLength": len(buf)}
    js = json.dumps(doc).encode("utf-8")
    js += b" " * ((-len(js)) % 4)
    bn = buf + b"\0" * ((-len(buf)) % 4)
    glb = (b"glTF" + struct.pack("<II", 2, 12 + 8 + len(js) + 8 + len(bn))
           + struct.pack("<II", len(js), 0x4E4F534A) + js
           + struct.pack("<II", len(bn), 0x004E4942) + bn)
    with tempfile.TemporaryDirectory() as td:
        p = Path(td) / "m.glb"
        p.write_bytes(glb)
        m = check_slm1(gi.convert(gi.Gltf(p), verbose=False))
    check("GLB gives the same geometry",
          m["nvert"] == 3 and [round(v, 3) for v in m["pos"][3:6]] == [10.0, 0.0, 0.0],
          str(m["pos"]))


def t_material_texture_pairing():
    print("\n[16] each material keeps ITS OWN texture through import")
    # REGRESSION GUARD, written against a real investigation (2026-09-05). A
    # two-primitive model whose two materials trade textures renders with its
    # colour regions swapped - "the colours are backwards" - and nothing else
    # about it looks wrong, so the fault is invisible unless the pairing is
    # asserted directly. Two textures with DISJOINT channel signatures make a
    # swap unmistakable: gold (high R, low B) against blue (low R, high B).
    gold = bytes([239, 206, 49, 255]) * 4
    blue = bytes([0, 16, 82, 255]) * 4
    mats = [
        {"name": "gold", "pbrMetallicRoughness":
            {"baseColorFactor": [1.0, 1.0, 1.0, 1.0],
             "baseColorTexture": {"index": 0}}},
        {"name": "blue", "pbrMetallicRoughness":
            {"baseColorFactor": [0.5, 0.25, 0.5, 1.0],
             "baseColorTexture": {"index": 1}}},
    ]
    p0 = simple_triangle(uv=True)
    p1 = simple_triangle(uv=True)
    p1["material"] = 1
    m = check_slm1(convert_doc(build_gltf([p0, p1], materials=mats,
                                          images=[png(2, 2, gold), png(2, 2, blue)])))
    check("two textures survive", len(m["tex"]) == 2, str(len(m["tex"])))
    t0 = m["mats"][m["prims"][0]["material"]]["texture"]
    t1 = m["mats"][m["prims"][1]["material"]]["texture"]
    check("primitive 0 keeps the gold texture, not the blue one",
          bytes(m["tex"][t0]["rgba"][0:4]) == bytes([239, 206, 49, 255]),
          "prim0 -> tex %d = %s" % (t0, list(m["tex"][t0]["rgba"][0:4])))
    check("primitive 1 keeps the blue texture, not the gold one",
          bytes(m["tex"][t1]["rgba"][0:4]) == bytes([0, 16, 82, 255]),
          "prim1 -> tex %d = %s" % (t1, list(m["tex"][t1]["rgba"][0:4])))
    check("the two primitives do NOT share one texture", t0 != t1,
          "both resolved to %d" % t0)
    check("each material keeps its own baseColorFactor",
          m["mats"][m["prims"][0]["material"]]["base"][:3] == (1.0, 1.0, 1.0)
          and m["mats"][m["prims"][1]["material"]]["base"][:3] == (0.5, 0.25, 0.5),
          str([m["mats"][i]["base"] for i in range(2)]))


def t_channel_order():
    print("\n[17] texture channel order is R,G,B,A end to end")
    # The other half of the same investigation. A red/blue exchange anywhere
    # between the PNG decoder and the installed file inverts every colour along
    # that axis, and a symmetric test texel would never show it. This texel has
    # four distinct, non-symmetric channel values, so ANY permutation fails.
    texel = bytes([1, 2, 3, 4])
    px = texel * 4
    mats = [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}]
    m = check_slm1(convert_doc(build_gltf([simple_triangle(uv=True)],
                                          materials=mats, images=[png(2, 2, px)])))
    got = bytes(m["tex"][0]["rgba"][0:4])
    check("R,G,B,A survive in that order (no BGRA, no permutation)",
          got == texel, "expected %s, got %s" % (list(texel), list(got)))
    # And the decoder agrees with the encoder on a full asymmetric image.
    w, h = 4, 2
    full = bytes(((x * 17 + y * 3 + c * 61) % 251) + 1
                 for y in range(h) for x in range(w) for c in range(4))
    gw, gh, back = gi.png_decode(png(w, h, full))
    check("a fully asymmetric image round-trips byte for byte",
          (gw, gh) == (w, h) and bytes(back) == full)




# =================================================== game-texture references ==
#
# The fixtures here are SYNTHETIC, including the "game" texture itself: the
# registry is swapped for a made-up entry over a 2x2 image invented in this
# file. That is not a convenience - it is the point. The mechanism under test
# is "a texture whose content matches the registry is refused and referenced",
# and that is exercised identically by a made-up entry. Using a real game
# texture would put ROM-derived bytes in the test suite to prove a rule whose
# entire purpose is keeping them out.


GAME_TEX_W, GAME_TEX_H = 2, 2
GAME_TEX_PX = bytes([11, 22, 33, 255,   44, 55, 66, 255,
                     77, 88, 99, 255,  100, 110, 120, 255])
GAME_TEX_ID = "test.synthetic_map"


def synthetic_registry():
    return {GAME_TEX_ID: {
        "w": GAME_TEX_W, "h": GAME_TEX_H,
        "sha256": gi.tex_digest(GAME_TEX_W, GAME_TEX_H, GAME_TEX_PX),
        "fnv1a": gi.fnv1a(GAME_TEX_PX),
        "source": "synthetic, invented by the test suite",
    }}


class fake_registry:
    """Swap the game-texture registry for the duration of a block."""

    def __init__(self, table):
        self.table = table

    def __enter__(self):
        self.saved = gi._GAME_TEX_CACHE
        gi._GAME_TEX_CACHE = self.table
        return self

    def __exit__(self, *a):
        gi._GAME_TEX_CACHE = self.saved
        return False


TEXMAT = [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}]


def t_texref_recognised():
    print("\n[18] a texture the registry recognises is REFERENCED, never embedded")
    with fake_registry(synthetic_registry()):
        blob = convert_doc(build_gltf([simple_triangle(uv=True)],
                                      materials=TEXMAT,
                                      images=[png(GAME_TEX_W, GAME_TEX_H,
                                                  GAME_TEX_PX)]))
    m = check_slm1(blob)
    t = m["tex"][0]
    check("the slot is a reference", t["kind"] == gi.TEX_REF, str(t["kind"]))
    check("it names the identifier", t["name"] == GAME_TEX_ID, str(t["name"]))
    check("it carries no pixels", t["rgba"] == b"")
    check("it carries the registry's FNV-1a", t["hash"] == gi.fnv1a(GAME_TEX_PX),
          "%08x" % t["hash"])
    check("the dimensions come through", (t["w"], t["h"]) == (GAME_TEX_W, GAME_TEX_H))
    check("the material still points at the slot", m["mats"][0]["texture"] == 0)
    # The load-bearing one: the pixels are not anywhere in the file.
    check("the recognised pixels appear NOWHERE in the written file",
          GAME_TEX_PX not in blob)


def t_texref_declared():
    print("\n[19] --texture-ref declares a game texture the content check misses")
    # Same image, one byte different, so the digest does NOT match.
    edited = bytearray(GAME_TEX_PX)
    edited[0] ^= 0x40
    with fake_registry(synthetic_registry()):
        blob = convert_doc(
            build_gltf([simple_triangle(uv=True)], materials=TEXMAT,
                       images=[png(GAME_TEX_W, GAME_TEX_H, bytes(edited))]),
            texture_refs={"image 0": GAME_TEX_ID})
    m = check_slm1(blob)
    t = m["tex"][0]
    check("the declared slot is a reference", t["kind"] == gi.TEX_REF)
    check("it names the identifier", t["name"] == GAME_TEX_ID)
    check("the author's edited pixels are not in the file",
          bytes(edited) not in blob)
    check("the hash is the GAME's, not the author's edit",
          t["hash"] == gi.fnv1a(GAME_TEX_PX), "%08x" % t["hash"])


def t_texref_repo_safe():
    print("\n[20] a repository model may contain no pixels at all")
    # An ordinary texture, nothing the registry knows.
    plain = bytes([1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255])
    with fake_registry(synthetic_registry()):
        expect_reject(
            "--repo REFUSES a model that would embed a texture",
            lambda: convert_doc(build_gltf([simple_triangle(uv=True)],
                                           materials=TEXMAT,
                                           images=[png(2, 2, plain)]),
                                repo_safe=True))
        blob = convert_doc(build_gltf([simple_triangle(uv=True)],
                                      materials=TEXMAT,
                                      images=[png(GAME_TEX_W, GAME_TEX_H,
                                                  GAME_TEX_PX)]),
                           repo_safe=True)
        m = check_slm1(blob)
        check("--repo ACCEPTS a model whose every texture is a reference",
              all(t["kind"] == gi.TEX_REF for t in m["tex"]))
        # A model with no textures at all is also repository-safe.
        blob2 = convert_doc(build_gltf([simple_triangle()]), repo_safe=True)
        check("--repo ACCEPTS a model with no textures",
              check_slm1(blob2)["nvert"] == 3)


def t_texref_declaration_errors():
    print("\n[21] bad --texture-ref declarations are refused, with a reason")
    plain = bytes([1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255])
    with fake_registry(synthetic_registry()):
        expect_reject(
            "an identifier no game texture has is refused",
            lambda: convert_doc(build_gltf([simple_triangle(uv=True)],
                                           materials=TEXMAT,
                                           images=[png(2, 2, plain)]),
                                texture_refs={"image 0": "nope.not_a_texture"}))
        expect_reject(
            "a declaration matching no image in the model is refused",
            lambda: convert_doc(build_gltf([simple_triangle(uv=True)],
                                           materials=TEXMAT,
                                           images=[png(2, 2, plain)]),
                                texture_refs={"absent.png": GAME_TEX_ID}))


def t_texref_malformed_slots():
    print("\n[22] a malformed reference slot is rejected by the loader's rules")
    with fake_registry(synthetic_registry()):
        good = convert_doc(build_gltf([simple_triangle(uv=True)],
                                      materials=TEXMAT,
                                      images=[png(GAME_TEX_W, GAME_TEX_H,
                                                  GAME_TEX_PX)]))
    check_slm1(good)                      # the baseline really is valid
    o_tex = struct.unpack_from("<I", good, 64)[0]

    def poke(offset, value):
        b = bytearray(good)
        struct.pack_into("<I", b, o_tex + offset, value)
        return bytes(b)

    expect_reject("a reference that also carries pixels",
                  lambda: check_slm1(poke(8, 128)))
    expect_reject("a reference with a zero-length identifier",
                  lambda: check_slm1(poke(24, 0)))
    expect_reject("a reference whose identifier runs past the file",
                  lambda: check_slm1(poke(20, len(good) + 4096)))
    expect_reject("a texture slot with an unknown kind",
                  lambda: check_slm1(poke(16, 7)))

    def bad_char():
        b = bytearray(good)
        noff = struct.unpack_from("<I", good, o_tex + 20)[0]
        b[noff] = 0x2F               # '/', not in the allowed set
        return check_slm1(bytes(b))

    expect_reject("an identifier containing a character that is not allowed",
                  bad_char)

    def unterminated():
        b = bytearray(good)
        noff, nlen = struct.unpack_from("<II", good, o_tex + 20)
        b[noff + nlen] = 0x61        # overwrite the NUL
        return check_slm1(bytes(b))

    expect_reject("an identifier that is not NUL-terminated", unterminated)


def t_texref_unknown_identifier_falls_back():
    print("\n[23] an identifier the runtime does not know is a FALLBACK, not a crash")
    # A well-formed file naming a texture no build resolves. The loader accepts
    # the FILE - it is not malformed - and sl_texref_resolve returns 0, which
    # makes sl_asset_override_available() report false and the ORIGINAL asset
    # draw. That split is deliberate: file validity and data availability are
    # different questions, and only the first can be answered offline.
    with fake_registry(synthetic_registry()):
        good = convert_doc(build_gltf([simple_triangle(uv=True)],
                                      materials=TEXMAT,
                                      images=[png(GAME_TEX_W, GAME_TEX_H,
                                                  GAME_TEX_PX)]))
    o_tex = struct.unpack_from("<I", good, 64)[0]
    noff, nlen = struct.unpack_from("<II", good, o_tex + 20)
    b = bytearray(good)
    b[noff : noff + nlen] = b"zz." + b"z" * (nlen - 3)
    try:
        m = check_slm1(bytes(b))
        check("the file itself is still structurally valid",
              m["tex"][0]["name"].startswith("zz."))
    except ValueError as e:
        check("the file itself is still structurally valid", False, str(e))
    src = (REPO / "src" / "native" / "sl_texref.c").read_text(encoding="utf-8")
    check("the resolver reports an unknown identifier rather than assuming one",
          "no game texture has that identifier" in src)
    check("a failed resolve falls back rather than drawing untextured",
          "drawing the original" in
          (REPO / "src" / "native" / "sl_asset_override.c").read_text(
              encoding="utf-8"))


def t_texref_tables_agree():
    print("\n[24] the importer's registry and the runtime's table name the same textures")
    src = (REPO / "src" / "native" / "sl_texref.c").read_text(encoding="utf-8")
    names = sorted(gi.game_textures())
    check("the importer knows at least the three documented identifiers",
          set(names) >= {"rareware.env_field", "rareware.env_gold",
                         "nintendo.logo_i8"}, str(names))
    for n in names:
        check("the runtime resolves '%s' too" % n, ('"%s"' % n) in src)
    # And the other direction: nothing in the C table is unreachable from here.
    import re as _re
    c_names = sorted(set(_re.findall(r'"((?:rareware|nintendo)\.[a-z0-9_]+)"', src)))
    check("no identifier exists only in the runtime", set(c_names) <= set(names),
          str(sorted(set(c_names) - set(names))))


def t_texref_registry_is_derived():
    print("\n[25] the Rareware identifiers are DERIVED from committed source")
    t = gi.game_textures()
    for n in ("rareware.env_field", "rareware.env_gold"):
        e = t.get(n)
        check("%s was read out of the tree" % n,
              e is not None and gi.RAREWARE_LOGO_C in e["source"],
              str(e and e["source"]))
        check("%s is 32x32" % n, e and (e["w"], e["h"]) == (32, 32))
    # Nothing was committed for them: their digests are not literals in the
    # importer, so they cannot go stale and they add no bytes to the tree.
    imp = (REPO / "tools" / "asset" / "gltf_import.py").read_text(encoding="utf-8")
    for n in ("rareware.env_field", "rareware.env_gold"):
        e = t[n]
        check("no digest for %s is hardcoded" % n, e["sha256"] not in imp)


def t_fnv_agrees_with_the_runtime():
    print("\n[26] FNV-1a is the same function on both sides of the format")
    # Published FNV-1a 32 test vectors.
    check("FNV-1a of the empty string", gi.fnv1a(b"") == 0x811C9DC5,
          "%08x" % gi.fnv1a(b""))
    check("FNV-1a of 'a'", gi.fnv1a(b"a") == 0xE40C292C, "%08x" % gi.fnv1a(b"a"))
    check("FNV-1a of 'foobar'", gi.fnv1a(b"foobar") == 0xBF9CF968,
          "%08x" % gi.fnv1a(b"foobar"))
    src = (REPO / "src" / "native" / "sl_texref.c").read_text(encoding="utf-8")
    check("the C side uses the same offset basis", "2166136261u" in src)
    check("the C side uses the same prime", "16777619u" in src)


def t_committed_models_carry_no_pixels():
    print("\n[27] every committed model's texture slots are references")
    d = REPO / "data" / "asset-overrides"
    files = sorted(d.rglob("*.slmodel")) if d.is_dir() else []
    if not files:
        check("no committed models to check (this is not a failure)", True)
        return
    for f in files:
        blob = f.read_bytes()
        try:
            m = check_slm1(blob)
        except ValueError as e:
            check("%s is a valid model" % f.name, False, str(e))
            continue
        check("%s is a valid model" % f.name, True)
        kinds = [t["kind"] for t in m["tex"]]
        check("%s: all %d texture slots are references" % (f.name, len(kinds)),
              all(k == gi.TEX_REF for k in kinds), str(kinds))
        check("%s: no slot carries pixel bytes" % f.name,
              all(t["rgba"] == b"" for t in m["tex"]))


def t_texgen_flags():
    print("\n[28] extras.sl_texgen declares generated coordinates per material")
    # SYNTHETIC ONLY. Two materials on one model, one asking for generated
    # coordinates and one refusing them, so the flag is shown to be PER
    # MATERIAL rather than per file - which is the whole gating claim.
    px = png(2, 2, bytes([9, 9, 9, 255]) * 4)
    mats = [
        {"name": "sweeps", "extras": {"sl_texgen": True},
         "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}},
        {"name": "painted", "extras": {"sl_texgen": False},
         "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}},
    ]
    p0 = simple_triangle(uv=True)
    p1 = simple_triangle(uv=True)
    p1["material"] = 1
    m = check_slm1(convert_doc(build_gltf([p0, p1], materials=mats,
                                          images=[px])))
    f0 = m["mats"][m["prims"][0]["material"]]["flags"]
    f1 = m["mats"][m["prims"][1]["material"]]["flags"]
    check("sl_texgen true sets the ON bit", f0 & gi.M_TEXGEN == gi.M_TEXGEN,
          "flags %#x" % f0)
    check("sl_texgen true does NOT set the OFF bit", f0 & gi.M_TEXGEN_OFF == 0,
          "flags %#x" % f0)
    check("sl_texgen false sets the OFF bit",
          f1 & gi.M_TEXGEN_OFF == gi.M_TEXGEN_OFF, "flags %#x" % f1)
    check("sl_texgen false does NOT set the ON bit", f1 & gi.M_TEXGEN == 0,
          "flags %#x" % f1)
    check("the two materials disagree, so the flag is per material",
          (f0 & (gi.M_TEXGEN | gi.M_TEXGEN_OFF))
          != (f1 & (gi.M_TEXGEN | gi.M_TEXGEN_OFF)))

    # Absent means ABSENT: neither bit, so the runtime's implication decides.
    plain = [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}]
    m2 = check_slm1(convert_doc(build_gltf([simple_triangle(uv=True)],
                                           materials=plain, images=[px])))
    check("a material that says nothing carries neither bit",
          m2["mats"][0]["flags"] & (gi.M_TEXGEN | gi.M_TEXGEN_OFF) == 0,
          "flags %#x" % m2["mats"][0]["flags"])

    # A non-bool is refused rather than coerced. "true" as a string is the
    # exact mistake this rejects, because guessing it would be a silent answer
    # to a question the author got wrong.
    for junk in ("true", 1, 0, "yes"):
        bad = [{"extras": {"sl_texgen": junk},
                "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}]
        expect_reject(
            "extras.sl_texgen = %r is refused, not coerced" % (junk,),
            lambda b=bad: convert_doc(build_gltf([simple_triangle(uv=True)],
                                                 materials=b, images=[px])))


def t_texgen_flag_bits_agree():
    print("\n[29] the texgen flag bits and the coordinate table agree across the seam")
    hdr = (REPO / "src" / "sl_asset_override.h").read_text(encoding="utf-8")
    for nm, val in (("SL_AMDL_M_TEXGEN", gi.M_TEXGEN),
                    ("SL_AMDL_M_TEXGEN_OFF", gi.M_TEXGEN_OFF)):
        check("%s is %#06x on the C side too" % (nm, val),
              ("#define %s      %#06xu" % (nm, val)).replace("0X", "0x") in hdr
              or ("%s  0x%04Xu" % (nm, val)) in hdr
              or ("0x%04xu" % val) in hdr)
    # No bit collides with one that already meant something.
    known = [gi.M_DOUBLESIDED, gi.M_ALPHA_BLEND, gi.M_ALPHA_MASK, gi.M_UNLIT,
             gi.M_TEXGEN, gi.M_TEXGEN_OFF]
    check("every material flag bit is distinct", len(set(known)) == len(known),
          str(known))

    # THE GATING RULE'S OTHER HALF. The importer's registry says which
    # identifiers name generated-coordinate textures; the runtime's table is
    # what actually decides. They are two hand-maintained lists and they must
    # not drift, exactly like the identifier lists in [24].
    src = (REPO / "src" / "native" / "sl_texref.c").read_text(encoding="utf-8")
    for name, e in sorted(gi.game_textures().items()):
        want = bool(e.get("generated"))
        # The entry runs from its identifier to the next brace-opened entry.
        i = src.index('"%s"' % name)
        j = src.find("{ \"", i + 1)
        entry = src[i : j if j > 0 else len(src)]
        got = "TEXREF_COORD_GENERATED" in entry
        check("'%s' has the same coordinate policy on both sides" % name,
              got == want, "importer generated=%s, runtime entry says %s"
              % (want, "GENERATED" if got else "STORED"))
    # Counted inside the TABLE only - the #define and the comparison in
    # sl_texref_is_generated() also spell the token, and counting those would
    # make this assert an implementation detail instead of the data.
    tbl = src[src.index("static const struct texref g_texref[]"):]
    tbl = tbl[: tbl.index("};")]
    check("the runtime marks no identifier the importer has never heard of",
          tbl.count("TEXREF_COORD_GENERATED")
          == sum(1 for e in gi.game_textures().values() if e.get("generated")),
          "%d marked in the table, %d in the importer"
          % (tbl.count("TEXREF_COORD_GENERATED"),
             sum(1 for e in gi.game_textures().values()
                 if e.get("generated"))))


def t_committed_models_keep_the_implied_default():
    print("\n[30] the committed models inherit generated coordinates by implication")
    # WHY THIS IS THE TEST AND NOT A RE-IMPORT. The gating default is resolved
    # at RUNTIME from the texture reference, so the two shipped files need no
    # flag and no new version to get the behaviour. That is a property of the
    # files as committed, and it is checked here rather than asserted: every
    # material must carry NEITHER bit, and its texture must be a reference the
    # registry marks as generated. If a future re-import started writing flags,
    # or a reference were repointed at a non-reflection texture, this fails.
    d = REPO / "data" / "asset-overrides"
    files = sorted(d.rglob("*.slmodel")) if d.is_dir() else []
    if not files:
        check("no committed models to check (this is not a failure)", True)
        return
    gt = gi.game_textures()
    for f in files:
        m = check_slm1(f.read_bytes())
        for mi, mm in enumerate(m["mats"]):
            check("%s material %d declares neither texgen bit" % (f.name, mi),
                  mm["flags"] & (gi.M_TEXGEN | gi.M_TEXGEN_OFF) == 0,
                  "flags %#x" % mm["flags"])
            ti = mm["texture"]
            check("%s material %d names a texture slot" % (f.name, mi), ti >= 0,
                  str(ti))
            if ti < 0:
                continue
            t = m["tex"][ti]
            check("%s material %d resolves to a generated-coordinate map"
                  % (f.name, mi),
                  t["kind"] == gi.TEX_REF
                  and bool(gt.get(t["name"], {}).get("generated")),
                  "slot names %r" % t["name"])


def mark_authored(doc, prov="a colour ramp drawn from seven stops",
                  on="images", index=0, value=True, with_prov=True):
    """Put the authored marker on the glTF image (or texture) object.

    A helper rather than a build_gltf parameter on purpose: the marker is
    something an AUTHOR writes into a file, so the tests should show it being
    written into an ordinary document from outside, exactly as a modelling
    tool or a hand edit would.
    """
    ex = {}
    if value is not None:
        ex[gi.AUTHORED_KEY] = value
    if with_prov:
        ex[gi.AUTHORED_PROVENANCE_KEY] = prov
    doc[on][index]["extras"] = ex
    return doc


def t_authored_textures():
    print("\n[31] --repo accepts pixels the file DECLARES as the author's own")
    plain = bytes([1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255])

    def doc():
        return build_gltf([simple_triangle(uv=True)], materials=TEXMAT,
                          images=[png(2, 2, plain)])

    with fake_registry(synthetic_registry()):
        # ---- the marked case is ACCEPTED, and the pixels really are there --
        blob = convert_doc(mark_authored(doc()), repo_safe=True)
        m = check_slm1(blob)
        check("a marked texture is EMBEDDED, not turned into a reference",
              m["tex"][0]["kind"] == gi.TEX_EMBEDDED)
        check("its pixels are actually written into the file",
              plain in blob)

        # ---- the UNMARKED case is still refused, exactly as before ---------
        expect_reject(
            "--repo still REFUSES unmarked embedded pixels",
            lambda: convert_doc(doc(), repo_safe=True))

        # ---- a reference is still accepted --------------------------------
        blob = convert_doc(build_gltf([simple_triangle(uv=True)],
                                      materials=TEXMAT,
                                      images=[png(GAME_TEX_W, GAME_TEX_H,
                                                  GAME_TEX_PX)]),
                           repo_safe=True)
        check("--repo still ACCEPTS a game-texture reference",
              check_slm1(blob)["tex"][0]["kind"] == gi.TEX_REF)

        # ---- the marker is honoured on the texture object too -------------
        blob = convert_doc(mark_authored(doc(), on="textures"), repo_safe=True)
        check("the marker is read off the texture object as well as the image",
              check_slm1(blob)["tex"][0]["kind"] == gi.TEX_EMBEDDED)

        # ---- PROVENANCE IS REQUIRED ---------------------------------------
        # A bare "trust me" is not a declaration. The string is what a
        # reviewer reads, so a marker without one is an incomplete marker and
        # is refused rather than quietly accepted.
        expect_reject(
            "sl_authored without a provenance string is REFUSED",
            lambda: convert_doc(mark_authored(doc(), with_prov=False),
                                repo_safe=True))
        expect_reject(
            "an EMPTY provenance string is REFUSED",
            lambda: convert_doc(mark_authored(doc(), prov="   "),
                                repo_safe=True))
        expect_reject(
            "a non-boolean sl_authored is REFUSED rather than guessed at",
            lambda: convert_doc(mark_authored(doc(), value="yes"),
                                repo_safe=True))
        # Explicit false is a legitimate declaration - it just does not open
        # the gate, so the categorical rule applies and the model is refused.
        expect_reject(
            "sl_authored: false does not open the gate",
            lambda: convert_doc(mark_authored(doc(), value=False),
                                repo_safe=True))

        # ---- THE ROM GUARD IS NOT WEAKENED --------------------------------
        # The load-bearing test of the whole mechanism. Content-addressed
        # detection runs BEFORE the marker is read, so declaring a GAME
        # texture as "authored" cannot embed it: it is still recognised, still
        # stored as a reference, and its pixels are still nowhere in the file.
        # If this ever fails, the marker has become a way to launder ROM
        # pixels into the repository and the mechanism must be withdrawn.
        blob = convert_doc(
            mark_authored(build_gltf([simple_triangle(uv=True)],
                                     materials=TEXMAT,
                                     images=[png(GAME_TEX_W, GAME_TEX_H,
                                                 GAME_TEX_PX)]),
                          prov="claimed as mine, but it is the game's"),
            repo_safe=True)
        m = check_slm1(blob)
        check("a GAME texture marked authored is STILL a reference",
              m["tex"][0]["kind"] == gi.TEX_REF, str(m["tex"][0]["kind"]))
        check("its identifier still comes from the registry",
              m["tex"][0]["name"] == GAME_TEX_ID)
        check("the game's pixels are STILL nowhere in the file",
              GAME_TEX_PX not in blob)

        # ---- the marker changes nothing outside --repo --------------------
        blob = convert_doc(mark_authored(doc()))
        check("outside --repo the marker changes nothing (still embedded)",
              check_slm1(blob)["tex"][0]["kind"] == gi.TEX_EMBEDDED)
        blob = convert_doc(doc())
        check("outside --repo an unmarked texture is embedded as always",
              check_slm1(blob)["tex"][0]["kind"] == gi.TEX_EMBEDDED)


def main() -> int:
    print("asset-override importer - synthetic tests (no ROM, no fixtures)")
    for fn in (t_minimal, t_attributes, t_colour_fold, t_two_primitives, t_alpha_mask,
               t_texture, t_png_filters, t_node_transform, t_reject_malformed_index,
               t_reject_truncated, t_reject_bad_version, t_reject_excessive_count,
               t_reject_unsupported_gltf, t_paths_agree, t_glb,
               t_material_texture_pairing, t_channel_order,
               t_texref_recognised, t_texref_declared, t_texref_repo_safe,
               t_texref_declaration_errors, t_texref_malformed_slots,
               t_texref_unknown_identifier_falls_back,
               t_texref_tables_agree, t_texref_registry_is_derived,
               t_fnv_agrees_with_the_runtime,
               t_committed_models_carry_no_pixels,
               t_texgen_flags, t_texgen_flag_bits_agree,
               t_committed_models_keep_the_implied_default,
               t_authored_textures):
        fn()
    print("\n%d passed, %d failed" % (PASSES, len(FAILURES)))
    for f in FAILURES:
        print("  FAILED: %s" % f)
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
