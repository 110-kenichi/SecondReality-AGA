#ifndef THELOOP_GEN_H
#define THELOOP_GEN_H

#include "../framework/framework.h"

/* Original COMAN/THELOOP.INC entry state:
 *   si/di = wave table positions, cx = waveX add, dy = waveY add,
 *   bp = bottom of the column in the frame buffer, tl_ax = camera height.
 */
extern const u16 *tl_wave1;
extern const u16 *tl_wave2;
extern u8 *tl_vbuf;

extern u16 tl_si, tl_di, tl_cx, tl_dy, tl_bp, tl_ax;

void theloop_run(void);

#endif
