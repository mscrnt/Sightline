"""Playable recorder for the libretro backend.

Records input INSIDE the harness rather than consuming RetroArch's replay
format. That format is positional - it logs the return value of every
retro_input_state call in order - so replaying it correctly requires our
harness to make byte-identical query sequences to RetroArch's, including
whatever RetroArch polls for its own purposes and whichever side of its input
remap it logs. Neither is verifiable without RetroArch's source, and the
experiment showed it: replaying a real recording moved the game (it diverged
from a no-input run at tick 364) but never reproduced the session.

Owning both ends removes the coupling. Record and replay then share one
process, one input format, and one timebase, and the round trip becomes a
property of code we control.

Video is XRGB8888 at 640x480 (measured from retro_get_system_av_info), blitted
straight to an SDL texture. Input comes from SDL, which sees the DualSense
where RetroArch's default udev driver does not - WSL runs no udev daemon.
"""

from __future__ import annotations

import ctypes as C
import struct
import time
from pathlib import Path

from .emu_libretro import (LibretroEmulator, InputFrame, RETRO_DEVICE_JOYPAD,
                           RETRO_DEVICE_ANALOG, BUTTON_MAP, C_BUTTON_BITS,
                           c_stick_axes)

SDL = "libSDL2-2.0.so.0"

SDL_INIT_AUDIO = 0x00000010
SDL_INIT_VIDEO = 0x00000020
SDL_INIT_JOYSTICK = 0x00000200
SDL_WINDOWPOS_CENTERED = 0x2FFF0000
SDL_WINDOW_SHOWN = 0x00000004
SDL_RENDERER_ACCELERATED = 0x00000002
SDL_PIXELFORMAT_ARGB8888 = 0x16362004
SDL_TEXTUREACCESS_STREAMING = 1
SDL_QUIT = 0x100
AUDIO_S16LSB = 0x8010
#: Drop rather than queue past this. WSLg's Pulse server will happily buffer
#: seconds of audio, which drifts further behind the picture the longer you
#: play; dropping keeps sound roughly in step with what is on screen.
AUDIO_MAX_QUEUED_BYTES = 8192 * 4
#: Frames to measure before opening the device. Long enough for a stable rate,
#: short enough that the silence at the start is not noticeable.
AUDIO_PROBE_FRAMES = 60

# DualSense over hid-generic, axis layout MEASURED with nothing pressed:
#   0 left X   1 left Y   2 right X   5 right Y
#   3 L2, 4 R2 - both rest at -32768, so "pressed" is the positive direction.
AXIS_LEFT_X, AXIS_LEFT_Y = 0, 1
AXIS_RIGHT_X, AXIS_RIGHT_Y = 2, 5
AXIS_L2, AXIS_R2 = 3, 4

BTN_CROSS, BTN_CIRCLE, BTN_SQUARE, BTN_TRIANGLE = 0, 1, 2, 3
BTN_L1, BTN_R1 = 4, 5
BTN_OPTIONS = 9
#: DualSense Create ("Select", left of the trackpad). Recorded into the stream
#: as InputFrame.DBG_SELECT and NEVER sent to the core - the normal ROM cannot
#: see it, the debug ROM is told about it through the mailbox. This is what
#: lets one recording drive both builds. See src/game/sl_romdbg.h.
BTN_CREATE = 8

TRIGGER_ON = 0          # midpoint of a -32768..32767 trigger
STICK_DEADZONE = 6000


