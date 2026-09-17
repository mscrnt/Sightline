"""Minimal libretro frontend: load core, run N frames, hash RDRAM each frame.

retro_run() advances EXACTLY one frame, so there is no sampling point to get
wrong - which is the entire class of bug that made mupen64plus so painful.
"""
import ctypes as C, hashlib, sys

CORE = sys.argv[1]
ROM = "/mnt/projects/sightline/build/u/ge007.u.z64"
FRAMES = int(sys.argv[2]) if len(sys.argv) > 2 else 300

RETRO_MEMORY_SYSTEM_RAM = 2
ENV_GET_CAN_DUPE = 3
ENV_GET_SYSTEM_DIRECTORY = 9
ENV_SET_PIXEL_FORMAT = 10
ENV_GET_VARIABLE = 15
ENV_SET_VARIABLES = 16
ENV_GET_VARIABLE_UPDATE = 17
ENV_GET_LOG_INTERFACE = 27
ENV_GET_SAVE_DIRECTORY = 31
ENV_SET_CORE_OPTIONS = 53
ENV_SET_CORE_OPTIONS_INTL = 54
ENV_GET_CORE_OPTIONS_VERSION = 52
ENV_SET_CORE_OPTIONS_V2 = 67
ENV_SET_CORE_OPTIONS_V2_INTL = 68
ENV_GET_LANGUAGE = 39
ENV_GET_INPUT_BITMASKS = 51
ENV_SET_SUPPORT_NO_GAME = 18
ENV_SET_PERFORMANCE_LEVEL = 8
ENV_GET_RUMBLE_INTERFACE = 23
ENV_GET_PERF_INTERFACE = 28
ENV_SET_GEOMETRY = 37
ENV_SET_SYSTEM_AV_INFO = 32

class GameInfo(C.Structure):
    _fields_ = [("path", C.c_char_p), ("data", C.c_void_p),
                ("size", C.c_size_t), ("meta", C.c_char_p)]

ENVCB   = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VIDEOCB = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AUDIOCB = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
AUDIOBCB= C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t)
POLLCB  = C.CFUNCTYPE(None)
STATECB = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)

core = C.CDLL(CORE)
sysdir = C.c_char_p(b"/tmp/lrtest")

def on_env(cmd, data):
    if cmd == ENV_SET_PIXEL_FORMAT:  return True
    if cmd == ENV_GET_CAN_DUPE:
        C.cast(data, C.POINTER(C.c_bool))[0] = True; return True
    if cmd in (ENV_GET_SYSTEM_DIRECTORY, ENV_GET_SAVE_DIRECTORY):
        C.cast(data, C.POINTER(C.c_char_p))[0] = sysdir; return True
    if cmd in (ENV_SET_VARIABLES, ENV_SET_CORE_OPTIONS, ENV_SET_CORE_OPTIONS_INTL):
        return True
    if cmd == ENV_GET_VARIABLE_UPDATE:
        C.cast(data, C.POINTER(C.c_bool))[0] = False; return True
    if cmd == ENV_GET_VARIABLE:
        # No overrides: let the core use its own defaults.
        C.cast(data, C.POINTER(C.c_void_p))[1] = None
        return False
    if cmd == ENV_GET_CORE_OPTIONS_VERSION:
        C.cast(data, C.POINTER(C.c_uint))[0] = 0    # legacy options only
        return True
    if cmd == ENV_GET_LANGUAGE:
        C.cast(data, C.POINTER(C.c_uint))[0] = 0
        return True
    if cmd in (ENV_SET_PERFORMANCE_LEVEL, ENV_SET_SUPPORT_NO_GAME,
               ENV_SET_GEOMETRY, ENV_SET_SYSTEM_AV_INFO,
               ENV_SET_CORE_OPTIONS_V2, ENV_SET_CORE_OPTIONS_V2_INTL):
        return True
    return False

def on_video(data, w, h, pitch): pass
def on_audio(l, r): pass
def on_audio_batch(data, frames): return frames
def on_poll(): pass
def on_state(port, device, index, ident): return 0

cbs = [ENVCB(on_env), VIDEOCB(on_video), AUDIOCB(on_audio),
       AUDIOBCB(on_audio_batch), POLLCB(on_poll), STATECB(on_state)]
core.retro_set_environment(cbs[0])
core.retro_set_video_refresh(cbs[1])
core.retro_set_audio_sample(cbs[2])
core.retro_set_audio_sample_batch(cbs[3])
core.retro_set_input_poll(cbs[4])
core.retro_set_input_state(cbs[5])
core.retro_init()

data = open(ROM, "rb").read()
buf = C.create_string_buffer(data, len(data))
gi = GameInfo(path=ROM.encode(), data=C.cast(buf, C.c_void_p),
              size=len(data), meta=None)
core.retro_load_game.restype = C.c_bool
if not core.retro_load_game(C.byref(gi)):
    print("LOAD FAILED"); raise SystemExit(1)

core.retro_get_memory_data.restype = C.c_void_p
core.retro_get_memory_size.restype = C.c_size_t
core.retro_run()          # cores often allocate RDRAM lazily on the first frame
mem = core.retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)
size = core.retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)
print(f"loaded ok; SYSTEM_RAM = {size} bytes at {hex(mem) if mem else None}")
if not mem:
    print("NO MEMORY ACCESS"); raise SystemExit(1)

base = C.cast(mem, C.POINTER(C.c_ubyte))
h = hashlib.sha1()
for i in range(FRAMES):
    core.retro_run()
    h.update(bytes(base[0x24460:0x24468]))      # g_randomSeed
    h.update(bytes(base[0x48490:0x48498]))      # frame counters
print(f"frames={FRAMES} SEQ={h.hexdigest()}")
