#!/usr/bin/env python3
"""Bisect the audio startup lifecycle across two differently-linked images.

The question this answers: the .aev instrumented image reaches
g_AudioFrameCount == 0 where the plain direct-boot image reaches 435. Which
lifecycle checkpoint is the EARLIEST one that differs?

Every address is resolved from THAT IMAGE'S OWN linker map. Comparing a raw
address across two links is meaningless and has already been done wrong once
(docs/backlog.md); this tool makes it impossible by construction - it never
sees an address that did not come from the map it was told to use.

Nothing here is instrumentation. Every checkpoint is ordinary global state the
game writes anyway, read out of RDRAM, so adding a checkpoint cannot perturb
the run it observes.

    tools/native/audioinit.py --image facility           # plain
    tools/native/audioinit.py --image facility.aev       # instrumented
"""
import argparse
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/trace"))

# ---- struct offsets, all derived from the headers in this tree ------------
# OSMesgQueue (include/PR/os.h:88-97)
MQ_VALID, MQ_FIRST, MQ_COUNT, MQ_MSG = 0x08, 0x0c, 0x10, 0x14
# OSThread (include/PR/os.h:63-73)
TH_NEXT, TH_PRI, TH_QUEUE, TH_STATE, TH_ID = 0x00, 0x04, 0x08, 0x10, 0x14
# OSSched (src/sched.h:47-64): retraceMsg 0x00, prenmiMsg 0x20, interruptQ
# 0x40, intBuf[8] 0x58, cmdQ 0x78, cmdMsgBuf[8] 0x90, then the pointers.
SC_THREAD, SC_CLIENTLIST = 0xb0, 0xb4
SC_FRAMECOUNT, SC_DOAUDIO = 0xd0, 0xd4
SC_SIZE = 0xd8
# OSScClient (src/sched.h:41-44)
CL_NEXT, CL_MSGQ = 0x00, 0x04
# struct AudioManager_s (src/audi.c:248-295) - offsets are in its comments
AM_CMDLIST, AM_AUDIOINFO, AM_NOUTBUF = 0x000, 0x008, 0x014
AM_THREAD, AM_FRAMEQ, AM_FRAMEBUF = 0x018, 0x1c8, 0x1e0
AM_REPLYQ, AM_REPLYBUF, AM_GLOBALS = 0x200, 0x218, 0x238
# ALHeap (include/PR/libaudio.h:126-131)
HP_BASE, HP_CUR, HP_LEN, HP_COUNT = 0x00, 0x04, 0x08, 0x0c

MUSIC_ALLOCATION_BYTES = None   # read from the source, see below

STATE_NAMES = {1: "STOPPED", 2: "RUNNABLE", 4: "RUNNING", 8: "WAITING"}

SYMS = [
    "g_sndBootswitchSound", "g_musicHeap", "g_musicSfxBufferPtr",
    "g_musicInstrumentBufferPtr", "g_musicDataTable",
    "g_musicXTrack1SeqPlayer", "g_musicXTrack2SeqPlayer",
    "g_musicXTrack3SeqPlayer", "g_FrameSize", "g_MinFrameSize",
    "g_MaxFrameSize", "g_AudioManager", "g_AudioClient", "g_DmaBuffers",
    "g_DmaMessageQueue", "os_scheduler", "g_AudioFrameCount",
    "g_sndPlayerPtr", "gfxClient", "g_CurrentAcmdList", "g_NextDMa",
    "currentFrameCounter", "g_mempPools",
]
OPTIONAL = {"slAudioEvLog"}


def load_map(path):
    """symbol -> KSEG0 address, from a GNU ld map."""
    out = {}
    pat = re.compile(r"^\s+0x([0-9a-f]{8,16})\s+([A-Za-z_][A-Za-z0-9_]*)\s*$")
    for line in path.read_text(errors="replace").splitlines():
        m = pat.match(line)
        if m:
            out.setdefault(m.group(2), int(m.group(1), 16) & 0xffffffff)
    return out