class Recorder:
    """Runs the core with a window and a live pad, logging input per frame.

    GoldenEye control style 1.2 Solitaire expects the analog stick to LOOK, so
    the right stick drives the N64 stick and the left stick drives the
    C-buttons. That split is what makes a modern pad play sensibly, and it is
    applied here rather than in an emulator remap so that recording and replay
    cannot disagree about it.
    """

    def __init__(self, rom_path: str, out_path: str, core_path: str | None = None,
                 scale: int = 2, eeprom: str | None = None,
                 snapshot_path: str | None = None):
        # Recording persists progress; replay never does. See eeprom_write.
        self.emu = LibretroEmulator(rom_path, core_path=core_path, eeprom=eeprom,
                                    eeprom_write=True)
        self.snapshot_path = snapshot_path
        self.out = Path(out_path)
        self.out.parent.mkdir(parents=True, exist_ok=True)
        self.scale = scale
        self._sdl = C.CDLL(SDL)
        self._window = None
        self._renderer = None
        self._texture = None
        self._pad = None
        self._stream = None
        self._frame = InputFrame(0)
        self.frames_written = 0
        #: True when the window was closed, as opposed to hitting max_frames.
        #: Both end the loop the same way, and a sweep needs to tell them apart.
        self.quit_requested = False
        self._bind_sdl()

    def _bind_sdl(self) -> None:
        s = self._sdl
        s.SDL_CreateWindow.restype = C.c_void_p
        s.SDL_CreateWindow.argtypes = [C.c_char_p, C.c_int, C.c_int,
                                       C.c_int, C.c_int, C.c_uint]
        s.SDL_CreateRenderer.restype = C.c_void_p
        s.SDL_CreateRenderer.argtypes = [C.c_void_p, C.c_int, C.c_uint]
        s.SDL_CreateTexture.restype = C.c_void_p
        s.SDL_CreateTexture.argtypes = [C.c_void_p, C.c_uint, C.c_int,
                                        C.c_int, C.c_int]
        s.SDL_UpdateTexture.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_int]
        s.SDL_RenderCopy.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p]
        s.SDL_RenderClear.argtypes = [C.c_void_p]
        s.SDL_RenderPresent.argtypes = [C.c_void_p]
        s.SDL_JoystickOpen.restype = C.c_void_p
        s.SDL_JoystickOpen.argtypes = [C.c_int]
        s.SDL_JoystickGetButton.restype = C.c_ubyte
        s.SDL_JoystickGetButton.argtypes = [C.c_void_p, C.c_int]
        s.SDL_JoystickGetAxis.restype = C.c_int16
        s.SDL_JoystickGetAxis.argtypes = [C.c_void_p, C.c_int]
        s.SDL_JoystickGetHat.restype = C.c_ubyte
        s.SDL_JoystickGetHat.argtypes = [C.c_void_p, C.c_int]
        s.SDL_JoystickNameForIndex.restype = C.c_char_p
        s.SDL_OpenAudioDevice.restype = C.c_uint
        s.SDL_OpenAudioDevice.argtypes = [C.c_char_p, C.c_int, C.c_void_p,
                                          C.c_void_p, C.c_int]
        s.SDL_QueueAudio.argtypes = [C.c_uint, C.c_void_p, C.c_uint]
        s.SDL_GetQueuedAudioSize.restype = C.c_uint
        s.SDL_GetQueuedAudioSize.argtypes = [C.c_uint]
        s.SDL_PauseAudioDevice.argtypes = [C.c_uint, C.c_int]
        s.SDL_CloseAudioDevice.argtypes = [C.c_uint]
        s.SDL_DestroyTexture.argtypes = [C.c_void_p]
        s.SDL_Delay.argtypes = [C.c_uint]

    def open(self) -> None:
        s = self._sdl
        if s.SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO) != 0:
            raise RuntimeError("SDL_Init failed")
        w, h = 640 * self.scale, 480 * self.scale
        self._window = s.SDL_CreateWindow(b"Sightline recorder",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          w, h, SDL_WINDOW_SHOWN)
        if not self._window:
            raise RuntimeError("could not create a window")
        self._renderer = s.SDL_CreateRenderer(self._window, -1,
                                              SDL_RENDERER_ACCELERATED)
        # No texture yet. Its size must match what the core actually hands us
        # per frame, which is NOT a constant: hardcoding 640x480 while the core
        # sent half that many rows drew the picture into the top half of the
        # window and left the rest black.
        self._texture = None
        self._tex_w = self._tex_h = 0
        n = s.SDL_NumJoysticks()
        if n > 0:
            self._pad = s.SDL_JoystickOpen(0)
            name = s.SDL_JoystickNameForIndex(0)
            print(f"pad: {name.decode() if name else 'unknown'}")
        else:
            print("pad: none detected - recording keyboard-free input only")
        self._stream = self.out.open("wb")

    # ---- input ----------------------------------------------------------

    def _read_pad(self) -> InputFrame:
        """Build one frame of N64 controller state from the physical pad."""
        if self._pad is None:
            return InputFrame(0)
        s, pad = self._sdl, self._pad
        s.SDL_JoystickUpdate()
        value = 0

        def press(bit):
            nonlocal value
            value |= 1 << bit

        if s.SDL_JoystickGetButton(pad, BTN_CROSS):
            press(InputFrame.A_BUTTON)
        if s.SDL_JoystickGetButton(pad, BTN_R1):
            press(InputFrame.Z_TRIG)          # R1 fires as well as R2
        if s.SDL_JoystickGetButton(pad, BTN_SQUARE):
            press(InputFrame.B_BUTTON)
        if s.SDL_JoystickGetButton(pad, BTN_L1):
            press(InputFrame.L_TRIG)
        if s.SDL_JoystickGetButton(pad, BTN_OPTIONS):
            press(InputFrame.START)
        # The debug oracle's SELECT. _serve_input never forwards it, so
        # pressing it cannot change the run being recorded.
        if s.SDL_JoystickGetButton(pad, BTN_CREATE):
            press(InputFrame.DBG_SELECT)
        # Triggers rest at -32768; pressed is the positive direction.
        if s.SDL_JoystickGetAxis(pad, AXIS_R2) > TRIGGER_ON:
            press(InputFrame.Z_TRIG)
        if s.SDL_JoystickGetAxis(pad, AXIS_L2) > TRIGGER_ON:
            press(InputFrame.R_TRIG)

        # Left stick -> C-buttons (movement in style 1.2).
        lx = s.SDL_JoystickGetAxis(pad, AXIS_LEFT_X)
        ly = s.SDL_JoystickGetAxis(pad, AXIS_LEFT_Y)
        if lx < -STICK_DEADZONE:
            press(InputFrame.L_CBUTTON)
        elif lx > STICK_DEADZONE:
            press(InputFrame.R_CBUTTON)
        if ly < -STICK_DEADZONE:
            press(InputFrame.U_CBUTTON)
        elif ly > STICK_DEADZONE:
            press(InputFrame.D_CBUTTON)

        # D-pad hat.
        hat = s.SDL_JoystickGetHat(pad, 0)
        if hat & 0x01:
            press(InputFrame.U_DPAD)
        if hat & 0x02:
            press(InputFrame.R_DPAD)
        if hat & 0x04:
            press(InputFrame.D_DPAD)
        if hat & 0x08:
            press(InputFrame.L_DPAD)

        # Right stick -> the N64 analog stick (looking), scaled to +/-80.
        rx = s.SDL_JoystickGetAxis(pad, AXIS_RIGHT_X)
        ry = s.SDL_JoystickGetAxis(pad, AXIS_RIGHT_Y)
        ax = 0 if abs(rx) < STICK_DEADZONE else max(-80, min(80, rx * 80 // 32767))
        ay = 0 if abs(ry) < STICK_DEADZONE else max(-80, min(80, -ry * 80 // 32767))
        value |= (ax & 0xFF) << 16
        value |= (ay & 0xFF) << 24
        return InputFrame(value)

    def _serve_input(self, port, device, index, ident):
        """Answer the core from the frame sampled at the top of this frame."""
        """Answer the core from the frame we sampled at the top of this frame."""
        if port != 0:
            return 0
        f = self._frame
        if device == RETRO_DEVICE_JOYPAD:
            for bit, lid in BUTTON_MAP.items():
                if lid == ident:
                    # C-buttons ride the right analog stick, not a button id.
                    return 0 if bit in C_BUTTON_BITS else (1 if f.pressed(bit) else 0)
            return 0
        if device == RETRO_DEVICE_ANALOG:
            if index == 0:                      # LEFT: the N64 analog stick
                if ident == 0:
                    return max(-32767, min(32767, f.x_axis * 409))
                if ident == 1:
                    return max(-32767, min(32767, -f.y_axis * 409))
            elif index == 1:                    # RIGHT: the N64 C-buttons
                cx, cy = c_stick_axes(f)
                return cx if ident == 0 else (cy if ident == 1 else 0)
        return 0

    # ---- main loop -------------------------------------------------------

    def framebuffer(self):
        """The last frame the core handed us, for whoever wants to save it.

        Returned rather than reached for, so the debug-capture path does not
        have to know the recorder's private attribute names.
        """
        return (self._last_frame, self._last_w, self._last_h, self._last_pitch)

    def run(self, on_tick=None, frame_counter_of=None, max_frames: int = 200000,
            pre_tick=None, synth_select=()):
        """Play until the window is closed, logging one 4-byte frame each tick.

        `pre_tick(index, frame, ram)` runs after the pad has been sampled and
        BEFORE the core consumes it. That ordering is the whole point for the
        debug mailbox: the ROM reads its command at the top of its own hook, so
        a poke made after retro_run() answers a frame late.

        `synth_select` is a set of frame indices at which DBG_SELECT is OR'd
        into the recorded input as though the button had been pressed. It is
        the known-positive control for the live feedback path - an instrument
        that cannot be made to fire on demand cannot be shown to work at all -
        and it needs no pad, so it runs under xvfb.
        """
        # Capture the framebuffer the core hands us each frame, so it can be
        # blitted to the window. Without this there is nothing to display.
        def on_video(data, width, height, pitch):
            self._last_frame = data
            self._last_w, self._last_h, self._last_pitch = width, height, pitch
        self.emu._on_video_capture = on_video
        # Hook, NOT a callback re-install: _install_callbacks calls retro_init,
        # and a second init would make recording a structurally different run
        # from replay - which is exactly the round-trip divergence.
        self.emu.input_hook = self._serve_input
        self.emu._load()
        # Snapshot AFTER _load(), never before: the save file is applied during
        # load, so a snapshot taken earlier captures a blank cartridge instead
        # of the one this session actually plays. Replay then starts from
        # different progress and diverges in the menus, which reads as a
        # nondeterministic game rather than the bookkeeping mistake it is.
        if self.snapshot_path:
            blob = self.emu.eeprom_snapshot()
            if blob:
                snap = Path(self.snapshot_path)
                snap.parent.mkdir(parents=True, exist_ok=True)
                snap.write_bytes(blob)
                print(f"  save snapshot: {snap}")
        # Warm-up frame: neutral input, not logged. Replay does the same, so
        # both sides start the recorded stream at the same frame.
        self._frame = InputFrame(0)
        self.emu._core.retro_run()

        # Pace to the core's own refresh rate. retro_run() returns as fast as
        # the CPU manages, which is far quicker than 60Hz and leaves the game
        # unplayable to record against. Pacing is display-side ONLY: the frame
        # is the unit of simulation either way, so sleeping between frames
        # cannot change what gets recorded or how it replays.
        fps = self._core_fps()          # also fills self._sample_rate
        self._fps = fps
        self._open_audio()
        target = 1.0 / fps if fps > 0 else 0.0
        print(f"  pacing to {fps:.2f} fps")

        s = self._sdl
        event = C.create_string_buffer(256)
        frames = 0
        next_due = time.perf_counter()
        while frames < max_frames:
            while s.SDL_PollEvent(event):
                if struct.unpack("<I", event.raw[:4])[0] == SDL_QUIT:
                    self.quit_requested = True
                    frames = max_frames
                    break
            if frames >= max_frames:
                break

            self._frame = self._read_pad()
            if frames in synth_select:
                self._frame = InputFrame(self._frame.value
                                         | (1 << InputFrame.DBG_SELECT))
            # Log BEFORE running: this is the input the frame will consume.
            self._stream.write(struct.pack(">I", self._frame.value))
            self._stream.flush()          # survive any exit, as with mupen
            self.frames_written += 1

            if pre_tick is not None:
                base = self.emu.rdram()
                if base is not None:
                    pre_tick(frames, self._frame, base)

            self.emu._core.retro_run()
            frames += 1

            base = self.emu.rdram()
            if base is not None and on_tick and frame_counter_of:
                on_tick(frames - 1, frame_counter_of(base), base)
            self._present()

            # Sleep off whatever is left of this frame's budget. Falling behind
            # does not accumulate debt - resync instead, or a slow patch would
            # make the game sprint to catch up.
            next_due += target
            slack = next_due - time.perf_counter()
            if slack > 0:
                s.SDL_Delay(int(slack * 1000))
            elif slack < -0.25:
                next_due = time.perf_counter()
        return {"frames": frames, "logged": self.frames_written}

    def _open_audio(self) -> None:
        """Start measuring the core's real output rate; open once it is known.

        The rate the core DECLARES is not the rate it produces. parallel_n64
        reports 32040 Hz while GoldenEye emits ~367 samples a frame, which is
        about 22050 - so a device opened at the declared rate drains half again
        faster than it is fed and crackles constantly. Measured: 44% of batches
        arrived to an empty queue at 32040, against 1% at 22050.

        RetroArch hides this with a resampler. Measuring is simpler and needs no
        resampling: ask the core what it actually emits, then match it.
        """
        self._audio_probe = {"samples": 0, "frames": 0}
        self.emu._on_audio_capture = self._measure_then_play

    def _measure_then_play(self, data, frames) -> None:
        """Count a short window, open the device at that rate, then play."""
        probe = self._audio_probe
        probe["samples"] += int(frames)
        probe["frames"] += 1
        if probe["frames"] < AUDIO_PROBE_FRAMES:
            return                      # still measuring; these are discarded
        fps = getattr(self, "_fps", 60.0) or 60.0
        rate = int(round(probe["samples"] / probe["frames"] * fps))
        self._start_audio(rate)
        self.emu._on_audio_capture = self._queue_audio

    def _start_audio(self, rate: int) -> None:
        """Open the device. Best effort - silence must never stop a take."""
        class Spec(C.Structure):
            _fields_ = [("freq", C.c_int), ("format", C.c_uint16),
                        ("channels", C.c_uint8), ("silence", C.c_uint8),
                        ("samples", C.c_uint16), ("padding", C.c_uint16),
                        ("size", C.c_uint32), ("callback", C.c_void_p),
                        ("userdata", C.c_void_p)]

        want = Spec(freq=rate, format=AUDIO_S16LSB, channels=2,
                    samples=1024, callback=None, userdata=None)
        got = Spec()
        dev = self._sdl.SDL_OpenAudioDevice(None, 0, C.byref(want),
                                            C.byref(got), 0)
        if not dev:
            print("  audio unavailable - recording silently")
            return
        self._audio_dev = dev
        self._sdl.SDL_PauseAudioDevice(dev, 0)
        declared = getattr(self, "_sample_rate", 0)
        note = f" (core declares {declared})" if declared and declared != rate else ""
        print(f"  audio: {rate} Hz{note}")

    def _queue_audio(self, data, frames) -> None:
        dev = self._audio_dev
        if not dev:
            return
        if self._sdl.SDL_GetQueuedAudioSize(dev) > AUDIO_MAX_QUEUED_BYTES:
            return                      # behind; drop rather than drift
        self._sdl.SDL_QueueAudio(dev, data, int(frames) * 4)   # s16 stereo

    def _ensure_texture(self, w: int, h: int) -> None:
        """(Re)create the texture when the core changes output size."""
        if self._texture is not None and (w, h) == (self._tex_w, self._tex_h):
            return
        s = self._sdl
        if self._texture is not None:
            s.SDL_DestroyTexture(self._texture)
        self._texture = s.SDL_CreateTexture(self._renderer,
                                            SDL_PIXELFORMAT_ARGB8888,
                                            SDL_TEXTUREACCESS_STREAMING, w, h)
        self._tex_w, self._tex_h = w, h

    def _present(self) -> None:
        if self._last_frame is None or not self._last_w or not self._last_h:
            return
        s = self._sdl
        self._ensure_texture(self._last_w, self._last_h)
        s.SDL_UpdateTexture(self._texture, None, self._last_frame,
                            self._last_pitch)
        s.SDL_RenderClear(self._renderer)
        # NULL src and dst: SDL scales the frame to the window, so the aspect
        # follows whatever the core emitted rather than an assumption here.
        s.SDL_RenderCopy(self._renderer, self._texture, None, None)
        s.SDL_RenderPresent(self._renderer)

    _last_frame = None
    _audio_dev = 0
    _audio_probe = None
    _fps = 60.0
    _last_w = _last_h = 0
    _last_pitch = 0

    def _core_fps(self) -> float:
        """Refresh rate the core declares, and cache the declared audio rate.

        The retro_get_system_av_info call itself lives on LibretroEmulator,
        which is where core metadata belongs and where romshot.py reads it
        from too - one reader, so the two cannot drift.
        """
        info = self.emu.av_info()
        self._sample_rate = info["sample_rate"]
        return info["fps"]

    def close(self) -> None:
        if self._audio_dev:
            self._sdl.SDL_CloseAudioDevice(self._audio_dev)
            self._audio_dev = 0
        if self._stream:
            self._stream.close()
        try:
            self.emu.close()
        finally:
            self._sdl.SDL_Quit()
