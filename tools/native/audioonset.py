"""Where do the cartridge's first 30 video frames go, INSIDE musicSeqPlayerInit?

STRICTLY ONE DOMAIN. This asks a cartridge-domain question about cartridge
video frames and nothing else. It deliberately does NOT compute a difference
against the native onset: docs/backlog.md records that the cartridge frame
domain and native pumped frames have NO derived mapping - one event pair
(cartridge frame 313 = native pumped frame 501) is the only correspondence
ever established - and an earlier pass already withdrew a claim for exactly
that subtraction.

So "the 28 frames" is not a quantity and is not what this measures. What it
measures is how the cartridge's own 30 frames are distributed across the
milestones of its own audio init.

Every field is read from RDRAM at addresses taken from the image's own map.
"""
import sys, pathlib, re
ROOT = pathlib.Path("/mnt/projects/sightline")
sys.path.insert(0, str(ROOT / "tools/trace"))
from sltrace.emu_libretro import LibretroEmulator
from sltrace.state import Memory

stem = "facility"
rom = ROOT/"build/u/direct"/f"ge007.u.{stem}.z64"; mp = ROOT/"build/u/direct"/f"ge007.u.{stem}.map"
inp = ROOT/"tools/trace/inputs"/(stem+".input"); eep = ROOT/"tools/trace/inputs"/(stem+".eeprom")
S={}; pat=re.compile(r"^\s+0x([0-9a-f]{8,16})\s+([A-Za-z_][A-Za-z0-9_.$]*)\s*$")
for l in mp.read_text(errors="replace").splitlines():
    m=pat.match(l)
    if m: S.setdefault(m.group(2), int(m.group(1),16)&0xffffffff)

AM = S["g_AudioManager"]
# milestone -> (address, "nonzero" predicate description)
MS = [("mempPools initialised",      S["g_mempPools"]+4),      # TOTAL.pos
      ("music heap base set",        S["g_musicHeap"]),
      ("sfx bank pointer",           S["g_musicSfxBufferPtr"]),
      ("instrument bank pointer",    S["g_musicInstrumentBufferPtr"]),
      ("sequence table pointer",     S["g_musicDataTable"]),
      ("synth frame size computed",  S["g_FrameSize"]),
      ("cmdList[0] allocated",       AM+0x00),
      ("audioInfo[0] allocated",     AM+0x08),
      ("seq player 1 allocated",     S["g_musicXTrack1SeqPlayer"]),
      ("seq player 3 allocated",     S["g_musicXTrack3SeqPlayer"]),
      ("audio thread id set",        AM+0x18+0x14),
      ("scheduler client list",      S["os_scheduler"]+0xb4),
      ("g_AudioFrameCount >= 1",     S["g_AudioFrameCount"])]

emu=LibretroEmulator(str(rom), input_stream=str(inp), eeprom=str(eep))
first={}
def on_tick(i, fc, base):
    m=Memory(base,0x800000)
    for name,addr in MS:
        if name in first: continue
        try:
            if m.u32(addr) != 0: first[name]=i
        except Exception: pass
emu.run(on_tick, lambda r:0, max_ticks=120); emu.close()
print("cartridge: first video frame at which each milestone is observable")
prev=None
for name,_ in MS:
    v=first.get(name)
    d = "" if (v is None or prev is None) else f"   (+{v-prev})"
    print(f"  {name:32s} {v if v is not None else 'not within 120'}{d}")
    if v is not None: prev=v