# The RDRAM byte order is NOT a detail to re-derive. The core holds 32-bit
# words in host order, and the harness's Memory class is the reader the trace
# gate is built on (tools/trace/sltrace/state.py:218-267). Reading it as plain
# big-endian - which this tool did on its first run - yields values like
# 0x503b0680 for a pointer whose true value is 0x80063b50. Use the validated
# reader, never a local one.
from sltrace.state import Memory


# The event log's storage is NOT linked, so it has no map symbol: it lives at a
# fixed address, read here through the same word-swapped RDRAM view. Physical
# address is addr & 0x1fffffff, which is what makes the KSEG1 alias and the
# KSEG0 address the same bytes.
SL_AUDIOEV_ADDR = 0xA0400000
SL_AUDIOEV_MAGIC = 0x534C4145
AEV_NAMES = {1: "FRAMECOUNT", 2: "NOTEON", 3: "SFX_ENTRY", 4: "SFX_INC",
             5: "SFX_DEC", 6: "MUSIC_REQ"}


def read_log(base, ramsize):
    """Decode the event log. Returns None if the magic is absent."""
    off = SL_AUDIOEV_ADDR & 0x1fffffff
    if off + 32 > ramsize:
        return {"error": f"0x{SL_AUDIOEV_ADDR:08x} is past the core's "
                         f"{ramsize:#x} of RDRAM"}
    def w(o):
        return struct.unpack(">I", bytes(bytearray(base[o:o + 4]))[::-1])[0]
    magic = w(off)
    if magic != SL_AUDIOEV_MAGIC:
        return {"magic": magic, "records": None}
    hdr = dict(magic=magic, version=w(off + 4), capacity=w(off + 8),
               count=w(off + 12), overflow=w(off + 16))
    recs = []
    for i in range(min(hdr["count"], hdr["capacity"])):
        b = off + 32 + i * 32
        recs.append([w(b + 4 * k) for k in range(8)])
    hdr["records"] = recs
    return hdr


def report_log(log):
    if log is None:
        return
    if "error" in log:
        print(f"  LOG: {log['error']}")
        return
    if log.get("records") is None:
        print(f"  LOG: magic absent (read 0x{log['magic']:08x}) - either the "
              f"writer never ran or 0x{SL_AUDIOEV_ADDR:08x} is not mapped")
        return
    print(f"  LOG: version={log['version']} capacity={log['capacity']} "
          f"count={log['count']} overflow={log['overflow']}")
    hist = {}
    for r in log["records"]:
        hist[r[0]] = hist.get(r[0], 0) + 1
    for k in sorted(hist):
        print(f"    {AEV_NAMES.get(k, k):<12} {hist[k]}")
    # THE POSITIVE CONTROL. A frame-count log is valid only if it carries
    # exactly one event per audio task, values 1..N in strict order, no
    # duplicates, no gaps, and no overflow. Only a log that passes this may
    # have a ZERO of any other event class believed.
    fc = [r[1] for r in log["records"] if r[0] == 1]
    if fc:
        ok = fc == list(range(1, len(fc) + 1))
        print(f"    FRAMECOUNT control: n={len(fc)} first={fc[0]} last={fc[-1]} "
              f"strict 1..n = {ok} overflow={log['overflow']}")


def mq(ram, addr):
    return dict(valid=ram.u32(addr + MQ_VALID), first=ram.u32(addr + MQ_FIRST),
                count=ram.u32(addr + MQ_COUNT), msg=ram.u32(addr + MQ_MSG))


