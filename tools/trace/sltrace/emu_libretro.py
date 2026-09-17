"""libretro backend for the trace harness.

Replaces the mupen64plus driver, which proved intermittently nondeterministic:
roughly one replay in four reproduced, with the divergence point wandering
between ticks 789 and 1416 depending on configuration (see docs/backlog.md
B-005). parallel_n64 under libretro reproduces byte-identically with no tuning
at all - 5 runs at 600 frames and 3 at 2000, all identical.

The structural reason this backend is better: retro_run() advances EXACTLY one
frame. There is no sampling point to choose, which deletes the whole class of
bug that cost a session - VI-boundary jitter, breakpoint plumbing, and
boot-phase filtering all become unnecessary. There is also no plugin ecosystem
whose timing can leak into the simulation, no speed limiter, and no dynarec.

Measured facts this file depends on:
  * The RAM pointer is NULL until after the first retro_run(); cores allocate
    lazily. Fetch it after one frame, never at load.
  * SYSTEM_RAM holds 32-bit words in host order, exactly like mupen64plus's
    debug pointer, so reads must swap.
  * Input is edge-sensitive. A button held from frame zero has NO effect -
    21744 presses returned once with a hash byte-identical to no input at all.
    Replaying recorded per-frame state produces edges naturally; synthesising a
    constant hold does not.
"""

from __future__ import annotations

import ctypes as C
import os
import sys
from pathlib import Path

#: Where a core is looked for when SL_LIBRETRO_CORE is unset, and what it is
#: called. Both are platform-dependent, and hardcoding the Linux pair made the
#: backend unloadable on Windows even though the CODE is portable - ctypes.CDLL
#: loads the .dll perfectly well. Measured 2026-09-03: exporting
#: SL_LIBRETRO_CORE at a win64 parallel_n64_libretro.dll was the ONLY change
#: needed to boot the retail ROM from ordinary Windows Python.
#:
#: Nothing here is committed or vendored - the core is an external dependency
#: that stays outside git (project rule 2), so this only ever
#: DISCOVERS one. The owner's absolute home directory is never written down:
#: the default is derived from Path.home() at runtime.
if sys.platform == "win32":
    CORE_NAME = "parallel_n64_libretro.dll"
    CORE_DIR = Path(
        os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local")
    ) / "sightline/libretro"
else:
    CORE_NAME = "parallel_n64_libretro.so"
    CORE_DIR = Path.home() / ".config/retroarch/cores"

DEFAULT_CORE = os.environ.get("SL_LIBRETRO_CORE", str(CORE_DIR / CORE_NAME))


def default_system_dir() -> Path:
    """Scratch directory the core is handed for its system/save files.

    "/tmp/sl-libretro" resolved to a top-level "tmp" directory on whatever
    drive happened to be current under Windows. It worked, but planting a
    directory at the root of the owner's drive is not something a tool
    should do uninvited.
    """
    if sys.platform == "win32":
        base = Path(os.environ.get("TEMP", Path.home() / "AppData/Local/Temp"))
        return base / "sl-libretro"
    return Path("/tmp/sl-libretro")

RETRO_MEMORY_SAVE_RAM = 0
RETRO_MEMORY_SYSTEM_RAM = 2
RETRO_DEVICE_JOYPAD = 1
RETRO_DEVICE_ANALOG = 5
RETRO_DEVICE_INDEX_ANALOG_LEFT = 0
RETRO_DEVICE_INDEX_ANALOG_RIGHT = 1
RETRO_DEVICE_ID_ANALOG_X = 0
RETRO_DEVICE_ID_ANALOG_Y = 1

