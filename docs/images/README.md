# Documentation screenshots

Screenshots of the native Windows build, captured for the repository README.
Each frame is read back from the game's own framebuffer at 1920x1080. The only
edits are exact crops of black borders, caption bands added below the
comparison pictures, and lossless assembly of frames into animated PNGs (APNG,
no palette reduction).

| File | Kind | What it shows | Source and edits |
|---|---|---|---|
| `dam-presentations-cycle.png` | APNG, 3 frames, 1000 ms each, loops | The Dam's gate yard as ORIGINAL, COMMUNITY HD and XBLA in turn - the order the PRESENTATION cycle steps | three captures from one pose (eye 19084.352, 60.276, 17547.629; theta 90.319, verta -4.0), seed and set of settings, differing only in PRESENTATION; the letterbox rows 0..44 and 1035..1079 cropped; a caption band naming the mode added below; 1920x1062 |
| `dam-presentations.png` | still | The same three captures cut into equal vertical slices at the same image coordinates: ORIGINAL (x 0..639), XBLA (x 640..1279), COMMUNITY HD (x 1280..1919) | the same crop; each slice is its own capture's pixels; caption band below; 1920x1054 |
| `dam-road-presentations.png` | still | The road along the top of the dam, sliced the same way: ORIGINAL, XBLA, COMMUNITY HD | three captures from one pose (eye -12005.7, 150.0, 1279.7; theta 255, verta 0), seed and settings, differing only in PRESENTATION; the same crop and slicing; caption band below; 1920x1054 |
| `menus-cycle.png` | APNG, 6 frames, 1500 ms each, loops | The front end: the main menu, OPTIONS, then SETTINGS' CONTROL, GAMEPLAY, DISPLAY (with PRESENTATION) and PAD tabs | six captures from one run of the real front end; the black pillarbox columns either side cropped (columns 225..1659 kept); 1435x1080 |
| `watch-sightline-cycle.png` | APNG, 5 frames, 1500 ms each, loops | The watch's last tab, SIGHTLINE, then its GRAPHICS, GAMEPLAY, DISPLAY and CONTROLS pages, in the Dam | five captures; the letterbox rows 0..44 and 1035..1079 cropped; 1920x990 |
| `watch-controllers-cycle.png` | APNG, 2 frames, 1500 ms each, loops | The watch's controls tab with the device-matched controller model: an Xbox controller, then a DualSense | two captures, the controller reported to the game as each type; the same row crop; 1920x990 |

**Copyright.** These images show third-party material: the game's own
artwork, decoded at run time from a user's GoldenEye 007 ROM; the Community HD
texture set, the work of its maintainers; and the GoldenEye 007 XBLA
remaster's artwork, read from a user-supplied source. That material belongs to
its rightsholders. `watch-controllers-cycle.png` also shows two third-party
models licensed under CC-BY-4.0 and modified for Sightline: "Xbox Controller"
by umkhero and "Playstation 5 Dualsense" by AHarmlessPotato (full credit lines
in `data/asset-overrides/source/controllers/*/ATTRIBUTION.md`). These
screenshots are not Sightline-authored artwork and are not covered by either
Sightline license (see [LICENSES/README.md](../../LICENSES/README.md)); they
are included only to document the software.
