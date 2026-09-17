#!/bin/sh
# Native skeleton build. Everything derived, nothing committed: objects,
# generated stubs and the binary live under build/native/.
#
# chraidata.c is excluded (AI-script DSL still leans on IDO argument-filling
# below the SWITCH layer); crash.c is excluded (the crash handler is
# MIPS-specific by nature). audi.c builds natively as of the audio seam work:
# its lone blocker was `s32 sp48[50] = CUSTOM_FX_PARAMS_N;`, an IDO-only
# array-from-array initializer, now a bcopy under #ifndef __sgi.
#
# The al* synthesis library (src/libultra/audio, src/libultrare/audio) IS in
# the glob as of 2026-08-30, and audio is enabled: music.c's native early return
# and snd.c's native g_sndBootswitchSound override are both gone. Stubs 56 -> 30.
#
# It stayed out for a long time, and the history matters because each attempt
# ended at a REAL defect rather than at a tuning knob. In order:
#   1. musicSeqPlayerInit early-returned, so g_musicXTrack1SeqPlayer was NULL
#      and musicTrack1ApplySeqpVol faulted on &seqp->evtq. Link and init are
#      coupled in BOTH directions - neither alone is safe.
#   2. alFxNew got NULL from alHeapAlloc. Not a small heap: seqCount was read
#      byte-swapped, making one allocation 252x too large. Fixed at the load
#      choke point, not by enlarging MUSIC_ALLOCATION_BYTES.
#   3. alCSeqNew ran off a wild pointer at frame 501 - the ALCMidiHdr inside the
#      decompressed sequence, another byte-order boundary.
#   4. __initFromBank walked off a bank whose instArray was entirely wild - the
#      ALBankFile (.ctl) graph, never normalised. sl_swap_bankfile now does it,
#      leaving the two arrays the RSP reads (ADPCM book, loop state) big-endian.
#   5. amMain never ran, so its prologue never ran, so the audio client was
#      never on the scheduler's client list and the frame queue stayed empty.
#      amStartAudioThread now performs that prologue cooperatively.
set -e
cd "$(dirname "$0")/../.."
# SL_ASAN=1 builds an AddressSanitizer variant into its OWN directory, so the
# normal binary is never clobbered by a ~2x-slower debug build. See sl_asan.h:
# the game sub-allocates from its own arenas, so ASan only becomes useful once
# those arenas annotate their objects - which memp.c and mema.c now do.
if [ -n "${SL_ASAN:-}" ]; then
    OUT=build/native-asan
    # -fsanitize-recover=address lets ASAN_OPTIONS=halt_on_error=0 keep going
    # after a finding instead of dying on the first one. Without it a single
    # benign overrun at boot hides everything behind it, which is exactly what
    # happened on the first prototype run.
    ASAN_CFLAGS="-fsanitize=address -fsanitize-recover=address -fno-omit-frame-pointer"
    ASAN_LIBS="-fsanitize=address"
else
    OUT=build/native
    ASAN_CFLAGS=""
    ASAN_LIBS=""
fi
mkdir -p $OUT
# The RESAMPLE polyphase table is ROM-derived: generated locally into
# $OUT (gitignored), never committed. Fails loudly if the extracted
# microcode segment is missing.
${PY:-.venv/bin/python3} tools/native/gen_resample_tab.py || exit 1
DEFS="-DVERSION_US -DLANG_US -DREFRESH_NTSC -DLEFTOVERDEBUG -DLEFTOVERSPECTRUM -DBUGFIX_R0 -DBYTEMATCH -DTARGET_N64 -D_LANGUAGE_C"
INC="-I. -Iinclude -Iinclude/PR -Isrc -Isrc/game -Isrc/inflate -Isrc/libultra"
# Measurement scaffolding flags, applied to BOTH compile classes. Decomp files
# get $DEFS $INC; src/platform and src/gfx deliberately do not, so a -D added to
# INC silently never reached sl_main.c - the reporters there only appeared to
# work because they were unguarded. Set explicitly, e.g.
#   SL_TRACE_DEFS=-DSL_ALHEAP_TRACE tools/native/build.sh
TRACE_DEFS="${SL_TRACE_DEFS:-}"