# Environment commands we must answer for the core to initialise and run.
ENV_GET_CAN_DUPE = 3
ENV_SET_PERFORMANCE_LEVEL = 8
ENV_GET_SYSTEM_DIRECTORY = 9
ENV_SET_PIXEL_FORMAT = 10
ENV_SET_INPUT_DESCRIPTORS = 11
ENV_GET_VARIABLE = 15
ENV_SET_VARIABLES = 16
ENV_GET_VARIABLE_UPDATE = 17
ENV_SET_SUPPORT_NO_GAME = 18
ENV_GET_LOG_INTERFACE = 27
ENV_GET_SAVE_DIRECTORY = 31
ENV_SET_SYSTEM_AV_INFO = 32
ENV_SET_CONTROLLER_INFO = 35
ENV_SET_GEOMETRY = 37
ENV_GET_LANGUAGE = 39
ENV_GET_CORE_OPTIONS_VERSION = 52
ENV_SET_CORE_OPTIONS = 53
ENV_SET_CORE_OPTIONS_INTL = 54
ENV_SET_CORE_OPTIONS_V2 = 67
ENV_SET_CORE_OPTIONS_V2_INTL = 68

# GoldenEye probes the controller pak. Under mupen64plus a MEMPAK/NONE mismatch
# between record and replay diverged at tick 1 while tick and VI counts still
# matched exactly - which looks nothing like an input problem. parallel_n64
# defaults these to "none", so pin them identically for record and replay.
CORE_OPTIONS = {
    b"parallel-n64-pak1": b"memory",
    b"parallel-n64-pak2": b"none",
    b"parallel-n64-pak3": b"none",
    b"parallel-n64-pak4": b"none",
}

ENVCB = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VIDEOCB = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AUDIOCB = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
AUDIOBCB = C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t)
POLLCB = C.CFUNCTYPE(None)
STATECB = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)


class GameInfo(C.Structure):
    _fields_ = [("path", C.c_char_p), ("data", C.c_void_p),
                ("size", C.c_size_t), ("meta", C.c_char_p)]


class BsvReplay:
    """RetroArch replay (BSV2) playback.

    Layout, established by inspecting a recording RetroArch itself plays back
    faithfully:

        0x00  "2VSB" magic
        0x0c  savestate size, little-endian u32
        0x18  savestate ("M64+SAVE" for this core)
        ...   input log: the int16 return value of EVERY retro_input_state
              call, in call order

    Playback is therefore just "return the next sample on each query". That
    works because the core is deterministic: replaying the same savestate and
    feeding the same answers reproduces the same sequence of queries. It also
    means the harness must not add or skip a single input query, or everything
    after that point is shifted.
    """

    MAGIC = b"2VSB"

    def __init__(self, path):
        raw = Path(path).read_bytes()
        if raw[:4] != self.MAGIC:
            raise ValueError(f"{path}: not a RetroArch replay (magic {raw[:4]!r})")
        state_size = int.from_bytes(raw[0x0C:0x10], "little")
        start = 0x18 + state_size
        if not (0 < state_size < len(raw)):
            raise ValueError(f"{path}: implausible savestate size {state_size}")
        self.savestate = raw[0x18:start]
        body = raw[start:]
        self.samples = [int.from_bytes(body[i:i + 2], "little", signed=True)
                        for i in range(0, len(body) - 1, 2)]
        self.cursor = 0
        self.exhausted = False
        self.swap_analog = False
        self._analog_buf: list[int] = []

    def next_sample_swapped_analog(self, device: int, index: int, ident: int) -> int:
        """Serve a sample, optionally swapping the two analog sticks.

        The recording was made with a RetroArch remap that swaps the sticks
        (pickme.rmp). If the replay log stores input BEFORE that remap is
        applied, feeding it raw gives the game unswapped sticks - it responds,
        but moves wrongly and never navigates the menus, which is exactly what
        the trace showed.

        The core's per-frame query order for a port is fixed and measured:
        16 joypad ids, then analog RIGHT X, RIGHT Y, LEFT X, LEFT Y. So the
        swap is positional: buffer those four and serve them as LEFT X, LEFT Y,
        RIGHT X, RIGHT Y.
        """
        if not self.swap_analog or device != 5:
            return self.next_sample()
        if not self._analog_buf:
            quad = [self.next_sample() for _ in range(4)]   # RX RY LX LY
            self._analog_buf = [quad[2], quad[3], quad[0], quad[1]]
        return self._analog_buf.pop(0)

    def next_sample(self) -> int:
        if self.cursor >= len(self.samples):
            self.exhausted = True
            return 0
        v = self.samples[self.cursor]
        self.cursor += 1
        return v


