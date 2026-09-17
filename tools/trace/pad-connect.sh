#!/bin/sh
# Reconnect the recording controller under WSL.  Run with sudo.
#
# WSL has no udev daemon (pid 1 is init, not systemd), so nothing loads the
# input drivers on demand and nothing fixes the permissions on the device
# nodes - they always appear root:root 0600.  Every step below is therefore
# manual, and has to be redone whenever the pad is unplugged or WSL restarts.
#
#   usage:  sudo tools/trace/pad-connect.sh [busid]
#
# The default busid matches the DualSense this harness is configured for
# (054c:0ce6); `usbipd.exe list` on the Windows side shows the current one.
set -e

BUSID="${1:-1-9}"
VIDPID="054c:0ce6"

if lsusb 2>/dev/null | grep -qi "$VIDPID"; then
    echo "pad already attached to WSL"
else
    echo "attaching busid $BUSID ..."
    # usbipd lives on the Windows side; reachable through WSL interop.
    usbipd.exe attach --wsl --busid "$BUSID" || {
        echo "attach failed - check 'usbipd.exe list' for the right busid" >&2
        exit 1
    }
    i=0
    while [ $i -lt 10 ]; do
        lsusb 2>/dev/null | grep -qi "$VIDPID" && break
        sleep 1
        i=$((i + 1))
    done
fi

# modprobe takes ONE module plus parameters; -a is what loads several.
modprobe -a usbhid joydev evdev hid-generic

i=0
while [ $i -lt 10 ]; do
    [ -e /dev/input/js0 ] && break
    sleep 1
    i=$((i + 1))
done

if [ ! -e /dev/input/js0 ]; then
    echo "no /dev/input/js0 - drivers loaded but nothing bound" >&2
    ls -l /dev/input/ 2>&1 >&2 || true
    exit 1
fi

# No udev, so the nodes stay root-owned; open them up for the recording run.
chmod a+rw /dev/input/js* /dev/input/event* 2>/dev/null || true

ls -l /dev/input/
echo
echo "pad ready - SDL should now enumerate it as device 0"