# Objects are reused when their .c is unchanged, which is right for source edits
# and WRONG when only flags change: a production build after a measurement build
# silently relinked stale objects still containing the guarded reporters, so an
# `nm` check on the binary reported symbols the current flags exclude. That
# defeats the artefact check itself. Stamp the flags and discard objects when
# they change, so the built binary always matches the flags asked for.
STAMP="$OUT/.trace_defs"
if [ ! -f "$STAMP" ] || [ "$(cat "$STAMP" 2>/dev/null)" != "$TRACE_DEFS" ]; then
    rm -f "$OUT"/*.o 2>/dev/null || true
fi
mkdir -p "$OUT"; printf '%s' "$TRACE_DEFS" > "$STAMP"
CC="${CC:-gcc}"
# 32-bit SDL2/GL for the window backend, matching the -m32 skeleton
# (libsdl2-dev:i386). Absent -> the null backend still builds and the sim runs
# headless, so a missing SDL is never fatal.
PKGI=/usr/lib/i386-linux-gnu/pkgconfig
SDL_CFLAGS="$(PKG_CONFIG_PATH=$PKGI pkg-config --cflags sdl2 2>/dev/null || echo -I/usr/include/SDL2)"
SDL_LIBS="$(PKG_CONFIG_PATH=$PKGI pkg-config --libs sdl2 2>/dev/null || echo -lSDL2) -lGL"
CFLAGS="$ASAN_CFLAGS -m32 -g -std=gnu89 -fno-toplevel-reorder -fms-extensions -fno-builtin -fcommon -w -msse2 -mfpmath=sse"
# newest header anywhere in the include graph: a header edit must
# recompile every object, or stale objects silently keep old semantics
HDR_STAMP=$(ls -t src/*.h src/game/*.h include/*.h include/PR/*.h 2>/dev/null | head -1)
n=0
# Objects whose source has moved or been deleted are still matched by the
# $OUT/*.o link glob below. That is not a stale-build annoyance, it is a
# correctness hazard: moving a file produced "multiple definition" against its
# own new object, and a deletion would have silently linked dead code instead.
# Record what SHOULD exist, and prune the rest before linking.
KEEP="$OUT/.keep-objs"
: > "$KEEP" 
for f in src/game/*.c src/native/*.c src/gfx/*.c src/*.c src/inflate/*.c src/platform/*.c src/libultra/gu/*.c src/libultra/audio/*.c src/libultrare/audio/*.c; do
  b=$(echo "$f" | sed 's|/|_|g; s|\.c$||')
  case "$f" in
    */crash.c) continue ;;
  esac
  echo "$b.o" >> "$KEEP"
  if [ "$f" -nt "$OUT/$b.o" ] 2>/dev/null || [ -n "$HDR_STAMP" -a "$HDR_STAMP" -nt "$OUT/$b.o" ] 2>/dev/null || [ ! -f "$OUT/$b.o" ]; then
    case "$f" in
      */chraidata.c)
        # same PRINT-string conversion the matching build applies
        sed -E -f tools/native/aiprint.sed "$f" | $CC $CFLAGS $DEFS $INC $TRACE_DEFS -x c - -c -o "$OUT/$b.o" ;;
      src/platform/*|src/gfx/*)
        # Host-side code: the N64 SDK include tree shadows compiler headers
        # (its stdarg.h has no __gnuc_va_list), so platform files compile
          # against the host headers alone. SDL is 32-bit to match -m32.
          # -Werror=implicit-function-declaration on NATIVE code only.
          #
          # B-045 spent a day on a fog "algorithm" bug that was an ABI
          # violation: glFogCoordf is GL 1.4 and Mesa's <GL/gl.h> declares
          # only through 1.3, so the call had no prototype. Default argument
          # promotion then pushed the float as an 8-byte double while the
          # entry point read 4 bytes, so on this -m32 build it took the wrong
          # half - a correct fog value in, an unrelated bit pattern out, and
          # a small change of input producing a wildly different result. That
          # is exactly the frame-to-frame flicker, and every measurement of
          # the VALUES looked healthy throughout because the values were.
          #
          # -w hid it. The decomp files keep -w (bg.c alone has 22 implicit
          # declarations and fixing those is separate work with matching-build
          # risk), but nothing new in src/gfx, src/platform or src/native gets
          # to repeat this.
          $CC $ASAN_CFLAGS -m32 -w -Werror=implicit-function-declaration \
              -msse2 -mfpmath=sse -I"$OUT" $SDL_CFLAGS $TRACE_DEFS -c "$f" -o "$OUT/$b.o" ;;
      *)
        $CC $CFLAGS $DEFS $INC $TRACE_DEFS -c "$f" -o "$OUT/$b.o" ;;
    esac
  fi
  n=$((n+1))
done
pruned=0
for o in "$OUT"/*.o; do
  [ -e "$o" ] || continue
  ob=$(basename "$o")
  case "$ob" in
    stubs.o|segments.o|modelhit_pool.o) continue ;;
  esac
  if ! grep -qx "$ob" "$KEEP" 2>/dev/null; then
    rm -f "$o"
    pruned=$((pruned+1))
  fi
done
[ "$pruned" -gt 0 ] && echo "  pruned $pruned orphaned object(s)"
echo "  objects: $n"
# link once to harvest unresolved symbols, generate stubs, link for real
# NOTE: the trial link MUST pass the same libraries as the real one. It
# harvests undefined symbols and the generator turns each into a no-op stub,
# so a library missing here gets silently stubbed out instead of linked:
# SDL_Init became "return 0", which is SDL's SUCCESS code, and the video
# subsystem quietly did nothing while reporting success.
rm -f $OUT/stubs.o $OUT/stubs.c $OUT/segments.o
# modelhit_pool.o MUST exist before the trial link, not after it. It defines
# g_ModelHitEntries; if the harvest runs without it, that symbol comes back
# undefined, gets a no-op stub, and the real link then sees both the stub and
# the pool - "multiple definition". A warm build hides this completely, because
# the object survives from the previous run and resolves during the harvest, so
# it only ever fails on a fresh checkout - which is the one build a new
# contributor does.
$CC -m32 -c tools/native/modelhit_pool.s -o $OUT/modelhit_pool.o
$CC $ASAN_LIBS -m32 -no-pie $OUT/*.o -o /dev/null -lm $SDL_LIBS 2> $OUT/link1.err || true
sed -n "s/.*undefined reference to [\`']\([A-Za-z_][A-Za-z0-9_]*\)'.*/\1/p" $OUT/link1.err \
  | sort -u > $OUT/unresolved.txt
