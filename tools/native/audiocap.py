"""PCM capture metrics, shared by the ROM oracle and the native build.

Audio correctness cannot be settled by listening. This module is the yardstick
both sides are measured with, so that "does the native mixer match the
cartridge?" is a numeric question with a numeric answer.

Two properties matter more than the metric list:

1. **The same code measures both sides.** If the ROM capture and the native
   capture went through different statistics, a difference between them would
   be unattributable - it could be the mixer or it could be the measurement.
   One implementation, two inputs.

2. **The metrics are chosen to catch the failures this port will actually
   have**, not to be a general audio toolkit:

   - `peak` / `rms` / `nonzero_frac` separate "silence" from "wrong" from
     "right". A stubbed mixer submits zeros; sl_audio.c already reports that
     case, and this repeats it for a file on disk.
   - `left`/`right` are reported SEPARATELY because an envmixer that fills
     only one bus is a plausible ACMD bug and a mono-summed RMS would hide it.
   - `onset_frame` is the timing yardstick. Two captures can have identical
     spectra and still be wrong if a sound fires at the wrong tick, and the
     whole point of driving both sides from one input stream is to make onset
     comparable.
   - `dc_offset` catches sign errors and half-swapped samples, which read as
     plausible-but-wrong on every amplitude metric.
   - `rms_byteswapped` is a DIAGNOSTIC, not a decision. Reading s16 with the
     wrong endianness inflates RMS enormously and drives nonzero to ~100%, so
     printing both lets a human see a byte-order mistake instead of arguing
     about it. Nothing here silently "corrects" byte order - B-022 records
     what that class of guess already cost this project.

No new dependencies: stdlib only. `audioop` would give C-speed RMS but is
deprecated and slated for removal, so the passes below are plain Python. A
measurement tool is allowed to take a few seconds.
"""
from __future__ import annotations

import array
import datetime
import hashlib
import json
import pathlib
import subprocess
import sys
import wave

#: A sample is "sounding" at or above this absolute s16 value. Not zero: a
#: decoder can emit +-1 dither that is inaudible but would make onset fire on
#: the first block and make nonzero_frac meaningless.
ONSET_THRESHOLD = 64

#: Stereo frames per block for the silent-block census. 1024 frames is ~46ms
#: at 22050Hz - short enough to localise a dropout, long enough that ordinary
#: zero crossings do not register as silence.
BLOCK_FRAMES = 1024


def sha1_file(path) -> str | None:
    p = pathlib.Path(path)
    if not p.is_file():
        return None
    h = hashlib.sha1()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def git_rev(root) -> str | None:
    try:
        out = subprocess.run(["git", "-C", str(root), "rev-parse", "--short", "HEAD"],
                             capture_output=True, text=True, timeout=10)
        return out.stdout.strip() or None
    except Exception:
        return None


