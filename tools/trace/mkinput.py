#!/usr/bin/env python3
"""
Synthesise a replay input stream.

Why this exists. Every recorded stream is a capture of someone playing, so a
question the recording never happened to exercise cannot be answered from it.
That is exactly what stalled B-032: across the whole facility recording the
game creates ZERO bullet impacts, so the trace says nothing about whether
decals render - only that this particular playthrough never shot a wall.
Recording a new one needs a window and a controller, and the display is not
always available.

The format is small enough to write directly (src/platform/sl_ultra_shim.c:421):
one 4-byte big-endian word per CONTROLLER READ - buttons hi, buttons lo,
stick x, stick y - with neutral input assumed after the end of the stream.

  tools/trace/mkinput.py fire out.input      hold fire in bursts after a lead-in
  tools/trace/mkinput.py idle out.input      neutral throughout (a control)
  tools/trace/mkinput.py pause out.input     raise the watch and hold it up
  tools/trace/mkinput.py watch out.input     watch -> equipment page -> scroll

The lead-in matters and is not a guess: the recorded facility stream sits
neutral for its first 461 reads while the level loads and the intro camera
hands over to first person, so anything shorter presses buttons at a menu.
This uses 900 to leave margin.

Button bits come from include/PR/os.h (Z_TRIG = CONT_G = 0x2000). Z is the fire
button - bondview2.c:4921 reads it for the fire/aim logic.
"""
import struct, sys

Z_TRIG       = 0x2000
START_BUTTON = 0x1000
R_JPAD       = 0x0100
D_JPAD       = 0x0400
LEAD_IN  = 900          # reads held neutral before any input


def word(button=0, stick_x=0, stick_y=0):
    """One controller read, in the order the SHIM ACTUALLY READS.

    Do not trust the prose at sl_ultra_shim.c:421 - it says "buttons hi,
    buttons lo, stick x, stick y" and the code below it does the reverse:

        button  = (b[3] << 8) | b[2]
        stick_x = b[1]
        stick_y = b[0]

    So byte 0 is stick_y and byte 3 is the button HIGH byte. Packing it the
    way the comment described cost several long runs: Z_TRIG (0x2000) came out
    as `20 00 00 00`, which the shim reads as button 0 with stick_y = 32 -
    bondviewProcessInput saw buttons == 0 on all 2437 calls.

    Checked against the real recording: its first non-neutral word is
    `00 00 00 10`, i.e. button 0x1000 (START), which only decodes correctly
    under this order.
    """
    return struct.pack("BbBB", stick_y & 0xFF, stick_x,
                       button & 0xFF, (button >> 8) & 0xFF)


def build(routine):
    out = [word() for _ in range(LEAD_IN)]
    if routine == "idle":
        out += [word() for _ in range(2000)]
    elif routine == "fire":
        # Bond spawns facing the airlock door at point-blank range, so firing
        # without turning is enough to put rounds into geometry. Burst rather
        # than hold: most weapons need the trigger released between shots.
        # Burst for as long as the recorded stream runs. A short tail was the
        # first attempt and it fired nothing: 900 reads of lead-in is NOT
        # provably past the intro camera, and read 461 only marks when the
        # human first pressed something, not when the game became
        # controllable. Covering the whole window removes the guess.
        for _ in range(750):
            out += [word(Z_TRIG) for _ in range(6)]
            out += [word() for _ in range(14)]
    elif routine == "pause":
        # Raise the watch and leave it up. START is a rising-edge toggle
        # (bondview2.c:4839 tests `buttons & ~oldbuttons & START_BUTTON`), so
        # it must be pressed and RELEASED - holding it does nothing after the
        # first frame, and pressing it twice puts the watch away again.
        # Press REPEATEDLY, not once. A single press at read 900 delivered
        # nothing: bondviewProcessInput saw buttons == 0 on all 1988 calls,
        # because 900 reads is not provably past the point where the game acts
        # on input and the stream was neutral forever after. The `fire`
        # routine works precisely because it bursts across the whole stream,
        # so some burst always lands once the game is live.
        #
        # START toggles, so the watch goes up and down across the run. That is
        # fine for measurement - any frame with the watch up reaches the draw.
        for _ in range(25):
            out += [word(START_BUTTON) for _ in range(4)]
            out += [word() for _ in range(596)]
    elif routine == "watch":
        # Raise the watch, step RIGHT onto the equipment page, then scroll the
        # item list DOWN one press at a time. Written for B-044, which only
        # shows on the first move off "unarmed".
        #
        # The timing here is MEASURED, not guessed, and getting it wrong is
        # silent: watch_screen0_navigation (options.c:688) only acts while the
        # watch is fully open, so a RIGHT that lands during the open animation
        # is swallowed and the run sits on the mission-status page forever -
        # which is exactly what the first attempt did, for 6000 frames.
        # bondviewProcessInput is called once per TWO controller reads, and
        # the watch reaches WATCH_ANIMATION_0x5 about 140 calls after the
        # START edge, so RIGHT goes 350 reads after START with margin.
        #
        # START toggles, so the watch alternates up/down cycle by cycle and
        # every second cycle re-runs the whole approach from the top. That is
        # wanted: reopening resets the page to mission status and the cursor
        # to unarmed, so each up-cycle is a fresh instance of the transition
        # under test.
        period = 1200
        for _ in range(12):
            blk = [word() for _ in range(period)]
            for i in range(0, 4):
                blk[i] = word(START_BUTTON)
            for i in range(350, 354):
                blk[i] = word(R_JPAD)
            t = 430
            while t + 4 < period:
                for i in range(t, t + 4):
                    blk[i] = word(D_JPAD)
                t += 60
            out += blk
    else:
        sys.exit(f"mkinput: unknown routine '{routine}' (fire, idle, pause, watch)")
    return b"".join(out)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip().splitlines()[0] + "\nusage: mkinput.py <routine> <out.input>")
    routine, path = sys.argv[1], sys.argv[2]
    data = build(routine)
    with open(path, "wb") as f:
        f.write(data)
    print(f"mkinput: {routine} -> {path}  ({len(data)//4} reads, lead-in {LEAD_IN})")