class InputFrame:
    """One frame of N64 controller state.

    Mirrors the BUTTONS union the recordings already use: 16 button bits plus
    two signed 8-bit axes, packed big-endian into 4 bytes.
    """

    __slots__ = ("value",)

    # Bit positions within BUTTONS.Value (little-endian bitfield order).
    R_DPAD, L_DPAD, D_DPAD, U_DPAD = 0, 1, 2, 3
    START, Z_TRIG, B_BUTTON, A_BUTTON = 4, 5, 6, 7
    R_CBUTTON, L_CBUTTON, D_CBUTTON, U_CBUTTON = 8, 9, 10, 11
    R_TRIG, L_TRIG = 12, 13
    #: Bits 14 and 15 are the two spare bits of the N64 button word (0x0040,
    #: the unused one, and 0x0080, reset). Bit 14 is reserved for the debug
    #: oracle's SELECT and is DELIBERATELY absent from BUTTON_MAP below, so it
    #: is recorded in the stream and never handed to the core.
    #:
    #: It cannot be handed to the core: the N64 has no Select button and this
    #: core's mapping reaches only real N64 buttons. And it must not be, even
    #: if it could - cheat.c:879 reads joyGetButtons(ANY_BUTTON) during
    #: gameplay and writes the raw pressed-button word into
    #: g_CurrentPlayer->cheatInputBuffer, so ANY bit changes player state.
    #: tools/native/rominspect.py delivers it to the debug ROM through the
    #: mailbox instead. See src/game/sl_romdbg.h.
    DBG_SELECT = 14

    def __init__(self, value: int = 0):
        self.value = value

    @classmethod
    def from_bytes(cls, raw: bytes) -> "InputFrame":
        return cls(int.from_bytes(raw, "big"))

    def pressed(self, bit: int) -> bool:
        return bool(self.value & (1 << bit))

    @property
    def x_axis(self) -> int:
        v = (self.value >> 16) & 0xFF
        return v - 256 if v > 127 else v

    @property
    def y_axis(self) -> int:
        v = (self.value >> 24) & 0xFF
        return v - 256 if v > 127 else v


# N64 button -> libretro RETRO_DEVICE_ID_JOYPAD id.
# The C-buttons have no direct libretro equivalent; the community convention
# maps them onto the face/shoulder buttons, which is what RetroArch's own N64
# remaps use.
# MEASURED against the running game, not guessed: each libretro id was held in
# isolation and the N64 button word the game received (via g_ContDataPtr) was
# read back. Two earlier guesses at this table were wrong in ways that were
# invisible until someone played - A sat on id 8, which this core ignores
# entirely, so "use" and "reload" simply did not exist.
#   id 0 -> A   id 1 -> B   id 3 -> START   id 4/5 -> D-Up/D-Down   id 12 -> Z
# C-buttons are NOT here; they arrive on the right analog stick (c_stick_axes).
BUTTON_MAP = {
    InputFrame.A_BUTTON: 0,    # A - use / reload
    InputFrame.B_BUTTON: 1,    # B
    InputFrame.Z_TRIG: 12,     # L2
    InputFrame.START: 3,       # START
    InputFrame.U_DPAD: 4,      # UP
    InputFrame.D_DPAD: 5,      # DOWN
    InputFrame.L_DPAD: 6,      # LEFT
    InputFrame.R_DPAD: 7,      # RIGHT
    InputFrame.L_TRIG: 10,     # L
    InputFrame.R_TRIG: 11,     # R
    # No C-buttons: see c_stick_axes. Leaving them here was not merely dead
    # weight - the guessed id for C-Down was 1, which is really N64 B, so
    # pressing left-stick-down reloaded the gun instead of stepping backwards.
}
LIBRETRO_TO_N64 = {v: k for k, v in BUTTON_MAP.items()}

