/*====================================================================
 * heapalloc.c
 *
 * Copyright 1995, Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Silicon Graphics,
 * Inc.; the contents of this file may not be disclosed to third
 * parties, copied or duplicated in any form, in whole or in part,
 * without the prior written permission of Silicon Graphics, Inc.
 *
 * RESTRICTED RIGHTS LEGEND:
 * Use, duplication or disclosure by the Government is subject to
 * restrictions as set forth in subdivision (c)(1)(ii) of the Rights
 * in Technical Data and Computer Software clause at DFARS
 * 252.227-7013, and/or in similar or successor clauses in the FAR,
 * DOD or NASA FAR Supplement. Unpublished - rights reserved under the
 * Copyright Laws of the United States.
 *====================================================================*/

#include "synthInternals.h"
#include <libaudio.h>
#include <os.h>
#include <ultraerror.h>

void *alHeapDBAlloc(u8 *file, s32 line, ALHeap *hp, s32 num, s32 size)
{
    s32 bytes;
    u8 *ptr = 0;

    bytes = (num*size + AL_CACHE_ALIGN) & ~AL_CACHE_ALIGN;
#ifdef SL_ALHEAP_TRACE
    /* Demand instrumentation, compiled out unless SL_ALHEAP_TRACE is defined,
     * so the matching build sees byte-identical source. No I/O and no headers
     * here - the repo shadows <stdio.h> with a minimal N64 one - the records
     * are printed from the platform layer. */
    {
        extern long sl_alheap_n, sl_alheap_req[], sl_alheap_used[],
                    sl_alheap_len[], sl_alheap_line[];
        extern unsigned char sl_alheap_ok[];
        extern const char *sl_alheap_file[];
        if (sl_alheap_n < 512) {
            long i = sl_alheap_n++;
            sl_alheap_req[i]  = (long) bytes;
            sl_alheap_used[i] = (long)(hp->cur - hp->base);
            sl_alheap_len[i]  = (long) hp->len;
            sl_alheap_ok[i]   = ((hp->cur + bytes) <= (hp->base + hp->len));
            sl_alheap_file[i] = file ? (const char *) file : "?";
            sl_alheap_line[i] = (long) line;
        }
    }
#endif
    
#ifdef _DEBUG
    hp->count++;    
    bytes += sizeof(HeapInfo);
#endif
    
    if ((hp->cur + bytes) <= (hp->base + hp->len)) {

        ptr = hp->cur;
        hp->cur += bytes;

#ifdef _DEBUG    
        ((HeapInfo *)ptr)->magic = AL_HEAP_MAGIC;
        ((HeapInfo *)ptr)->size  = bytes;
        ((HeapInfo *)ptr)->count = hp->count;
        if (file) {
            ((HeapInfo *)ptr)->file  = file;
            ((HeapInfo *)ptr)->line  = line;
        } else {
            ((HeapInfo *)ptr)->file  = (u8 *) "unknown";
            ((HeapInfo *)ptr)->line  = 0;
        }
        
        ptr += sizeof(HeapInfo);        
#endif

    } else {
#ifdef _DEBUG
        __osError(ERR_ALHEAPNOFREE, 1, size);
#endif        
    }

    return ptr;
}