class Capture:
    """Interleaved stereo s16 PCM plus the metadata needed to reproduce it.

    `endian` describes the bytes as they arrived. The libretro core hands back
    host-order samples; the native build's SL_AUDIO_DUMP writes whatever the
    game submitted, BEFORE sl_audio.c's optional swap - so a mixer that keeps
    N64 DRAM order produces big-endian bytes there. It is an explicit argument
    rather than a guess for the reason in the module docstring.
    """

    def __init__(self, pcm: bytes, *, rate: int, endian: str = "little",
                 source: str = "unknown", meta: dict | None = None):
        if endian not in ("little", "big"):
            raise ValueError(f"endian must be 'little' or 'big', got {endian!r}")
        # Trim a trailing partial stereo frame rather than misalign the
        # channels: a truncated dump would otherwise swap L and R for the
        # whole file and every per-channel metric below would be nonsense.
        frame_bytes = 4
        usable = len(pcm) - (len(pcm) % frame_bytes)
        self.truncated = len(pcm) - usable
        self.pcm = pcm[:usable]
        self.rate = int(rate)
        self.endian = endian
        self.source = source
        self.meta = dict(meta or {})

    @classmethod
    def from_file(cls, path, **kw) -> "Capture":
        return cls(pathlib.Path(path).read_bytes(), **kw)

    def samples(self) -> array.array:
        a = array.array("h")
        a.frombytes(self.pcm)
        if (self.endian == "big") != (sys.byteorder == "big"):
            a.byteswap()
        return a

    def frames(self) -> int:
        return len(self.pcm) // 4

    def metrics(self) -> dict:
        a = self.samples()
        n = len(a)
        nframes = n // 2

        if n == 0:
            return {
                "bytes": 0, "samples": 0, "stereo_frames": 0, "rate": self.rate,
                "duration_s": 0.0, "peak": 0, "rms": 0.0, "nonzero": 0,
                "nonzero_frac": 0.0, "dc_offset": 0.0, "onset_frame": None,
                "onset_s": None, "silent_blocks": 0, "blocks": 0,
                "left": {"peak": 0, "rms": 0.0}, "right": {"peak": 0, "rms": 0.0},
                "rms_byteswapped": 0.0, "pcm_sha1": hashlib.sha1(b"").hexdigest(),
                "truncated_bytes": self.truncated, "silent": True,
            }

        peak = 0
        sumsq = 0
        total = 0
        nonzero = 0
        lpeak = rpeak = 0
        lsq = rsq = 0
        onset = None

        for i in range(n):
            v = a[i]
            total += v
            if v:
                nonzero += 1
            av = -v if v < 0 else v
            sumsq += v * v
            if av > peak:
                peak = av
            if i & 1:
                rsq += v * v
                if av > rpeak:
                    rpeak = av
            else:
                lsq += v * v
                if av > lpeak:
                    lpeak = av
            if onset is None and av >= ONSET_THRESHOLD:
                onset = i // 2

        half = max(nframes, 1)
        # Same bytes read the other way round. Only ever reported.
        b = self.samples()
        b.byteswap()
        swsq = 0
        for v in b:
            swsq += v * v

        blocks = (nframes + BLOCK_FRAMES - 1) // BLOCK_FRAMES
        silent_blocks = 0
        for blk in range(blocks):
            lo = blk * BLOCK_FRAMES * 2
            hi = min(lo + BLOCK_FRAMES * 2, n)
            if not any(a[j] for j in range(lo, hi)):
                silent_blocks += 1

        return {
            "bytes": len(self.pcm),
            "samples": n,
            "stereo_frames": nframes,
            "rate": self.rate,
            "duration_s": round(nframes / self.rate, 4) if self.rate else None,
            "peak": peak,
            "rms": round((sumsq / n) ** 0.5, 2),
            "nonzero": nonzero,
            "nonzero_frac": round(nonzero / n, 6),
            "dc_offset": round(total / n, 3),
            "onset_frame": onset,
            "onset_s": round(onset / self.rate, 4) if (onset is not None and self.rate) else None,
            "silent_blocks": silent_blocks,
            "blocks": blocks,
            "left": {"peak": lpeak, "rms": round((lsq / half) ** 0.5, 2)},
            "right": {"peak": rpeak, "rms": round((rsq / half) ** 0.5, 2)},
            "rms_byteswapped": round((swsq / n) ** 0.5, 2),
            "pcm_sha1": hashlib.sha1(self.pcm).hexdigest(),
            "truncated_bytes": self.truncated,
            "silent": nonzero == 0,
        }

    def write_wav(self, path) -> pathlib.Path:
        """Write a real .wav so a human CAN listen - as a cross-check on the
        numbers, never as the verdict. ROM-derived; belongs under /tmp."""
        p = pathlib.Path(path)
        p.parent.mkdir(parents=True, exist_ok=True)
        a = self.samples()          # normalised to host order
        if sys.byteorder == "big":  # wave wants little-endian
            a = array.array("h", a)
            a.byteswap()
        with wave.open(str(p), "wb") as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(self.rate or 22050)
            w.writeframes(a.tobytes())
        return p

    def write_json(self, path, extra: dict | None = None) -> pathlib.Path:
        p = pathlib.Path(path)
        p.parent.mkdir(parents=True, exist_ok=True)
        doc = {
            "source": self.source,
            "captured_utc": datetime.datetime.now(datetime.timezone.utc)
                            .strftime("%Y-%m-%dT%H:%M:%SZ"),
            "endian": self.endian,
            "metrics": self.metrics(),
            "repro": self.meta,
        }
        if extra:
            doc.update(extra)
        p.write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n")
        return p


def format_metrics(label: str, m: dict) -> str:
    if m["samples"] == 0:
        return f"{label:<10} NO AUDIO CAPTURED (0 bytes)"
    onset = "never" if m["onset_frame"] is None else \
        f"frame {m['onset_frame']} ({m['onset_s']:.3f}s)"
    return (
        f"{label:<10} {m['stereo_frames']:>9} frames  {m['duration_s']:>7.2f}s "
        f"@{m['rate']}Hz\n"
        f"{'':<10} peak={m['peak']:<6} rms={m['rms']:<9} "
        f"nonzero={100 * m['nonzero_frac']:.2f}%  dc={m['dc_offset']}\n"
        f"{'':<10} L peak={m['left']['peak']:<6} rms={m['left']['rms']:<9}"
        f"R peak={m['right']['peak']:<6} rms={m['right']['rms']}\n"
        f"{'':<10} onset={onset}  silent-blocks="
        f"{m['silent_blocks']}/{m['blocks']}\n"
        f"{'':<10} sha1={m['pcm_sha1'][:16]}  "
        f"rms-if-byteswapped={m['rms_byteswapped']}"
    )