def snapshot(ram, s):
    """Every checkpoint, read once, as plain values."""
    am = s["g_AudioManager"]
    hp = s["g_musicHeap"]
    sc = s["os_scheduler"]
    o = {}

    o["bootswitch"] = ram.u32(s["g_sndBootswitchSound"])
    o["heap"] = dict(base=ram.u32(hp + HP_BASE), cur=ram.u32(hp + HP_CUR),
                     len=ram.u32(hp + HP_LEN), count=ram.u32(hp + HP_COUNT))
    o["sfxbank"] = ram.u32(s["g_musicSfxBufferPtr"])
    o["instbank"] = ram.u32(s["g_musicInstrumentBufferPtr"])
    o["datatable"] = ram.u32(s["g_musicDataTable"])
    o["seqp"] = [ram.u32(s[f"g_musicXTrack{i}SeqPlayer"]) for i in (1, 2, 3)]
    o["framesize"] = [ram.u32(s[k]) for k in
                      ("g_MinFrameSize", "g_FrameSize", "g_MaxFrameSize")]
    o["cmdlist"] = [ram.u32(am + AM_CMDLIST + 4 * i) for i in range(2)]
    o["audioinfo"] = [ram.u32(am + AM_AUDIOINFO + 4 * i) for i in range(3)]
    o["noutbuf"] = ram.u32(am + AM_NOUTBUF)
    o["thread"] = dict(state=ram.u16(am + AM_THREAD + TH_STATE),
                       id=ram.u32(am + AM_THREAD + TH_ID),
                       pri=ram.u32(am + AM_THREAD + TH_PRI),
                       queue=ram.u32(am + AM_THREAD + TH_QUEUE),
                       next=ram.u32(am + AM_THREAD + TH_NEXT))
    o["frameq"] = mq(ram, am + AM_FRAMEQ)
    o["frameq_addr"] = am + AM_FRAMEQ
    o["framebuf_addr"] = am + AM_FRAMEBUF
    o["replyq"] = mq(ram, am + AM_REPLYQ)
    o["replybuf_addr"] = am + AM_REPLYBUF
    o["dmaq"] = mq(ram, s["g_DmaMessageQueue"])
    o["globals"] = ram.block(am + AM_GLOBALS, 0x20).hex(" ")
    o["dmabuf0ptr"] = ram.u32(s["g_DmaBuffers"] + 0x08)
    o["sched"] = dict(thread=ram.u32(sc + SC_THREAD),
                      clientList=ram.u32(sc + SC_CLIENTLIST),
                      frameCount=ram.u32(sc + SC_FRAMECOUNT),
                      doAudio=ram.u32(sc + SC_DOAUDIO))

    # Registration proof: walk the scheduler's OWN list, count the audio client.
    chain, hits, gfx_hits, cur = [], 0, 0, o["sched"]["clientList"]
    for _ in range(64):
        if cur == 0:
            break
        if not ram.valid_ptr(cur):
            chain.append(-1)
            break
        chain.append(cur)
        if cur == s["g_AudioClient"]:
            hits += 1
        if cur == s["gfxClient"]:
            gfx_hits += 1
        try:
            cur = ram.u32(cur + CL_NEXT)
        except ValueError:
            chain.append(-1)      # next is not an RDRAM address: list is broken
            break
    o["clients"] = chain
    o["audio_client_hits"] = hits
    o["gfx_client_hits"] = gfx_hits
    o["audio_client_addr"] = s["g_AudioClient"]
    o["audio_client_next"] = ram.u32(s["g_AudioClient"] + CL_NEXT)
    o["audio_client_msgq"] = ram.u32(s["g_AudioClient"] + CL_MSGQ)

    # The memory pools. mempAllocBytesInBank spins forever (memp.c:158,165,190)
    # rather than returning NULL, so an exhausted pool looks exactly like a
    # hang - which is why these are read before anything is inferred.
    o["pools"] = [dict(start=ram.u32(s["g_mempPools"] + 16 * i),
                       pos=ram.u32(s["g_mempPools"] + 16 * i + 4),
                       end=ram.u32(s["g_mempPools"] + 16 * i + 8),
                       prevpos=ram.u32(s["g_mempPools"] + 16 * i + 12))
                  for i in range(7)]
    o["gameframe"] = ram.s32(s["currentFrameCounter"])
    o["framecount"] = ram.u32(s["g_AudioFrameCount"])
    o["sndplayer"] = ram.u32(s["g_sndPlayerPtr"])
    o["acmdlist"] = ram.u32(s["g_CurrentAcmdList"])
    o["nextdma"] = ram.u32(s["g_NextDMa"])
    return o