# C-buttons do NOT arrive as joypad buttons. ParaLLEl N64 exposes them as the
# core's RIGHT analog stick, which is why sending them as button ids did
# nothing at all - the left thumbstick appeared dead and weapon cycling and
# reload went with it. Confirmed against the user's own working RetroArch
# remap, which drives stk_r from the physical left stick.
#
# They stay DIGITAL in the recorded frame: the N64 C-buttons are digital, the
# 4-byte input format has no room for a second stick, and full deflection is
# what a pressed C-button means. Record and replay both call this, so the two
# sides cannot disagree about the synthesis - which would break the round trip.
C_STICK_FULL = 32767


def c_stick_axes(frame: "InputFrame") -> tuple[int, int]:
    """(x, y) for the core's right stick, from the frame's C-button bits."""
    x = y = 0
    if frame.pressed(InputFrame.R_CBUTTON):
        x = C_STICK_FULL
    elif frame.pressed(InputFrame.L_CBUTTON):
        x = -C_STICK_FULL
    if frame.pressed(InputFrame.D_CBUTTON):
        y = C_STICK_FULL          # libretro Y is positive downward
    elif frame.pressed(InputFrame.U_CBUTTON):
        y = -C_STICK_FULL
    return x, y


#: C-buttons are served on the analog stick above, so answering them again as
#: joypad ids would be a second, redundant press - and one of the ids guessed
#: for them was SELECT, which is not an N64 button at all.
C_BUTTON_BITS = frozenset((InputFrame.U_CBUTTON, InputFrame.D_CBUTTON,
                           InputFrame.L_CBUTTON, InputFrame.R_CBUTTON))


class SecondCoreError(RuntimeError):
    """Raised when a process tries to build a second emulator."""


