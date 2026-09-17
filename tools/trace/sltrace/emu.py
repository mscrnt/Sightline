"""Headless mupen64plus driver for the trace harness.

Every non-obvious choice here was established empirically. Changing any of them
without re-running the determinism check is how this harness starts lying.

  * Pure interpreter (R4300Emulator=0). The dynarec is not a determinism
    guarantee.
  * REAL gfx and rsp plugins, not null ones. With null plugins GoldenEye's main
    loop blocks on the graphics pipeline and currentFrameCounter freezes around
    50 forever - the game never runs, and a harness sampling a hung game looks
    exactly like a harness detecting nondeterminism.
  * Sampling on the debugger's VI callback, then filtering to ticks where
    currentFrameCounter advanced. M64CMD_SET_FRAME_CALLBACK is driven by the
    video plugin and never fires in this configuration, so it cannot be used.
  * A display is required because a real gfx plugin is required. Under CI this
    means Xvfb; the resulting state hashes are identical either way (verified).
"""

from __future__ import annotations

import ctypes as C
import os
import re
from pathlib import Path

CORE_SO = "/usr/lib/x86_64-linux-gnu/libmupen64plus.so.2"
PLUGIN_DIR = "/usr/lib/x86_64-linux-gnu/mupen64plus/"
HEADER = "/usr/include/mupen64plus/m64p_types.h"

# Overridable so the renderer/RSP can be swapped when investigating whether
# their timing leaks into the simulation. Both are part of the determinism
# contract and appear in the trace fingerprint.
GFX_PLUGIN = os.environ.get("SL_GFX_PLUGIN", "mupen64plus-video-glide64mk2.so")
# LLE RSP, NOT HLE. This is THE determinism setting.
#
# With mupen64plus-rsp-hle the same input replayed twice produces different
# currentFrameCounter sequences - A: 2828,2830,2832,2834,2836,2839 versus
# B: 2828,2830,2832,2835,2837,2839 - because the game derives frameDelay from
# osGetCount() deltas (frametiming.c) and HLE task completion does not reproduce
# exactly. Swapping only this component makes 1200 ticks byte-identical.
#
# Ruled out first, each by measurement: the sampler, the state schema,
# RandomizeInterrupt, CountPerOp/SiDmaDuration pinning, and the renderer
# (Glide64mk2 and Rice diverge alike). LLE costs speed; an oracle can afford it.
RSP_PLUGIN = os.environ.get("SL_RSP_PLUGIN", "mupen64plus-rsp-z64-hlevideo.so")
SL_INPUT_PLUGIN = str(Path(__file__).resolve().parents[1] / "slinput" / "slinput.so")

M64PLUGIN_RSP, M64PLUGIN_GFX, M64PLUGIN_AUDIO, M64PLUGIN_INPUT = 1, 2, 3, 4
M64TYPE_INT, M64TYPE_BOOL = 1, 3
M64P_DBG_PTR_RDRAM = 1
DBG_RUNSTATE_RUNNING = 2

M64CMD_CORE_STATE_SET = 17
M64CORE_SPEED_LIMITER = 5

M64P_BKP_CMD_ADD_STRUCT = 2
M64P_BKP_FLAG_ENABLED = 0x01
M64P_BKP_FLAG_WRITE = 0x04
DBG_RUNSTATE_PAUSED = 0


class M64pBreakpoint(C.Structure):
    _fields_ = [("address", C.c_uint32),
                ("endaddr", C.c_uint32),
                ("flags", C.c_uint)]


DEBUGCB = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_char_p)
STATECB = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_int)
VOIDCB = C.CFUNCTYPE(None)
UPDCB = C.CFUNCTYPE(None, C.c_uint)


def _parse_enum(name: str) -> dict[str, int]:
    """Read enum values from the header rather than hard-coding them.

    Hand-transcribing m64p_command is a trap: the values are implicit, so a
    miscount shifts every constant by one and produces M64ERR_INVALID_STATE on
    calls that look correct.
    """
    src = Path(HEADER).read_text()
    blk = re.search(r"typedef enum \{(.*?)\} %s;" % name, src, re.S).group(1)
    out, val = {}, 0
    for line in blk.splitlines():
        m = re.match(r"\s*(\w+)\s*(?:=\s*(\d+))?\s*,?", line)
        if not m or not m.group(1).isupper():
            continue
        if m.group(2) is not None:
            val = int(m.group(2))
        out[m.group(1)] = val
        val += 1
    return out


