# LICENSES

What in this repository is, and is not, covered by the Sightline licenses.
The inventory is by directory, taken from `git diff --name-status
upstream/master master` against `gitlab.com/kholdfuzion/goldeneye_src` and
from each file's own header. Modification in this fork does not change a
file's status: a file that exists upstream stays upstream material however
much it has been edited here.

| Material | Where | License |
|---|---|---|
| Sightline-owned code and documentation (see the directory list below) | Files added in this fork under `src/gfx/`, `src/platform/`, `src/native/`, `src/net/`, `src/sl_*.h`, `src/game/sl_*.h`, `tools/asset/`, `tools/derive/`, `tools/docs/`, `tools/export/`, `tools/native/`, `tools/sightline/`, `tools/trace/` (harness code), `tools/windows/`, `ci/`, `.gitea/workflows/`, `data/overrides/`, `docs/` files added in this fork, `requirements.txt`, and the scripts under `data/asset-overrides/` | Zero-Clause BSD — [Sightline-Code-0BSD.txt](Sightline-Code-0BSD.txt) |
| Sightline-owned original artwork | `data/asset-overrides/source/**` — the `.gltf`, `.bin`, `.png` and `metadata.json` files whose source glTF declares `extras.sl_authored` with a provenance string (currently the four boot packages: `goldeneye_logo`, `legal_page`, `nintendo_logo`, `rareware_logo`) | CC0 1.0 Universal — [Sightline-Assets-CC0-1.0.txt](Sightline-Assets-CC0-1.0.txt); see [data/asset-overrides/LICENSE.md](../data/asset-overrides/LICENSE.md) |
| Third-party controller models (the watch's device-matched controllers) | `data/asset-overrides/source/controllers/xbox/` — "Xbox Controller" by umkhero (https://sketchfab.com/umkhero), https://sketchfab.com/3d-models/xbox-controller-32d17951703b4e05abed11a1c65f5909; `data/asset-overrides/source/controllers/dualsense/` — "Playstation 5 Dualsense" by AHarmlessPotato (https://sketchfab.com/AHarmlessPotato), https://sketchfab.com/3d-models/playstation-5-dualsense-878c1f882808477ab81c2fe86d5a3936. Modified for Sightline (split into parts, re-based and scaled to the N64 pad's frame, PBR maps dropped, textures resized, manufacturer logos removed; each package's `ATTRIBUTION.md` lists the changes). The two replacement parts flagged `sl_authored_part` (the plain Guide and PS buttons) are the project owner's own. Declared in the glTF as `extras.sl_third_party`, never `sl_authored`. | CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/). Attribution required; the credit line to reproduce in any release is in each package's `ATTRIBUTION.md`. **Not** covered by the CC0 grant, nor by the code grant. No endorsement by the authors is implied. |
| rabbitizer | `tools/ido5.3_recomp/rabbitizer/` | MIT — see `tools/ido5.3_recomp/rabbitizer/LICENSE` (Decompollaborate). Not covered by the Sightline licenses. |
| ido-static-recomp sources | `tools/ido5.3_recomp/*.c`, `*.cpp`, `*.h`, `Makefile` | Third-party (decompals/Emill `ido-static-recomp`). No license file is present in this tree. Not covered by the Sightline licenses. |
| Upstream GoldenEye decompilation | Everything present in `gitlab.com/kholdfuzion/goldeneye_src` — the upstream parts of `src/`, `include/`, `assets/`, `ld/`, `rsp/`, `docs/`, `scripts/` and `tools/` (e.g. `tools/ido5.3_recomp/`, `tools/irix/`), the root `Makefile`, `readme.md`, `.github/workflows/` and the rest — including the files this fork has modified | No explicit license identified in upstream. Retained as-is. Not covered by the Sightline licenses. |
| SGI IRIX IDO 5.3 toolchain binaries | `tools/irix/root/`, `tools/SGIImageViewer.exe` | Inherited third-party status with their existing notices. Not covered. |
| N64 SDK / libultra headers and sources | `src/libultra/`, `src/libultrare/`, `include/`, `rsp/` | Inherited third-party status with their existing copyright notices. Not covered. |
| Files carrying another author's notice | e.g. `src/game/spectrum_hw.h` (Istvan Novak, MIT, per its header) | As stated in the file. Not covered. |
| Derived, ported or uncertain files added in this fork | Listed below | Outside both Sightline grants. |

