#!/bin/bash
# Make a USB gamepad usable inside WSL2, from a Windows-attached device.
#
# WSL2's kernel ships no joystick drivers, so an Xbox pad (vendor-class GIP)
# cannot work without a custom kernel. A DualSense or any other HID-class pad
# CAN, because usbhid/hid-generic/joydev/evdev are all present - they just need
# loading, and the device nodes come out root-only since no udev is running.
#
# Re-run this after every attach: the nodes are recreated each time.
#
#   usbipd bind --force --busid <N>   (elevated PowerShell, once per boot)
#   sudo tools/trace/recording/attach_pad.sh <N>
set -euo pipefail

BUSID="${1:-}"
[ -n "$BUSID" ] || { echo "usage: attach_pad.sh <usbipd-busid>   e.g. 1-9" >&2; exit 1; }
[ "$(id -u)" = "0" ] || { echo "needs root (modprobe + chmod): sudo $0 $BUSID" >&2; exit 1; }

usbipd.exe attach --wsl --busid "$BUSID" 2>&1 | grep -viE "warning: Unknown USB filter" || true
sleep 3

# hid-generic binds the pad; joydev/evdev expose it. SDL reads evdev.
modprobe usbhid hid-generic joydev evdev 2>/dev/null || true
sleep 1

shopt -s nullglob
nodes=(/dev/input/event* /dev/input/js*)
if [ ${#nodes[@]} -eq 0 ]; then
    echo "no input nodes appeared - is the pad HID-class? (Xbox pads are not)" >&2
    exit 1
fi
# No udev here, so nodes are root:root 0600 and SDL cannot open them.
chmod a+rw "${nodes[@]}"
echo "input nodes ready:"
ls -l "${nodes[@]}"
