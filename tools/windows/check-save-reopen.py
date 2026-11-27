"""Exercise native cartridge saving across two automated game launches.

The ROM is supplied by the user. All writable state stays in a temporary
directory, isolated from the player's actual save and settings.
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "trace"))
from sltrace.savefile import find_records


def run(exe, rom, work, save):
    env = {key: value for key, value in os.environ.items()
           if not key.startswith("SL_")}
    env.update(LOCALAPPDATA=str(work / "local-data"),
               SL_ROM=str(rom), SL_EEPROM_RW=str(save),
               SL_AUTO_ROUTE="runway", SL_WINDOW="1",
               SL_WINDOW_SIZE="640x480", SL_FPS="60",
               SL_RUN="0", SL_FRAMES="3600", SL_AUDIO="0")
    (work / "local-data").mkdir(exist_ok=True)
    stream = work / "skip-titles.input"
    words = bytearray(3600 * 4)
    for vi in range(1800):
        if vi % 90 < 4:
            words[vi * 4 + 3] = 0x10  # N64 START_BUTTON, high byte
    stream.write_bytes(words)
    env["SL_INPUT"] = str(stream)
    try:
        result = subprocess.run([str(exe)], cwd=exe.parent, env=env,
                                capture_output=True, timeout=120,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError("game did not exit within 120 seconds") from exc
    log = result.stderr.decode("utf-8", errors="replace")
    if result.returncode != 0 or "sl_autoroute: done p5 steps=5" not in log:
        tail = "\n".join(log.splitlines()[-20:])
        raise RuntimeError(f"route failed (exit {result.returncode}):\n{tail}")
    if "sl_autoroute: refused" in log:
        raise RuntimeError("the saved Runway mission was refused")
    if not save.is_file() or save.stat().st_size != 2048:
        raise RuntimeError("game did not persist a complete EEPROM image")
    hits = find_records(save.read_bytes())
    if 32 not in hits:
        raise RuntimeError(f"first folder failed cartridge checksum: {hits}")
    return hits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--fixture", type=Path,
                        default=Path(__file__).resolve().parents[1] /
                        "trace/inputs/runway.eeprom")
    args = parser.parse_args()
    exe, rom, fixture = (path.resolve() for path in
                         (args.exe, args.rom, args.fixture))
    if not exe.is_file() or not rom.is_file() or not fixture.is_file():
        parser.error("exe, ROM, and fixture must exist")
    if rom.stat().st_size != 12582912:
        parser.error("ROM must be the 12 MiB North American cartridge image")
    with tempfile.TemporaryDirectory(prefix="sightline-save-reopen-") as temp:
        work = Path(temp)
        save = work / "eeprom.bin"
        save.write_bytes(fixture.read_bytes()[:2048])
        if save.stat().st_size != 2048 or 32 not in find_records(save.read_bytes()):
            parser.error("fixture does not contain a valid first folder")
        first = run(exe, rom, work, save)
        second = run(exe, rom, work, save)
        print(f"save reopen OK; first valid folders: {first}; "
              f"second valid folders: {second}")


if __name__ == "__main__":
    main()