class Emulator:
    """Runs a ROM, invoking a callback once per advancing game tick."""

    def __init__(self, rom_path: str, config_dir: str = "/tmp/m64cfg",
                 data_dir: str = "/usr/share/mupen64plus", verbose: bool = False,
                 input_replay: str | None = None, input_record: str | None = None,
                 load_state: str | None = None, save_state: str | None = None):
        self.rom_path = rom_path
        self.input_replay = input_replay
        self.input_record = input_record
        # A savestate gives record and replay an identical starting point, which
        # removes menu navigation from both. It also makes verification cheap:
        # without it, replaying a level means re-watching the intro and menus at
        # emulated speed before any gameplay happens.
        self.load_state = load_state
        self.save_state = save_state
        self.cmd = _parse_enum("m64p_command")
        self.verbose = verbose
        self._plugins: list = []
        self._rdram = None
        self._core = C.CDLL(CORE_SO)
        self._bind()
        self._startup(config_dir, data_dir)

    def _bind(self):
        c = self._core
        c.CoreStartup.argtypes = [C.c_int, C.c_char_p, C.c_char_p, C.c_void_p,
                                  DEBUGCB, C.c_void_p, STATECB]
        c.CoreDoCommand.argtypes = [C.c_int, C.c_int, C.c_void_p]
        c.CoreAttachPlugin.argtypes = [C.c_int, C.c_void_p]
        c.DebugMemGetPointer.restype = C.c_void_p
        c.DebugMemGetPointer.argtypes = [C.c_int]
        c.DebugSetCallbacks.argtypes = [VOIDCB, UPDCB, VOIDCB]
        c.DebugSetRunState.argtypes = [C.c_int]
        c.DebugBreakpointCommand.argtypes = [C.c_int, C.c_uint,
                                             C.POINTER(M64pBreakpoint)]
        c.DebugBreakpointCommand.restype = C.c_int
        c.DebugStep.restype = C.c_int

    def _startup(self, config_dir: str, data_dir: str):
        def on_debug(ctx, level, msg):
            if self.verbose and level <= 2:
                print("core:", msg.decode(errors="replace"))

        self._dbg_cb = DEBUGCB(on_debug)
        self._state_cb = STATECB(lambda ctx, p, v: None)
        Path(config_dir).mkdir(parents=True, exist_ok=True)
        rc = self._core.CoreStartup(0x020104, config_dir.encode(), data_dir.encode(),
                                    None, self._dbg_cb, None, self._state_cb)
        if rc != 0:
            raise RuntimeError(f"CoreStartup failed rc={rc}")
        h = C.c_void_p()
        self._core.ConfigOpenSection(b"Core", C.byref(h))
        self._core.ConfigSetParameter(h, b"R4300Emulator", M64TYPE_INT,
                                      C.byref(C.c_int(0)))
        self._core.ConfigSetParameter(h, b"EnableDebugger", M64TYPE_BOOL,
                                      C.byref(C.c_int(1)))

        # THE determinism setting. mupen64plus randomizes PI/SI interrupt timing
        # by default ("RandomizeInterrupt = True") to imitate hardware jitter.
        # That makes every run different: the same input replayed twice produced
        # 17610 vs 18772 VI over 10490 ticks and diverged at tick 789.
        #
        # It is set here rather than in the config file so it cannot be lost by
        # a stale or regenerated config - this is not a preference, the harness
        # is meaningless without it.
        self._core.ConfigSetParameter(h, b"RandomizeInterrupt", M64TYPE_BOOL,
                                      C.byref(C.c_int(0)))

        # NOT pinned: CountPerOp=2 and SiDmaDuration=0x900 were tried and made
        # matters worse (881 ticks matched vs 1416 on auto), without making the
        # frame-counter sequence reproducible. Auto uses the per-ROM values from
        # mupen64plus.ini, which are at least consistent. Left on auto until
        # there is evidence a specific value helps.

    def _attach_real(self, ptype: int, soname: str):
        path = soname if soname.startswith("/") else PLUGIN_DIR + soname
        lib = C.CDLL(path, mode=C.RTLD_GLOBAL)
        lib.PluginStartup.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p]
        lib.PluginStartup(C.c_void_p(self._core._handle), None,
                          C.cast(self._dbg_cb, C.c_void_p))
        rc = self._core.CoreAttachPlugin(ptype, C.c_void_p(lib._handle))
        if rc != 0:
            raise RuntimeError(f"attach {path} failed rc={rc}")
        self._plugins.append(lib)

    def rdram(self):
        if self._rdram is None:
            p = self._core.DebugMemGetPointer(M64P_DBG_PTR_RDRAM)
            if not p:
                return None
            self._rdram = C.cast(p, C.POINTER(C.c_ubyte))
        return self._rdram

    def run_on_write(self, on_tick, frame_counter_addr: int, max_ticks: int):
        """Sample at the instruction that writes currentFrameCounter.

        Sampling on the VI callback is NOT frame-deterministic: a frame spans
        several VI interrupts and the counter can advance by more than one
        between them, so "first VI after the counter changed" lands at a varying
        point inside the frame. Two runs then read state a few thousand
        instructions apart and differ in player position and RNG cursor while
        being the same frame - which looks exactly like emulator
        nondeterminism and is not.

        A write breakpoint fires at one fixed instruction every frame, so both
        runs sample the same instant by construction.
        """
        state = {"ticks": 0, "hits": 0, "error": None, "last_fc": None}

        def dbg_init():
            # The debugger works in PHYSICAL addresses; KSEG0 must be masked off.
            phys = frame_counter_addr & 0x1FFFFFFF
            bkp = M64pBreakpoint(address=phys,
                                 endaddr=phys + 3,
                                 flags=M64P_BKP_FLAG_ENABLED | M64P_BKP_FLAG_WRITE)
            rc = self._core.DebugBreakpointCommand(
                M64P_BKP_CMD_ADD_STRUCT, 0, C.byref(bkp))
            if rc < 0:
                state["error"] = f"could not set breakpoint rc={rc}"
            self._core.DebugSetRunState(DBG_RUNSTATE_RUNNING)

        def dbg_update(pc):
            state["hits"] += 1
            base = self.rdram()
            if base is not None and state["error"] is None:
                try:
                    fc = frame_counter_of_addr(base)
                    if fc != state["last_fc"] and _frame_is_settled(state, fc):
                        state["last_fc"] = fc
                        on_tick(state["ticks"], fc, base)
                        state["ticks"] += 1
                        # Savestate periodically, not at exit: the window gets
                        # closed, so anything deferred to shutdown never runs.
                        # This lived only in run() and was lost when recording
                        # moved to this sampler.
                        if (self.save_state and state["ticks"] % 600 == 0):
                            self._core.CoreDoCommand(
                                self.cmd["M64CMD_STATE_SAVE"], 1,
                                C.c_char_p(self.save_state.encode()))
                except BaseException as e:
                    import traceback
                    state["error"] = "".join(traceback.format_exception(e))
            if state["ticks"] >= max_ticks or state["error"]:
                self._core.CoreDoCommand(self.cmd["M64CMD_STOP"], 0, None)
                return
            # Resume by setting the run state and returning. Calling DebugStep()
            # here re-enters the core from inside its own callback and segfaults.
            self._core.DebugSetRunState(DBG_RUNSTATE_RUNNING)

        def _frame_is_settled(st, fc):
            """Reject writes made before the frame counter is a frame counter.

            The breakpoint fires on any write to that address, including during
            boot before the variable is initialised - observed values there are
            1427584, 51328, 128, 0. Sampling then hashes a player struct that
            does not exist yet and a chr table that is not populated, producing
            divergences that have nothing to do with the game.

            Accept only a plausible, monotonically advancing counter, resetting
            if it drops (a level load legitimately restarts it).
            """
            last = st.get("accepted_fc")
            if fc < 0 or fc > 10_000_000:
                return False
            if last is None:
                st["accepted_fc"] = fc
                return fc < 1000            # only start from an early frame
            if fc < last:                   # counter restarted
                st["accepted_fc"] = fc
                return fc < 1000
            if fc - last > 1000:            # implausible jump
                return False
            st["accepted_fc"] = fc
            return True

        def frame_counter_of_addr(base):
            o = frame_counter_addr - 0x80000000
            return int.from_bytes(bytes(base[o:o + 4])[::-1], "big", signed=True)

        self._ci = VOIDCB(dbg_init)
        self._cu = UPDCB(dbg_update)
        self._cv = VOIDCB(lambda: None)
        self._core.DebugSetCallbacks(self._ci, self._cu, self._cv)
        self._boot_and_execute()

        if state["error"]:
            raise RuntimeError(state["error"])
        return state

    def _boot_and_execute(self):
        data = Path(self.rom_path).read_bytes()
        buf = C.create_string_buffer(data, len(data))
        rc = self._core.CoreDoCommand(self.cmd["M64CMD_ROM_OPEN"], len(data), buf)
        if rc != 0:
            raise RuntimeError(f"ROM_OPEN failed rc={rc}")
        self._attach_real(M64PLUGIN_GFX, GFX_PLUGIN)
        self._attach_real(M64PLUGIN_RSP, RSP_PLUGIN)
        self._core.CoreAttachPlugin(M64PLUGIN_AUDIO, None)
        import os
        os.environ.pop("SL_INPUT_REPLAY", None)
        os.environ.pop("SL_INPUT_RECORD", None)
        if self.input_replay:
            os.environ["SL_INPUT_REPLAY"] = self.input_replay
        if self.input_record:
            os.environ["SL_INPUT_RECORD"] = self.input_record
        if self.input_replay or self.input_record:
            self._attach_real(M64PLUGIN_INPUT, SL_INPUT_PLUGIN)
        else:
            self._core.CoreAttachPlugin(M64PLUGIN_INPUT, None)
        self._core.CoreDoCommand(self.cmd["M64CMD_EXECUTE"], 0, None)

    def run(self, on_tick, frame_counter_of, max_ticks: int, max_vi: int = 2_000_000):
        """on_tick(tick_index, frame_counter) is called once per advancing tick."""
        state = {"last_fc": None, "ticks": 0, "vi": 0, "stalled_at": None,
                 "error": None, "state_loaded": False}

        def dbg_init():
            self._core.DebugSetRunState(DBG_RUNSTATE_RUNNING)

        def dbg_vi():
            state["vi"] += 1
            base = self.rdram()
            if base is None:
                return

            # Load the savestate once the core is actually running, then start
            # counting ticks from there so record and replay align on tick 0.
            if self.load_state and not state["state_loaded"]:
                rc = self._core.CoreDoCommand(
                    self.cmd["M64CMD_STATE_LOAD"], 0,
                    C.c_char_p(self.load_state.encode()))
                state["state_loaded"] = True
                state["last_fc"] = None
                if rc != 0:
                    state["error"] = f"savestate load failed rc={rc}: {self.load_state}"
                    self._core.CoreDoCommand(self.cmd["M64CMD_STOP"], 0, None)
                return
            fc = frame_counter_of(base)
            if fc == state["last_fc"]:
                # Detect a hung game rather than silently sampling nothing.
                if state["vi"] > 600 and state["ticks"] < 8:
                    state["stalled_at"] = fc
                    self._core.CoreDoCommand(self.cmd["M64CMD_STOP"], 0, None)
                return
            # Write the savestate periodically, not at exit. There is no way to
            # quit the harness from inside the game, so the window gets closed -
            # and anything deferred to shutdown never happens. Same bug class as
            # the input stream and trace, in the one place I had not fixed it.
            if self.save_state and state["ticks"] and state["ticks"] % 600 == 0:
                self._core.CoreDoCommand(
                    self.cmd["M64CMD_STATE_SAVE"], 1,
                    C.c_char_p(self.save_state.encode()))

            state["last_fc"] = fc
            try:
                on_tick(state["ticks"], fc, base)
            except BaseException as e:
                # ctypes prints and discards callback exceptions, which would
                # surface later as a bogus "stalled game". Capture and stop.
                import traceback
                state["error"] = "".join(traceback.format_exception(e))
                self._core.CoreDoCommand(self.cmd["M64CMD_STOP"], 0, None)
                return
            state["ticks"] += 1
            if state["ticks"] >= max_ticks or state["vi"] >= max_vi:
                self._core.CoreDoCommand(self.cmd["M64CMD_STOP"], 0, None)

        self._ci, self._cu, self._cv = VOIDCB(dbg_init), UPDCB(lambda pc: None), VOIDCB(dbg_vi)
        self._core.DebugSetCallbacks(self._ci, self._cu, self._cv)

        data = Path(self.rom_path).read_bytes()
        buf = C.create_string_buffer(data, len(data))
        rc = self._core.CoreDoCommand(self.cmd["M64CMD_ROM_OPEN"], len(data), buf)
        if rc != 0:
            raise RuntimeError(f"ROM_OPEN failed rc={rc}")

        # Order matters: gfx before rsp, because RSP HLE feeds the video plugin.
        self._attach_real(M64PLUGIN_GFX, GFX_PLUGIN)
        self._attach_real(M64PLUGIN_RSP, RSP_PLUGIN)
        self._core.CoreAttachPlugin(M64PLUGIN_AUDIO, None)
        # Input goes through our own plugin so replay needs no hardware.
        import os
        os.environ.pop("SL_INPUT_REPLAY", None)
        os.environ.pop("SL_INPUT_RECORD", None)
        if self.input_replay:
            os.environ["SL_INPUT_REPLAY"] = self.input_replay
        if self.input_record:
            os.environ["SL_INPUT_RECORD"] = self.input_record
        if self.input_replay or self.input_record:
            # Recording chains to the SDL plugin, which reads its pad mapping
            # from the config dir we started the core with - so the recording
            # config must be the one holding the gamepad profile.
            self._attach_real(M64PLUGIN_INPUT, SL_INPUT_PLUGIN)
        else:
            self._core.CoreAttachPlugin(M64PLUGIN_INPUT, None)

        self._core.CoreDoCommand(self.cmd["M64CMD_EXECUTE"], 0, None)

        if state["error"] is not None:
            raise RuntimeError("state capture raised:\n" + state["error"])

        if state["stalled_at"] is not None:
            raise RuntimeError(
                f"game stalled: currentFrameCounter stuck at {state['stalled_at']} "
                f"after {state['vi']} VI interrupts with only {state['ticks']} ticks. "
                f"This usually means a plugin is missing or null - the game blocks "
                f"on the graphics pipeline and never advances."
            )
        return state
