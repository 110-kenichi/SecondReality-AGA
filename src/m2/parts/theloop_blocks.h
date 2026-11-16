#ifndef THELOOP_BLOCKS_H
#define THELOOP_BLOCKS_H

#include "../framework/framework.h"

/* One step of the COMAN/THELOOP.INC ray march (128 of them).
 * Layout is fixed because sinus_theloop.asm indexes it directly:
 *   0  eax_inc (u32)
 *   4  bx_add  (s16)
 *   6  dx_add  (s16)
 *   8  mode    (u16)
 *  10  pad
 */
typedef struct {
    u32 eax_inc;
    s16 bx_add;
    s16 dx_add;
    u16 mode;
    u16 pad;
} TlBlock;

extern const TlBlock tl_blocks[128];

#endif