class LibretroEmulator:
    """Drives a libretro core one frame at a time.

    ONE PER PROCESS. dlopen returns the same library instance for a path that
    is already loaded, so a second LibretroEmulator does not get a fresh core -
    it inherits the first one's globals, mid-run and possibly already deinit'd.
    Nothing errors; the second run simply starts wherever the first stopped.

    That cost a long debugging session: an in-process record-then-replay test
    reported the round trip as diverged, and it stayed diverged through three
    unrelated "fixes", because replay was resuming the recorder's core at frame
    386 with the counter frozen. Record and replay each in their own process.
    """

    #: Set once a core is loaded in this process; never cleared, because close()
    #: does not unload the library or reset its globals.
    _process_has_core = False

    def __init__(self, rom_path: str, core_path: str | None = None,
                 system_dir: str | None = None, verbose: bool = False,
                 input_stream: str | None = None,
                 replay_file: str | None = None,
                 eeprom: str | None = None, eeprom_write: bool = False):
        if LibretroEmulator._process_has_core:
            raise SecondCoreError(
                "a libretro core is already loaded in this process.\n"
                "dlopen would hand back the SAME core, still holding the first "
                "run's state, so this run would silently resume it instead of "
                "booting.\n"
                "Run each record/replay in its own process "
                "(subprocess.run([sys.executable, ...]))."
            )
        LibretroEmulator._process_has_core = True
        self.rom_path = rom_path
        self.eeprom_path = eeprom
        # Writing the save back is OPT-IN, and replay must never do it. The
        # game saves progress as you play, so a replay that stores its result
        # overwrites the very snapshot it was replaying - the first run passes,
        # every run after it starts from different progress and diverges. The
        # recording end wants persistence; a replay is strictly a reader.
        self.eeprom_write = eeprom_write
        self.core_path = core_path or DEFAULT_CORE
        self.verbose = verbose
        self._sysdir = C.c_char_p(
            str(system_dir or default_system_dir()).encode())
        Path(self._sysdir.value.decode()).mkdir(parents=True, exist_ok=True)

        self._bsv = BsvReplay(replay_file) if replay_file else None
        if self._bsv is not None and os.environ.get("SL_SWAP_ANALOG", "0") == "1":
            self._bsv.swap_analog = True
        self._frames: list[InputFrame] = []
        if input_stream:
            raw = Path(input_stream).read_bytes()
            self._frames = [InputFrame.from_bytes(raw[i:i + 4])
                            for i in range(0, len(raw) - 3, 4)]

        self._frame_index = 0
        # The warm-up frame this core needs must consume NO recorded input.
        # During recording that frame ran with neutral input, so if replay lets
        # it eat frames[0] the whole stream is applied one frame early and the
        # round trip diverges. Same trap as the BSV cursor.
        self._warming_up = False
        # A recorder installs a hook here to supply live input. Using a hook
        # rather than re-installing callbacks matters: _install_callbacks calls
        # retro_init, and calling it twice makes the recorder's core a
        # structurally different run from replay's, which breaks the round trip.
        self.input_hook = None
        self._rdram = None
        self._opt_bufs: dict[bytes, C.c_char_p] = {}
        self._core = C.CDLL(self.core_path)
        self._bind()
        self._install_callbacks()

    # ---- core plumbing ---------------------------------------------------

    def _bind(self) -> None:
        c = self._core
        c.retro_load_game.restype = C.c_bool
        c.retro_get_memory_data.restype = C.c_void_p
        c.retro_get_memory_data.argtypes = [C.c_uint]
        c.retro_get_memory_size.restype = C.c_size_t
        c.retro_get_memory_size.argtypes = [C.c_uint]
        c.retro_set_controller_port_device.argtypes = [C.c_uint, C.c_uint]

    def _on_env(self, cmd: int, data: int) -> bool:
        if cmd == ENV_SET_PIXEL_FORMAT:
            return True
        if cmd == ENV_GET_CAN_DUPE:
            C.cast(data, C.POINTER(C.c_bool))[0] = True
            return True
        if cmd in (ENV_GET_SYSTEM_DIRECTORY, ENV_GET_SAVE_DIRECTORY):
            C.cast(data, C.POINTER(C.c_char_p))[0] = self._sysdir
            return True
        if cmd == ENV_GET_VARIABLE:
            var = C.cast(data, C.POINTER(C.c_char_p * 2))
            key = var[0][0]
            if key in CORE_OPTIONS:
                # Keep the buffer alive; the core holds the pointer.
                self._opt_bufs[key] = C.c_char_p(CORE_OPTIONS[key])
                var[0][1] = self._opt_bufs[key].value
                return True
            return False
        if cmd == ENV_GET_VARIABLE_UPDATE:
            C.cast(data, C.POINTER(C.c_bool))[0] = False
            return True
        if cmd == ENV_GET_CORE_OPTIONS_VERSION:
            C.cast(data, C.POINTER(C.c_uint))[0] = 0
            return True
        if cmd == ENV_GET_LANGUAGE:
            C.cast(data, C.POINTER(C.c_uint))[0] = 0
            return True
        if cmd in (ENV_SET_VARIABLES, ENV_SET_CORE_OPTIONS,
                   ENV_SET_CORE_OPTIONS_INTL, ENV_SET_CORE_OPTIONS_V2,
                   ENV_SET_CORE_OPTIONS_V2_INTL, ENV_SET_PERFORMANCE_LEVEL,
                   ENV_SET_SUPPORT_NO_GAME, ENV_SET_GEOMETRY,
                   ENV_SET_SYSTEM_AV_INFO, ENV_SET_INPUT_DESCRIPTORS,
                   ENV_SET_CONTROLLER_INFO):
            return True
        return False

    def _on_input_state(self, port: int, device: int, index: int,
                        ident: int) -> int:
        """Answer from the recording; zero when there is none."""
        if self._bsv is not None:
            # BSV2 logs every query's answer in order, so consume in order.
            return self._bsv.next_sample_swapped_analog(device, index, ident)
        if self.input_hook is not None:
            return self.input_hook(port, device, index, ident)
        if self._warming_up:
            return 0
        if port != 0 or self._frame_index >= len(self._frames):
            return 0
        frame = self._frames[self._frame_index]
        if device == RETRO_DEVICE_JOYPAD:
            bit = LIBRETRO_TO_N64.get(ident)
            if bit is None or bit in C_BUTTON_BITS:
                return 0
            return 1 if frame.pressed(bit) else 0
        if device == RETRO_DEVICE_ANALOG:
            if index == RETRO_DEVICE_INDEX_ANALOG_LEFT:
                # N64 axes are +/-80 at saturation; libretro wants +/-32767.
                if ident == RETRO_DEVICE_ID_ANALOG_X:
                    return max(-32767, min(32767, int(frame.x_axis * 409)))
                if ident == RETRO_DEVICE_ID_ANALOG_Y:
                    return max(-32767, min(32767, int(-frame.y_axis * 409)))
            elif index == RETRO_DEVICE_INDEX_ANALOG_RIGHT:
                cx, cy = c_stick_axes(frame)
                if ident == RETRO_DEVICE_ID_ANALOG_X:
                    return cx
                if ident == RETRO_DEVICE_ID_ANALOG_Y:
                    return cy
        return 0

    def _install_callbacks(self) -> None:
        self._cb_env = ENVCB(self._on_env)
        # A recorder can install _on_video_capture to receive each frame.
        def _video(d, w, h, pitch):
            hook = getattr(self, "_on_video_capture", None)
            if hook is not None and d:
                hook(d, w, h, pitch)
        self._cb_video = VIDEOCB(_video)
        self._cb_audio = AUDIOCB(lambda l, r: None)

        # A recorder can install _on_audio_capture to hear the game. Replay
        # leaves it unset, so a gate run stays silent and pays nothing for it.
        # Consuming audio cannot affect determinism any more than drawing the
        # frame does - nothing here feeds back into the simulation.
        def _audio(data, frames):
            hook = getattr(self, "_on_audio_capture", None)
            if hook is not None and data:
                hook(data, frames)
            return frames
        self._cb_audio_batch = AUDIOBCB(_audio)
        self._cb_poll = POLLCB(lambda: None)
        self._cb_state = STATECB(self._on_input_state)
        c = self._core
        c.retro_set_environment(self._cb_env)
        c.retro_set_video_refresh(self._cb_video)
        c.retro_set_audio_sample(self._cb_audio)
        c.retro_set_audio_sample_batch(self._cb_audio_batch)
        c.retro_set_input_poll(self._cb_poll)
        c.retro_set_input_state(self._cb_state)
        c.retro_init()

    def _restore_state(self) -> None:
        """Load the savestate the replay was recorded from."""
        if self._bsv is None:
            return
        blob = self._bsv.savestate
        self._core.retro_unserialize.restype = C.c_bool
        self._core.retro_unserialize.argtypes = [C.c_void_p, C.c_size_t]
        buf = C.create_string_buffer(blob, len(blob))
        if not self._core.retro_unserialize(C.cast(buf, C.c_void_p), len(blob)):
            raise RuntimeError("core rejected the replay's savestate")
        # Rewind the input log. The warm-up frame this core needs before
        # unserialize will accept a state makes ~61 input queries, each of which
        # consumed a sample RetroArch never allocated to it - shifting the whole
        # stream and desyncing the replay completely. The savestate IS the
        # recording's start, so input must start at sample 0.
        self._bsv.cursor = 0
        self._bsv.exhausted = False
        self._bsv._analog_buf = []

    def _load(self) -> None:
        data = Path(self.rom_path).read_bytes()
        self._rom_buf = C.create_string_buffer(data, len(data))
        gi = GameInfo(path=str(self.rom_path).encode(),
                      data=C.cast(self._rom_buf, C.c_void_p),
                      size=len(data), meta=None)
        if not self._core.retro_load_game(C.byref(gi)):
            raise RuntimeError(f"core refused the ROM: {self.rom_path}")
        self._core.retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD)
        self._eeprom_load()

    # ---- persistent save ---------------------------------------------------
    #
    # Without this the cartridge save is blank on EVERY run, so the game is
    #永 brand new: only the first mission is available and nothing you play
    # is ever remembered. That blocks recording later levels at all, and it is
    # invisible until you try - the game simply looks like a fresh cartridge.
    #
    # The whole save region is copied verbatim rather than the EEPROM alone.
    # The core exposes one blob covering SRAM/FlashRAM/EEPROM, and treating it
    # as opaque means nothing here depends on which of those this cartridge
    # uses or where inside the blob it lives.

    def _save_ram(self):
        ptr = self._core.retro_get_memory_data(RETRO_MEMORY_SAVE_RAM)
        size = self._core.retro_get_memory_size(RETRO_MEMORY_SAVE_RAM)
        if not ptr or not size:
            return None, 0
        return ptr, size

    def _eeprom_load(self) -> None:
        if not self.eeprom_path:
            return
        src = Path(self.eeprom_path)
        if not src.exists():
            return
        ptr, size = self._save_ram()
        if not ptr:
            return
        blob = src.read_bytes()
        if len(blob) != size:
            # Refuse rather than partially apply. A truncated save would load
            # as subtly corrupt progress, which is far worse to diagnose than
            # a blank one.
            raise ValueError(
                f"save file {src} is {len(blob)} bytes, core expects {size}")
        C.memmove(ptr, blob, size)

    def eeprom_snapshot(self) -> bytes:
        """The save region as it stands right now."""
        ptr, size = self._save_ram()
        if not ptr:
            return b""
        return bytes(C.cast(ptr, C.POINTER(C.c_ubyte))[:size])

    def eeprom_store(self) -> None:
        if not self.eeprom_path or not self.eeprom_write:
            return
        blob = self.eeprom_snapshot()
        if blob:
            out = Path(self.eeprom_path)
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(blob)

    def rdram(self):
        """RDRAM, valid only after the first retro_run()."""
        if self._rdram is None:
            p = self._core.retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)
            if not p:
                return None
            self._rdram = C.cast(p, C.POINTER(C.c_ubyte))
        return self._rdram

    # ---- the run loop ----------------------------------------------------

    def run(self, on_tick, frame_counter_of, max_ticks: int):
        """Advance frame by frame, calling on_tick(index, frame_counter, ram).

        No sampling heuristic: retro_run() is exactly one frame, so every frame
        is a tick and both runs of the same input sample the same instants.
        """
        self._load()
        self._warming_up = True
        # One frame BEFORE restoring. This core initialises lazily: both the
        # RAM pointer and retro_serialize_size are invalid until a frame has
        # run, and retro_unserialize is rejected outright. The frame is
        # discarded - the savestate overwrites whatever it produced.
        self._core.retro_run()
        # The replay was recorded from this savestate, so restore it before
        # feeding any input - otherwise the input log answers a different game
        # than the one it was recorded against.
        self._restore_state()
        self._warming_up = False
        stats = {"ticks": 0, "frames": 0,
                 "input_frames": len(self._frames),
                 "bsv_samples": len(self._bsv.samples) if self._bsv else 0}

        for _ in range(max_ticks):
            self._core.retro_run()
            self._frame_index += 1
            stats["frames"] += 1
            base = self.rdram()
            if base is None:
                continue
            fc = frame_counter_of(base)
            on_tick(stats["ticks"], fc, base)
            stats["ticks"] += 1

        return stats

    def av_info(self) -> dict:
        """What the core DECLARES about its video and audio timing.

        Declared, not measured, and the difference matters: parallel_n64
        reports 32040Hz while GoldenEye actually emits ~22050. Callers that
        need the real rate must derive it from the samples they received -
        see tools/native/audiocap.py. This is metadata for the record.
        """
        class Geometry(C.Structure):
            _fields_ = [("base_width", C.c_uint), ("base_height", C.c_uint),
                        ("max_width", C.c_uint), ("max_height", C.c_uint),
                        ("aspect_ratio", C.c_float)]

        class Timing(C.Structure):
            _fields_ = [("fps", C.c_double), ("sample_rate", C.c_double)]

        class AVInfo(C.Structure):
            _fields_ = [("geometry", Geometry), ("timing", Timing)]

        info = AVInfo()
        self._core.retro_get_system_av_info(C.byref(info))
        return {"fps": float(info.timing.fps),
                "sample_rate": int(info.timing.sample_rate),
                "width": int(info.geometry.base_width),
                "height": int(info.geometry.base_height)}

    def close(self) -> None:
        try:
            self.eeprom_store()
        except Exception as exc:
            print(f"warning: could not write save file: {exc}", file=sys.stderr)
        try:
            self._core.retro_unload_game()
            self._core.retro_deinit()
        except Exception:
            pass
