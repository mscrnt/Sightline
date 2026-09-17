#!/bin/bash
# Sightline — base ROM verification gate.
#
# Runs BEFORE any build step. A wrong-region or byte-swapped dump otherwise
# produces compile/link failures deep in the build that read as toolchain
# problems and cost days. Every failure below names the actual cause and the fix.
#
# usage: verify_rom.sh <rom-path> <expected-sha1-file> <outcode>

set -u

ROM="${1:-baserom.u.z64}"
SHA1FILE="${2:-ge007.u.sha1}"
OUTCODE="${3:-u}"

red()  { printf '\033[1;31m%s\033[0m\n' "$*" >&2; }
grn()  { printf '\033[1;32m%s\033[0m\n' "$*"; }
info() { printf '%s\n' "$*" >&2; }

die() { red "$*"; exit 1; }

banner() {
    red "=============================================================="
    red " BASE ROM VERIFICATION FAILED"
    red "=============================================================="
}

# --- 1. presence -----------------------------------------------------------
if [ ! -f "$ROM" ]; then
    banner
    red " Missing ROM: $ROM"
    info ""
    info " Sightline never ships game assets. Supply your own dump of the"
    info " USA cartridge and place it at the repo root as: $ROM"
    exit 1
fi

if [ ! -r "$ROM" ]; then
    banner; die " ROM exists but is not readable: $ROM"
fi

# --- 2. size ---------------------------------------------------------------
EXPECTED_SIZE=12582912   # 12 MiB
ACTUAL_SIZE=$(stat -c %s "$ROM")
if [ "$ACTUAL_SIZE" -ne "$EXPECTED_SIZE" ]; then
    banner
    red " Wrong size: $ACTUAL_SIZE bytes (expected $EXPECTED_SIZE = 12 MiB)"
    info ""
    if [ "$ACTUAL_SIZE" -lt "$EXPECTED_SIZE" ]; then
        info " The file is short. This is usually a truncated download or a"
        info " ROM still inside an archive. Do not build against it."
    else
        info " The file is oversized. This is usually a dump with a copier"
        info " header (e.g. 512-byte .smc-style header) prepended, or a"
        info " concatenated file. Strip the header before use."
    fi
    exit 1
fi

# --- 3. byte order ---------------------------------------------------------
# The three dump conventions carry identical content in different byte orders,
# so a swapped dump has correct data and a completely different SHA-1. Naming
# that explicitly is the whole point of checking magic before hashing.
MAGIC=$(xxd -p -l 4 "$ROM" | tr '[:upper:]' '[:lower:]')
case "$MAGIC" in
    80371240) ;;  # z64, big-endian — correct
    37804012)
        banner
        red " Byte-swapped dump detected (.v64 / 16-bit byte-swapped)."
        red " Magic is 0x37804012; the build requires 0x80371240 (.z64)."
        info ""
        info " The DATA IS CORRECT — only the byte order is wrong, which is why"
        info " the SHA-1 will not match. Convert it, do not re-dump:"
        info ""
        info "     dd if=$ROM of=baserom.tmp conv=swab && mv baserom.tmp $ROM"
        info ""
        info " (or: tool64 / ucon64 --z64)"
        exit 1
        ;;
    40123780)
        banner
        red " Little-endian dump detected (.n64 / 32-bit word-swapped)."
        red " Magic is 0x40123780; the build requires 0x80371240 (.z64)."
        info ""
        info " The DATA IS CORRECT — only the byte order is wrong. Convert to"
        info " big-endian .z64 with ucon64 --z64 (a plain conv=swab is NOT"
        info " sufficient for this variant; it is a 4-byte word swap)."
        exit 1
        ;;
    *)
        banner
        red " Not an N64 ROM: magic is 0x$MAGIC, expected 0x80371240."
        info ""
        info " The first four bytes match no known N64 dump convention."
        info " This is most likely a zip/7z archive, an emulator save state,"
        info " or an HTML error page saved with a .z64 extension."
        exit 1
        ;;
esac

# --- 4. region -------------------------------------------------------------
# Country code lives at 0x3E. Wrong-region ROMs build cleanly for a long while
# before failing, so catch it here rather than at link time.
CC_HEX=$(xxd -p -s 62 -l 1 "$ROM")
CC=$(printf "\\x${CC_HEX}" | tr -d '\0')
case "$OUTCODE" in
    u) WANT_CC="E"; WANT_REGION="USA / North America" ;;
    e) WANT_CC="P"; WANT_REGION="Europe (PAL)" ;;
    j) WANT_CC="J"; WANT_REGION="Japan" ;;
    *) WANT_CC=""; WANT_REGION="(unknown outcode $OUTCODE)" ;;
esac

region_name() {
    case "$1" in
        E) echo "USA / North America" ;;
        P) echo "Europe (PAL)" ;;
        J) echo "Japan" ;;
        *) echo "unknown (0x$CC_HEX)" ;;
    esac
}

if [ -n "$WANT_CC" ] && [ "$CC" != "$WANT_CC" ]; then
    banner
    red " Wrong region."
    red "   ROM is:   $(region_name "$CC")  [country code '$CC']"
    red "   Build is: $WANT_REGION           [expects '$WANT_CC']"
    info ""
    info " Either supply the $WANT_REGION dump, or build the version matching"
    info " your ROM:   make matching VERSION=EU     (or VERSION=JP)"
    exit 1
fi

# --- 5. SHA-1 --------------------------------------------------------------
# The decomp reproduces the retail ROM byte for byte, so the expected hash of
# the BUILD OUTPUT is also the expected hash of a good base ROM.
[ -f "$SHA1FILE" ] || die "Missing checksum file: $SHA1FILE"
EXPECTED_SHA1=$(awk '{print $1; exit}' "$SHA1FILE")
ACTUAL_SHA1=$(sha1sum "$ROM" | awk '{print $1}')

if [ "$ACTUAL_SHA1" != "$EXPECTED_SHA1" ]; then
    banner
    red " SHA-1 mismatch."
    red "   expected: $EXPECTED_SHA1   (from $SHA1FILE)"
    red "   actual:   $ACTUAL_SHA1"
    info ""
    info " Size, byte order and region all checked out, so this is not a"
    info " format problem. Most likely causes, in order:"
    info "   - a ROM-hack, translation patch, or trainer applied to the dump"
    info "   - an overdumped or bit-rotted cartridge read"
    info "   - a different revision of the cartridge than the decomp targets"
    info ""
    info " The matching build cannot be byte-identical from this ROM."
    exit 1
fi

grn "ROM OK  $ROM"
grn "        $(region_name "$CC") · z64 big-endian · 12 MiB · sha1 $ACTUAL_SHA1"