# map-known symbols become real segment windows and rom-offset absolutes;
# only what the map does not know gets stubbed
python3 tools/native/gen_segments.py build/u/ge007.u.map $OUT/unresolved.txt $OUT/segments.s
$CC -m32 -c $OUT/segments.s -o $OUT/segments.o
sort $OUT/unresolved.txt > $OUT/u1
comm -23 $OUT/u1 $OUT/segments.s.covered > $OUT/unstubbed.txt
mv $OUT/unstubbed.txt $OUT/unresolved.txt
{
  echo "/* auto-generated: symbols the native link cannot yet resolve."
  echo "   Retired piece by piece by the shim, asset transcode, and ports. */"
  while read s; do
    [ -n "$s" ] || continue
    # callable no-ops: a data-shaped stub lives in non-executable memory and
    # segfaults the moment the game CALLS it (guScale did). A function symbol
    # satisfies data references too; anything that WRITES to one will fault
    # loudly, which is the correct behavior for un-ported state.
    echo "long $s(void) { return 0; }"
  done < $OUT/unresolved.txt
} > $OUT/stubs.c
$CC -m32 -w -c $OUT/stubs.c -o $OUT/stubs.o
$CC $ASAN_LIBS -m32 -no-pie $OUT/*.o -o $OUT/skeleton -lm $SDL_LIBS
echo "  stubs: $(wc -l < $OUT/unresolved.txt)"
echo "  built: $OUT/skeleton"
