#!/usr/bin/env python3
"""Run the REAL ROM under the libretro core and save frames, and/or audio.

The reference the native renderer never had. Every "is this ours or Rare's?"
question was being argued from the decomp; this answers it by looking at what
the cartridge actually draws, using the input streams already recorded.

    tools/native/romshot.py --input facility-sweep-007agent --frames 9000 \
                            --every 250 --out /tmp/rom-facility

The same run can capture the cartridge's AUDIO, which is the oracle the native
mixer will be built against (B-022). The wrapper already exposes the hook; all
this does is write what comes out of it, with the metrics that make two
captures comparable:

    tools/native/romshot.py --input facility-pane --frames 3600 \
                            --audio-out /tmp/rom-facility/audio --no-video

Frames and PCM are ROM-derived: they go under /tmp and are never committed.
A libretro core may only be loaded once per process, so this runs standalone.

ON DECLARED RATES, measured 2026-08-28 and worth knowing before trusting one.
parallel_n64 declares 32040Hz up to and including frame 33, then revises itself
to 22047Hz at frame 34 - the point GoldenEye configures the AI. So the declared
rate is not simply "wrong": it is a stock N64 default until the game speaks up,
and correct afterwards (22047 against audi.c's OUTPUT_RATE of 22050). Anything
that samples it early gets the default. The rate written into the .wav here is
therefore MEASURED from samples-per-video-frame, which does not depend on when
you happen to ask. That measurement lands at ~21360Hz over a 900-frame facility
run, ~3% under both declarations, and the discrepancy is NOT yet explained -
treat the wav's rate as a playback convenience, not as a settled fact.
"""
import argparse, ctypes, os, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/trace"))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True,
                    help="stream name under tools/trace/inputs (no extension)")
    ap.add_argument("--frames", type=int, default=6000)
    ap.add_argument("--every", type=int, default=250)
    ap.add_argument("--out", default="/tmp/romshot")
    ap.add_argument("--audio-out", default=None, metavar="PREFIX",
                    help="capture the cartridge's PCM to PREFIX.wav and write "
                         "PREFIX.json with the metrics and repro metadata. "
                         "This is the audio oracle - see audiocap.py.")
    ap.add_argument("--audio-rate", type=int, default=None,
                    help="force the rate written into the .wav. The default is "
                         "MEASURED from samples-per-video-frame rather than "
                         "taken from the core - see the note on declared rates "
                         "in the module docstring.")
    ap.add_argument("--no-video", action="store_true",
                    help="skip framebuffer capture; audio only, much faster")
    ap.add_argument("--eeprom", default=None,
                    help="explicit eeprom path; otherwise the one beside the "
                         "input stream. Progress state changes what the ROM "
                         "boots into, so a mismatched save is one more way "
                         "for two runs to be different playthroughs.")
    ap.add_argument("--rom", default=None,
                    help="defaults to the direct-boot ROM the stream was "
                         "recorded against, NOT baserom - a stream recorded "
                         "through trace-record answers a direct-boot build, "
                         "and replaying it against baserom drives a different "
                         "level entirely (measured: a facility stream landed "
                         "in Surface)")
    args = ap.parse_args()

    rom = args.rom
    if rom is None:
        direct = ROOT / "build/u/direct" / f"ge007.u.{args.input}.z64"
        rom = str(direct) if direct.is_file() else str(ROOT / "baserom.u.z64")
    print(f"romshot: rom = {rom}")

    from sltrace.emu_libretro import LibretroEmulator

    # --input takes a stream NAME under tools/trace/inputs, or a PATH to an
    # input file. The path form is what makes a MATCHED comparison possible at
    # all: the native recorder writes its stream to ~/.sightline/runs/<id>/
    # input in the identical format (one 4-byte big-endian record per VI
    # retrace - sl_ultra_shim.c:497 on this side, InputFrame.from_bytes on the
    # other), so the same buttons can be pushed through the cartridge and
    # through the native build. Without it the two sides run DIFFERENT
    # playthroughs and any frame pairing between them is a guess. Pairing
    # still has to be measured afterwards - the ROM image seeds its RNG from
    # osGetCount (boss.c:389) so the two runs are not identical even on
    # identical input - but with a shared stream there is at least a pair to
    # find.
    if "/" in args.input or args.input.endswith(".input"):
        inp = pathlib.Path(args.input)
        eep = inp.with_suffix(".eeprom")
    else:
        inp = ROOT / "tools/trace/inputs" / (args.input + ".input")
        eep = ROOT / "tools/trace/inputs" / (args.input + ".eeprom")
    if args.eeprom:
        eep = pathlib.Path(args.eeprom)
    if not inp.is_file():
        print(f"romshot: no such stream: {inp}", file=sys.stderr)
        return 2
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    state = {"fb": None, "w": 0, "h": 0, "pitch": 0, "saved": 0}

    def on_video(data, width, height, pitch):
        state["fb"], state["w"], state["h"], state["pitch"] = \
            data, width, height, pitch

    emu = LibretroEmulator(rom, input_stream=str(inp),
                           eeprom=str(eep) if eep.is_file() else None)
    if not args.no_video:
        emu._on_video_capture = on_video

    # ---- audio oracle ----------------------------------------------------
    # The wrapper already routes retro_set_audio_sample_batch here; nothing
    # about the run changes, because consuming audio feeds nothing back into
    # the core. Replay leaves the hook unset and pays nothing.
    audio_chunks: list[bytes] = []
    audio = {"batches": 0, "frames": 0}
    if args.audio_out:
        def on_audio(data, frames):
            n = int(frames)
            if n <= 0:
                return
            audio_chunks.append(ctypes.string_at(data, n * 4))  # s16 stereo
            audio["batches"] += 1
            audio["frames"] += n
        emu._on_audio_capture = on_audio

    def on_tick(index, frame_counter, ram):
        if index % args.every or state["fb"] is None:
            return
        w, h, pitch = state["w"], state["h"], state["pitch"]
        # The core hands the framebuffer back as a RAW ADDRESS, which ctypes
        # presents as a plain int - not a buffer. string_at reads it.
        raw = ctypes.string_at(state["fb"], pitch * h)
        # XRGB8888 little-endian, so memory order is B,G,R,X. Reading it as
        # X,R,G,B put the unused padding byte in blue and made white snow
        # come out yellow. Slice per channel
        # rather than looping per pixel - 307200 pixels a frame in Python is
        # the difference between a capture and a coffee break.
        rows = []
        for y in range(h):
            line = raw[y * pitch : y * pitch + w * 4]
            row = bytearray(w * 3)
            row[0::3] = line[2::4]
            row[1::3] = line[1::4]
            row[2::3] = line[0::4]
            rows.append(bytes(row))
        p = out / f"f-{index:06d}.ppm"
        with open(p, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (w, h))
            f.write(b"".join(rows))
        state["saved"] += 1

    stats = emu.run(on_tick, lambda ram: 0, max_ticks=args.frames)
    # Read AFTER the run: retro_get_system_av_info is only contractually valid
    # once the game is loaded. Measured 2026-08-28 that parallel_n64 returns
    # the same values before load, so this ordering is insurance rather than a
    # fix - but it costs nothing and the API does not promise the other way.
    av = emu.av_info()
    emu.close()
    print(f"romshot: {stats['frames']} frames, {state['saved']} saved -> {out}")

    if args.audio_out:
        import audiocap

        pcm = b"".join(audio_chunks)
        vframes = stats["frames"] or 1
        # MEASURED rate: stereo frames the core actually produced, over the
        # wall-clock the video frames represent. Deliberately not the declared
        # rate - see --audio-rate.
        per_video = audio["frames"] / vframes
        rate = args.audio_rate or int(round(per_video * av["fps"]))

        cap = audiocap.Capture(
            pcm, rate=rate, endian="little", source="rom",
            meta={
                "rom": rom,
                "rom_sha1": audiocap.sha1_file(rom),
                "core": emu.core_path,
                "core_sha1": audiocap.sha1_file(emu.core_path),
                "input_stream": str(inp),
                "input_sha1": audiocap.sha1_file(inp),
                "eeprom": str(eep) if eep.is_file() else None,
                "eeprom_sha1": audiocap.sha1_file(eep) if eep.is_file() else None,
                "video_frames": stats["frames"],
                "audio_batches": audio["batches"],
                "samples_per_video_frame": round(per_video, 3),
                "declared_fps": av["fps"],
                "declared_sample_rate": av["sample_rate"],
                "measured_rate": int(round(per_video * av["fps"])),
                "git_rev": audiocap.git_rev(ROOT),
                "argv": sys.argv,
            })
        wav = cap.write_wav(str(args.audio_out) + ".wav")
        js = cap.write_json(str(args.audio_out) + ".json")
        m = cap.metrics()
        print(audiocap.format_metrics("rom", m))
        print(f"romshot: core declares {av['sample_rate']}Hz, "
              f"measured {int(round(per_video * av['fps']))}Hz")
        print(f"romshot: {wav}  {js}")
        # The oracle is this harness's own positive control. If the CARTRIDGE
        # is silent the harness is broken, and saying so here stops a later
        # "native produced nothing" from being read as a finding about the
        # native mixer.
        if m["silent"]:
            print("romshot: ORACLE FAILED - the cartridge itself produced no "
                  "audio. Do not treat any comparison against this as valid.",
                  file=sys.stderr)
            return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
