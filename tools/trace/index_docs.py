import os

ROOT = "/mnt/projects/goldeneye_docs/notes/GE Documentation"
rows = []
for dp, dn, fn in os.walk(ROOT):
    for f in fn:
        p = os.path.join(dp, f)
        rel = os.path.relpath(p, ROOT)
        try:
            sz = os.path.getsize(p)
            head = ""
            with open(p, errors="replace") as fh:
                for _ in range(8):
                    line = fh.readline()
                    if not line:
                        break
                    line = line.strip()
                    if line and not set(line) <= set("_-=+*#/ \t"):
                        head = line[:88]
                        break
        except Exception as exc:
            sz, head = 0, f"<{exc}>"
        rows.append((rel, sz, head))

rows.sort()
with open("/tmp/docs-index.tsv", "w") as out:
    for rel, sz, head in rows:
        out.write(f"{rel}\t{sz}\t{head}\n")

print(f"indexed {len(rows)} files, {sum(r[1] for r in rows)/1024:.0f} KB")
top = {}
for rel, sz, _ in rows:
    k = rel.split(os.sep)[0]
    top[k] = top.get(k, 0) + 1
for k, v in sorted(top.items(), key=lambda x: -x[1]):
    print(f"  {v:4d}  {k}")