def report(tag, s, o):
    p = print
    p(f"===== {tag} =====")
    p(f"  symbols from its own map; g_AudioManager @0x{s['g_AudioManager']:08x} "
      f"os_scheduler @0x{s['os_scheduler']:08x}")
    p(f"  CP1  init entry precondition  g_sndBootswitchSound = {o['bootswitch']}"
      f"   (nonzero => musicSeqPlayerInit returns immediately)")
    h = o["heap"]
    p(f"  CP2  music heap               base=0x{h['base']:08x} cur=0x{h['cur']:08x} "
      f"len={h['len']} used={(h['cur'] - h['base']) if h['base'] else 0} "
      f"free={(h['len'] - (h['cur'] - h['base'])) if h['base'] else 0} count={h['count']}")
    p(f"  CP3  banks                    sfx=0x{o['sfxbank']:08x} "
      f"inst=0x{o['instbank']:08x} seqtable=0x{o['datatable']:08x}")
    p(f"  CP4  synth / frame sizing     min/frame/max = {o['framesize']}")
    p(f"       ALGlobals[0x00..0x20]    {o['globals']}")
    p(f"  CP5  sequence players         " +
      " ".join(f"0x{v:08x}" for v in o["seqp"]))
    p(f"  CP6  manager construction     cmdList=" +
      " ".join(f"0x{v:08x}" for v in o["cmdlist"]) +
      "  audioInfo=" + " ".join(f"0x{v:08x}" for v in o["audioinfo"]) +
      f"  dmaBuf0.ptr=0x{o['dmabuf0ptr']:08x}")
    t = o["thread"]
    p(f"  CP7  audio thread             id={t['id']} pri={t['pri']} "
      f"state={t['state']} ({STATE_NAMES.get(t['state'], '?')}) "
      f"queue=0x{t['queue']:08x} next=0x{t['next']:08x}")
    p(f"  CP8  client registration      audio client 0x{o['audio_client_addr']:08x} "
      f"appears {o['audio_client_hits']}x, gfx {o['gfx_client_hits']}x, "
      f"list=[" + " ".join(f"0x{c:08x}" for c in o["clients"]) + "]")
    p(f"       client fields            next=0x{o['audio_client_next']:08x} "
      f"msgQ=0x{o['audio_client_msgq']:08x} "
      f"(frameMessageQueue is 0x{o['frameq_addr']:08x})")
    q = o["frameq"]
    p(f"  CP9  frame message queue      valid={q['valid']} first={q['first']} "
      f"msgCount={q['count']} msg=0x{q['msg']:08x} "
      f"(frameMessageBuffer is 0x{o['framebuf_addr']:08x})")
    sc = o["sched"]
    p(f"  CP10 scheduler                frameCount={sc['frameCount']} "
      f"doAudio={sc['doAudio']} thread=0x{sc['thread']:08x} "
      f"clientList=0x{sc['clientList']:08x}")
    p(f"  CP11 notifications posted     inferred from CP9 valid/first above")
    r = o["replyq"]
    p(f"  CP12 reply queue              valid={r['valid']} first={r['first']} "
      f"msgCount={r['count']} msg=0x{r['msg']:08x}")
    d = o["dmaq"]
    p(f"  CP13 dma queue                valid={d['valid']} first={d['first']} "
      f"msgCount={d['count']}")
    p(f"  CP14 acmd/dma progress        g_CurrentAcmdList={o['acmdlist']} "
      f"g_NextDMa={o['nextdma']}")
    p(f"  CP15 g_AudioFrameCount        {o['framecount']}")
    p(f"       currentFrameCounter      {o['gameframe']}")
    names = ["TOTAL", "MF", "2", "ML", "STAGE", "ME", "PERMANENT"]
    p(f"       g_mempPools @0x{s['g_mempPools']:08x}")
    for n, q in zip(names, o["pools"]):
        free = q["end"] - q["pos"]
        p(f"         {n:<10} start=0x{q['start']:08x} pos=0x{q['pos']:08x} "
          f"end=0x{q['end']:08x} free={free:>9} used={q['pos'] - q['start']:>9}")
    p(f"       g_sndPlayerPtr           0x{o['sndplayer']:08x}")
    report_log(o.get("log"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True,
                    help="direct-boot image stem, e.g. 'facility' or 'facility.aev'")
    ap.add_argument("--input", default=None,
                    help="input stream name (default: image stem before the first dot)")
    ap.add_argument("--frames", type=int, default=900)
    ap.add_argument("--live", type=int, default=0,
                    help="also print a liveness row every N frames")
    ap.add_argument("--at", type=int, action="append", default=None,
                    help="also report at this frame index (repeatable)")
    args = ap.parse_args()

    stem = args.image
    inp_name = args.input or stem.split(".")[0]
    rom = ROOT / "build/u/direct" / f"ge007.u.{stem}.z64"
    mp = ROOT / "build/u/direct" / f"ge007.u.{stem}.map"
    for f in (rom, mp):
        if not f.is_file():
            print(f"audioinit: missing {f}", file=sys.stderr)
            return 2

    syms = load_map(mp)
    missing = [k for k in SYMS if k not in syms]
    if missing:
        print(f"audioinit: {mp.name} lacks {missing}", file=sys.stderr)
        return 2
    s = {k: syms[k] for k in SYMS}
    for k in OPTIONAL:
        if k in syms:
            s[k] = syms[k]

    from sltrace.emu_libretro import LibretroEmulator
    inp = ROOT / "tools/trace/inputs" / (inp_name + ".input")
    eep = ROOT / "tools/trace/inputs" / (inp_name + ".eeprom")
    emu = LibretroEmulator(str(rom), input_stream=str(inp) if inp.is_file() else None,
                           eeprom=str(eep) if eep.is_file() else None)

    marks = sorted(set(args.at or []) | {args.frames - 1})
    seen = {}
    size_holder = {}

    import hashlib

    def liveness(index, ram):
        """Is this image EXECUTING? Three independent witnesses, no inference.

        currentFrameCounter is the game's own tick; os_scheduler.frameCount is
        the scheduler's retrace count; the digest is over a 256 KB slice of the
        game's own bss/heap region, so a frozen machine and a running one
        cannot look alike."""
        blob = ram.block(0x80040000, 0x40000)
        print(f"  live f{index:5d}  currentFrameCounter={ram.s32(s['currentFrameCounter']):8d}"
              f"  sched.frameCount={ram.u32(s['os_scheduler'] + SC_FRAMECOUNT):8d}"
              f"  g_AudioFrameCount={ram.u32(s['g_AudioFrameCount']):6d}"
              f"  digest={hashlib.sha1(blob).hexdigest()[:12]}")

    def on_tick(index, fc, base):
        if args.live and index % args.live == 0:
            if "n" not in size_holder:
                size_holder["n"] = emu._core.retro_get_memory_size(2)
            liveness(index, Memory(base, size_holder["n"]))
        if index in marks:
            if "n" not in size_holder:
                size_holder["n"] = emu._core.retro_get_memory_size(2)
            ram = Memory(base, size_holder["n"])
            seen[index] = snapshot(ram, s)
            seen[index]["log"] = read_log(base, size_holder["n"])

    stats = emu.run(on_tick, lambda ram: 0, max_ticks=args.frames)
    emu.close()

    print(f"audioinit: image {rom.name}  map {mp.name}  input {inp_name}  "
          f"frames {stats['frames']}")
    for i in marks:
        if i in seen:
            report(f"{rom.name} @ frame {i}", s, seen[i])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
