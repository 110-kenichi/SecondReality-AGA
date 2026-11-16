#ifndef VISU_CLIP_H
#define VISU_CLIP_H

/* VISU/ADRAWCLP.ASM NEWCLIPCALC: sort by the clipped axis before
 * computing the other coordinate with a 14-bit fixed-point fraction. */
static inline s32 visu_clip_other(s32 a, s32 b, s32 va, s32 vb, s32 bound) {
    if (a > b) {
        s32 t = a; a = b; b = t;
        t = va; va = vb; vb = t;
    }
    s32 fraction = ((bound - a) * 16384L) / (b - a);
    return va + (((vb - va) * fraction) >> 14);
}

#endif
