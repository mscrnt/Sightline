#!/usr/bin/env python3
"""Generate byte-swap functions from bondtypes.h struct definitions (T4).

Parses `typedef struct NAME { ... } NAME;` bodies and emits a C function
per struct that swaps every multi-byte field in place (wire big-endian ->
native), preserving byte fields.  Constructs the parser can't prove safe
(bitfields, unions with mixed-granularity members) are listed as WARN
comments in the output for manual resolution.

Usage: gen_swap.py <header> <StructName>... > out.c
"""
import re
import sys

WORD_TYPES = {
    "u32", "s32", "f32", "int", "uint32_t", "int32_t", "float",
    "romptr_t", "uintptr_t",
}
HALF_TYPES = {"u16", "s16", "short", "uint16_t", "int16_t"}
BYTE_TYPES = {"u8", "s8", "char", "bool", "uint8_t", "int8_t"}
# composite expansions: name -> number of 32-bit words
COMPOSITE_WORDS = {
    "coord3d": 3, "vec3d": 3, "coord2d": 2, "bbox": 6, "rect4f": 8,
    "Mtxf": 16, "view4f": 4, "view4s32": 4,
}
# structs made only of bytes: nothing to swap
BYTE_COMPOSITES = {"rgba_u8"}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def find_struct(header_text, name):
    pat_tail = r"\s*(?:/\*.*?\*/\s*)?\{"
    m = re.search(
        r"typedef\s+struct\s+" + re.escape(name) + pat_tail,
        header_text, re.S)
    if not m:
        m = re.search(r"struct\s+" + re.escape(name) + pat_tail,
                      header_text, re.S)
    if not m:
        return None
    i = m.end()
    depth = 1
    while depth and i < len(header_text):
        c = header_text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        i += 1
    return header_text[m.end():i - 1]


def parse_fields(body):
    """Yield (kind, ctype, name, count) or ('warn', message)."""
    body = strip_comments(body)
    # flatten unions: keep the first member declaration, warn
    while True:
        m = re.search(r"union\s*(\w*)\s*\{", body)
        if not m:
            break
        i = m.end()
        depth = 1
        while depth:
            if body[i] == "{":
                depth += 1
            elif body[i] == "}":
                depth -= 1
            i += 1
        # union body without braces; take text up to first ';' at depth 0
        inner = body[m.end():i - 1]
        # the union member may carry a name after '}'
        tail = re.match(r"\s*(\w*)\s*;", body[i:])
        taillen = tail.end() if tail else 0
        first = inner.split(";")[0] + ";"
        note = "/* WARN union flattened to first member: %s */" % first.strip()
        body = body[:m.start()] + note + first + body[i + taillen:]
    fields = []
    for decl in body.split(";"):
        decl = decl.strip()
        if not decl:
            continue
        if "WARN" in decl:
            fields.append(("warn", decl, "", 0))
            m2 = re.search(r"\*/(.*)$", decl, re.S)
            if m2 and m2.group(1).strip():
                decl = m2.group(1).strip()
            else:
                continue
        if decl.startswith("inherits"):
            base = decl.split()[1]
            fields.append(("inherit", base, "", 0))
            continue
        if ":" in decl and "[" not in decl:
            fields.append(("warn", "/* WARN bitfield: %s */" % decl, "", 0))
            continue
        m = re.match(
            r"(?:struct\s+|enum\s+|const\s+)*(\w+)\s*(\*?)\s*(\w+)"
            r"(?:\s*\[\s*(\w+)\s*\])?$", decl)
        if not m:
            fields.append(("warn", "/* WARN unparsed: %s */" % decl, "", 0))
            continue
        ctype, ptr, name, count = m.groups()
        count = count or "1"
        if ptr:
            fields.append(("word", ctype + " *", name, count))
        elif ctype in WORD_TYPES:
            fields.append(("word", ctype, name, count))
        elif ctype in HALF_TYPES:
            fields.append(("half", ctype, name, count))
        elif ctype in BYTE_TYPES:
            fields.append(("byte", ctype, name, count))
        elif ctype in COMPOSITE_WORDS:
            fields.append(("composite", ctype, name, count))
        elif ctype in BYTE_COMPOSITES:
            continue
        else:
            fields.append(("maybe_struct", ctype, name, count))
    return fields


def emit(header_text, name, emitted):
    if name in emitted:
        return ""
    emitted.add(name)
    body = find_struct(header_text, name)
    if body is None:
        return "/* WARN struct %s not found */\n" % name
    out = []
    pre = []
    for kind, ctype, fname, count in parse_fields(body):
        if kind == "warn":
            out.append("    " + ctype)
        elif kind == "inherit":
            pre.append(emit(header_text, ctype, emitted))
            out.append("    sl_swap_%s((struct %s *) r);" % (ctype, ctype))
        elif kind == "byte":
            continue
        elif kind == "half":
            if count == "1":
                out.append("    SW16(r->%s);" % fname)
            else:
                out.append("    for (i = 0; i < %s; i++) SW16(r->%s[i]);"
                           % (count, fname))
        elif kind == "word":
            if count == "1":
                out.append("    SW32(r->%s);" % fname)
            else:
                out.append("    for (i = 0; i < %s; i++) SW32(r->%s[i]);"
                           % (count, fname))
        elif kind == "maybe_struct":
            if find_struct(header_text, ctype) is not None:
                pre.append(emit(header_text, ctype, emitted))
                if count == "1":
                    out.append("    sl_swap_%s(&r->%s);" % (ctype, fname))
                else:
                    out.append(
                        "    for (i = 0; i < %s; i++)"
                        " sl_swap_%s(&r->%s[i]);" % (count, ctype, fname))
            else:
                out.append("    SW32(r->%s);" % fname if count == "1" else
                           "    for (i = 0; i < %s; i++) SW32(r->%s[i]);"
                           % (count, fname))
                out.append("    /* WARN assumed 32-bit enum/typedef:"
                           " %s %s */" % (ctype, fname))
        elif kind == "composite":
            n = COMPOSITE_WORDS[ctype]
            if count == "1":
                out.append("    for (i = 0; i < %d; i++)"
                           " SW32(((u32 *) &r->%s)[i]);" % (n, fname))
            else:
                out.append("    for (i = 0; i < %d * %s; i++)"
                           " SW32(((u32 *) &r->%s)[i]);" % (n, count, fname))
    needs_i = any("for (i" in line for line in out)
    fn = ["".join(pre)]
    fn.append("static void sl_swap_%s(struct %s *r)\n{\n" % (name, name))
    if needs_i:
        fn.append("    s32 i;\n\n")
    fn.append("\n".join(out))
    fn.append("\n}\n\n")
    return "".join(fn)


def main():
    headers = []
    names = []
    for arg in sys.argv[1:]:
        if arg.endswith(".h"):
            headers.append(open(arg).read())
        else:
            names.append(arg)
    header = "\n".join(headers)
    emitted = set()
    print("/* GENERATED by tools/native/gen_swap.py - edit the WARN spots"
          " by hand, then keep. */")
    for name in names:
        sys.stdout.write(emit(header, name, emitted))


main()
