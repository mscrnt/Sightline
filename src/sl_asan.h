/**
 * sl_asan.h - teach AddressSanitizer about this game's own allocators.
 *
 * WHY THIS EXISTS. Plain -fsanitize=address is close to useless on this
 * codebase, and it fails in the exact place it is most wanted. The game does
 * not call malloc per object: it takes a few huge blocks and sub-allocates
 * from them itself (mempAllocBytesInBank, memaAlloc). ASan sees the huge
 * blocks and nothing inside them, so a stray write from one game object into
 * another is INSIDE a valid allocation as far as ASan is concerned, and passes
 * silently.
 *
 * That is precisely B-037: a waypoint BFS indexed off the end of its array and
 * wrote -1 into a guard model's animation pointer. Both live in the stage
 * arena. Unannotated ASan would not have said a word, and the bug took three
 * crashes and a day to corner because the crash was always frames away from
 * the write.
 *
 * ASan's manual poisoning API exists for exactly this. Poison an arena when it
 * is reset, unpoison each sub-allocation as it is handed out, and re-poison on
 * free. ASan then knows the object boundaries the allocator knows, and an
 * inter-object write inside the arena becomes a hard error at the instruction
 * that does it, with a backtrace.
 *
 * COST WHEN DISABLED IS ZERO. Without -fsanitize=address these expand to
 * nothing - not a call, not a branch. The normal build is byte-for-byte what
 * it was, which is the whole point: coverage when hunting, no tax when playing.
 *
 * The interface is declared here rather than pulled from
 * <sanitizer/asan_interface.h> because src/ compiles against the N64 SDK
 * include path, which shadows the host headers - the same reason src/sprintf.c
 * declares vsprintf by hand.
 *
 * GRANULARITY CAVEAT, and it is ASan's, not ours: poisoning works on 8-byte
 * shadow granules. The START of an unpoisoned region is exact, but the END is
 * rounded up to the next granule, so a stray write into the last few bytes of
 * the gap after an object can be missed. Objects here are far larger than 8
 * bytes and the wild indices we care about land whole granules away, so this
 * does not affect the bug class being chased.
 */
#ifndef SL_ASAN_H
#define SL_ASAN_H

#if !defined(__sgi) && defined(__SANITIZE_ADDRESS__)

void __asan_poison_memory_region(void const volatile *addr, unsigned long size);
void __asan_unpoison_memory_region(void const volatile *addr, unsigned long size);

/* Mark a span unusable: reading or writing it is an ASan error. */
#define SL_ASAN_POISON(addr, size)   __asan_poison_memory_region((addr), (size))
/* Hand a span back to the program. */
#define SL_ASAN_UNPOISON(addr, size) __asan_unpoison_memory_region((addr), (size))
#define SL_ASAN_ON 1

#else

#define SL_ASAN_POISON(addr, size)   ((void) 0)
#define SL_ASAN_UNPOISON(addr, size) ((void) 0)
#define SL_ASAN_ON 0

#endif
#endif /* SL_ASAN_H */