## Sightline-owned code: directory list

Files with status `A` (added) in the diff against upstream, under:

- `src/gfx/`, `src/platform/`, `src/native/`, `src/net/`
- `src/sl_asan.h`, `src/sl_asset_override.h`, `src/sl_endian.h`, `src/sl_types.h`
- `src/game/sl_audioev.h`, `src/game/sl_escort.h`, `src/game/sl_romdbg.h`
- `tools/asset/`, `tools/derive/`, `tools/docs/`, `tools/export/`,
  `tools/native/`, `tools/sightline/`, `tools/windows/`
- `tools/trace/` — the harness code: `*.py`, `*.c`, `*.sh`, `Makefile`,
  `README.md`, `tests/`, `recording/`, `slinput/`, `sltrace/`
- `ci/`, `.gitea/workflows/`, `data/overrides/`, `requirements.txt`
- `docs/` files added in this fork (`ROADMAP.md`, `project-rules.md`,
  `backlog.md`, `divergences.md`, `decisions/`, `doc-routing.json`, and the
  other Sightline-authored documents); documentation authored for Sightline
  is covered by the same 0BSD grant as the code
- `data/asset-overrides/README.md`, `data/asset-overrides/LICENSE.md`, and
  the scripts under `data/asset-overrides/source/` (e.g. `check_bounds.py`)
- `LICENSES/README.md` (this file)

The files named in the next section are excluded from that list even though
they sit in these directories.

## Excluded: derived, ported or uncertain files added in this fork

These were added in the fork but are ports, transcriptions or close
derivations of upstream code or of data read out of the game, or their
status could not be settled from the file itself. They are outside both
Sightline grants and keep whatever status their source material has.

- `src/game/sl_ported_asm.c` — C port of upstream hand-written MIPS assembly
  (`src/random.s`, `src/game/chrObjRandom.s`, and others named in the file).
- `src/native/sl_credits_dbg.c` — its header states its arithmetic is a
  transcription of upstream `bondviewRenderCredits`.
- `tools/trace/sltrace/savefile.py` and `tools/save/ge_unlock_save.py` —
  each contains a port of the save checksum routine from upstream
  `src/game/crc.c` plus the PRNG step.
- `src/platform/sl_acmd.c` — the native ACMD interpreter; its comments state
  that the ADPCM and POLEF sections were derived instruction-level from the
  aspMain audio microcode read out of the ROM. Excluded as uncertain.
- `tools/native/envlit.h`, `tools/native/acmdenvfull.c`,
  `tools/native/acmdenvx.c`, `tools/native/acmdvoicex.c`,
  `tools/native/acmdenv3.c`, `tools/native/acmdenv3w.c` — audio-derivation
  evaluators described in their headers as literal transcriptions or decoded
  instruction tables of the same microcode. Excluded as uncertain.
- Recorded harness data, generated by running the game under the harness and
  not hand-authored: `tools/trace/inputs/` (`*.input`, `*.spec`, `*.eeprom`),
  `tools/trace/traces/` (`*.sltrace`), `tools/trace/sessions/`, `*.move`,
  `*.vis`, `tools/trace/gate-results.json`, `docs/coverage-v12.json`, and the
  committed `tools/trace/tests/__pycache__/*.pyc`. Not explicitly scoped by
  either grant.

## What this file does not do

It records what the files themselves show. It makes no claim about the
licensing status of the upstream decompilation, the SDK material or the
toolchain beyond noting that none of them is covered by the Sightline
licenses, and it does not relicense anything inherited.
