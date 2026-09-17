#include <ultra64.h>
#include "bondtypes.h"
#include "initunk_005450.h"

#define MODELHITENTRIES_LEN 600

extern struct ModelHitEntry *g_ModelHitFreeList; // canonically freedist

/**
 * These are linker/BSS labels. g_ModelHitEntries is the start of a
 * ModelHitEntry[600] pool. g_ModelHitEntriesPenultimate labels entry 598
 * inside that same pool and is kept as a raw char symbol so IDO emits the
 * original relocation.
 */
extern char g_ModelHitEntries[]; // dword_CODE_bss_80076A50
extern char g_ModelHitEntriesPenultimate[]; // dword_CODE_bss_80079908

/**
 * Address: 7F005450
 * 
 * Called by stage load.
 * 
 * Initializes a fixed pool of ModelHitEntry records used while building per-model hit/collision traversal lists.
 * The pool is threaded as a doubly linked free list and consumed by objecthandler.c.
 */
void initModelHitEntryFreeList(void)
{
    s32 i;
    ModelHitEntry *entries = (ModelHitEntry *)g_ModelHitEntries;

#ifndef __sgi
    /* Sightline native: the decomp declares this pool as ~30 fragment
     * symbols whose N64 bss layout happens to be contiguous; the free list
     * is threaded across all of them. -fno-toplevel-reorder reproduces the
     * layout natively - this assert proves it before the threading loop can
     * shred whatever would otherwise follow (it reached gfxFrameMsgQ). */
    {
        extern char g_ModelHitEntriesPenultimate[];
        if ((unsigned long) g_ModelHitEntriesPenultimate
            - (unsigned long) g_ModelHitEntries != 0x2EB8ul /* Penultimate sits at +0x2EB8 in the overlay */) {
            osSyncPrintf("sightline native: ModelHit pool layout broken (delta 0x%lx)\n",
                (unsigned long) g_ModelHitEntriesPenultimate - (unsigned long) g_ModelHitEntries);
            *(volatile int *) 0 = 0;   /* fail loudly, never silently corrupt */
        }
    }
#endif
    g_ModelHitFreeList = entries;

    entries[0].next = &entries[1];

    for (i = 1; i < MODELHITENTRIES_LEN - 1; i++)
    {
        entries[i].next = &entries[i + 1];
        entries[i].prev = &entries[i - 1];
    }

    ((ModelHitEntry *)g_ModelHitEntries)[MODELHITENTRIES_LEN - 1].prev = (ModelHitEntry *)g_ModelHitEntriesPenultimate;
}
