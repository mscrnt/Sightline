"""Print model bounds against the GoldenEye logo prop's box. Run at import time.

The box is the ORIGINAL prop's full extent, measured from its two display
lists (lettering + flatshaded_slab). There is no importer slot for this asset
yet, so this is a reference figure, not a contract the tooling enforces.
"""
import sys, numpy as np, trimesh
BOX = np.array([2568.0, 544.0, 134.0])      # x -1284..1284, y -272..272, z -67..67
p = sys.argv[1] if len(sys.argv) > 1 else "sightline.gltf"
s = trimesh.load(p)
m = s.dump(concatenate=True) if hasattr(s, "geometry") else s
lo, hi = m.bounds
print("file      ", p)
print("min       ", np.round(lo, 2))
print("max       ", np.round(hi, 2))
print("extents   ", np.round(m.extents, 2))
print("prop box  ", BOX, "  (original GoldenEye logo, both display lists)")
print("headroom  ", np.round(BOX - m.extents, 2))
print("FITS      ", bool(np.all(m.extents <= BOX + 1e-6)))
print()
print("lettering alone in the original: x[-1284,1284] y[-139,106] z=0")
print("this model:                      x[%.0f,%.0f] y[%.0f,%.0f] z[%.0f,%.0f]"
      % (lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
