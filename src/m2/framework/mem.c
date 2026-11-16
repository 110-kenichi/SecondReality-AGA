#include "mem.h"
#include <proto/exec.h>
#include <exec/memory.h>

#define MEM_SLOTS 128
static void *mem_ptr[MEM_SLOTS];
static u32 mem_size[MEM_SLOTS];

void *mem_alloc(u32 size, u32 flags) {
    void *p = AllocMem(size, flags);
    if (!p)
        return 0;
    for (int i = 0; i < MEM_SLOTS; i++) {
        if (!mem_ptr[i]) {
            mem_ptr[i] = p;
            mem_size[i] = size;
            return p;
        }
    }
    return p;   /* 表が一杯 (想定外): 返せないが動作は続ける */
}

void mem_free(void *p, u32 size) {
    if (!p)
        return;
    for (int i = 0; i < MEM_SLOTS; i++) {
        if (mem_ptr[i] == p) {
            mem_ptr[i] = 0;
            break;
        }
    }
    FreeMem(p, size);
}

void mem_free_all(void) {
    for (int i = 0; i < MEM_SLOTS; i++) {
        if (mem_ptr[i]) {
            FreeMem(mem_ptr[i], mem_size[i]);
            mem_ptr[i] = 0;
        }
    }
}
