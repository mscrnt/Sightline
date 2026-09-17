/**
 * Native-only scalar types (T4).
 *
 * The N64 SDK's ultra64.h supplies u8..f64, and src/game files that need
 * nothing else from the SDK were including the whole thing to get them. That
 * is a real dependency, not a cosmetic one: ultra64.h pulls in the OS, the
 * RSP/RDP command interface, and the cartridge/VI hardware surface - exactly
 * the coupling Phase 1 removes, and exactly what check_layering.py counts.
 *
 * Defined from stdint so the sizes are the ABI's, not a repeat of the SDK's
 * assumptions. Sizes are asserted below rather than trusted.
 *
 * Never included by the IDO/matching build: those files are #ifndef __sgi and
 * contribute nothing to the ROM.
 *
 * NOTE ON SCOPE. Replacing ultra64.h with this header only severs the
 * dependency for a file that includes NOTHING ELSE from the SDK side. A file
 * that also includes bondgame.h or model.h still gets ultra64.h transitively,
 * because those headers include it themselves - and check_layering.py counts
 * DIRECT includes only. Dropping the direct include there would turn the
 * check green while changing nothing real. Don't.
 */
#ifndef SL_TYPES_H
#define SL_TYPES_H

#ifdef __sgi
#error "sl_types.h is native-only; guard the include with #ifndef __sgi"
#endif

#include <stdint.h>

typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef int16_t  s16;
typedef uint32_t u32;
typedef int32_t  s32;
typedef uint64_t u64;
typedef int64_t  s64;
typedef float    f32;
typedef double   f64;

/* The port assumes these widths everywhere it reads ROM-order data; a silent
 * change would corrupt every byte-positional parser rather than fail. */
typedef char sl_types_size_check[
    (sizeof(u32) == 4 && sizeof(s32) == 4 && sizeof(u64) == 8
     && sizeof(f32) == 4 && sizeof(f64) == 8) ? 1 : -1];

#endif /* SL_TYPES_H */
