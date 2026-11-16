/* Reference renderer: SecondReality++ Parts/COMAN/COMAN_MAIN.cpp theloop3(),
 * transcribed verbatim (block table from COMAN_Data.cpp).
 * Used to cross-check our THELOOP.INC translation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kblocks.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;

#define STRIDE 160

static u8 w1[65536];
static u8 w2[65536];
static u8 vbuf[16384 * 4];

static int cameralevel = -270;
static short theloop_xsina1, theloop_ysina1, theloop_xsina2, theloop_ysina2;

static int read_i16_le(const u8 *p) { return (s16)((u16)p[0] | ((u16)p[1] << 8)); }

static unsigned add32_cf(u32 *dst, u32 src)
{
    u32 a = *dst, r = a + src;
    *dst = r;
    return r < a;
}

static unsigned adc32_cf(u32 *dst, u32 src, unsigned cf_in)
{
    unsigned long long s = (unsigned long long)(*dst) + (unsigned long long)src + cf_in;
    *dst = (u32)s;
    return (unsigned)(s >> 32);
}

static unsigned adc16_into_low_u32(u32 *reg32, s16 imm, unsigned cf_in)
{
    u16 lo = (u16)(*reg32 & 0xFFFFu);
    u32 s = (u32)lo + (u32)(u16)imm + (u32)cf_in;
    *reg32 = (*reg32 & 0xFFFF0000u) | (s & 0xFFFFu);
    return s > 0xFFFFu;
}

void theloop3(int xw, int yw, int b)
{
    u32 EAX = 0, ECX = 0xEC00FFFAu;
    u32 ESI = ((u32)xw) & ~1u;
    u32 EDI = ((u32)yw) & ~1u;
    int BP = b;

    for (int i = 0; i < KBLOCKS_SIZE; ++i) {
        const struct TheLoopBlock *blk = &kBlocks[i];

        if (blk->mode == 1) {
            u16 si = (u16)ESI, di = (u16)EDI;
            si = (u16)(si + (u16)theloop_xsina1);
            di = (u16)(di + (u16)theloop_ysina1);
            ESI = (ESI & 0xFFFF0000u) | si;
            EDI = (EDI & 0xFFFF0000u) | di;
        } else {
            u16 si = (u16)ESI, di = (u16)EDI;
            si = (u16)(si + (u16)theloop_xsina2);
            di = (u16)(di + (u16)theloop_ysina2);
            ESI = (ESI & 0xFFFF0000u) | si;
            EDI = (EDI & 0xFFFF0000u) | di;
        }

        s16 BX = read_i16_le(w1 + ESI);
        BX = (s16)(BX + read_i16_le(w2 + EDI));
        BX = (s16)(BX + blk->bx_add);

        if ((s16)(u16)(EAX & 0xFFFFu) >= BX) {
            if (blk->mode == 1) {
                unsigned cf = add32_cf(&EAX, ECX);
                adc16_into_low_u32(&EAX, -1, cf);
            } else {
                unsigned cf = add32_cf(&EAX, ECX);
                cf = adc32_cf(&EAX, ECX, cf);
                adc16_into_low_u32(&EAX, -1, cf);
            }
            continue;
        }

        u8 DL = (u8)((u16)((u16)BX + (u16)blk->dx_add) & 0xFFu);
        DL >>= 1;

        for (;;) {
            unsigned cf = add32_cf(&EAX, blk->eax_inc);
            adc16_into_low_u32(&EAX, 0, cf);
            vbuf[BP] = DL;

            u16 AXu = (u16)(EAX & 0xFFFFu);
            if ((s16)AXu >= BX) {
                unsigned cf1 = adc32_cf(&ECX, 0x00A00000u << 4, 0);
                adc16_into_low_u32(&ECX, 0, cf1);
                BP -= STRIDE;
                break;
            }

            cf = add32_cf(&EAX, blk->eax_inc);
            adc16_into_low_u32(&EAX, 0, cf);
            vbuf[BP - STRIDE] = DL;

            AXu = (u16)(EAX & 0xFFFFu);
            if ((s16)AXu >= BX) {
                BP -= STRIDE;
                unsigned cf_add = add32_cf(&ECX, 0x00A00000u << 4);
                unsigned cf2 = adc32_cf(&ECX, 0x00A00000u << 4, cf_add);
                adc16_into_low_u32(&ECX, 0, cf2);
                BP -= STRIDE;
                break;
            }

            cf = add32_cf(&EAX, blk->eax_inc);
            adc16_into_low_u32(&EAX, 0, cf);
            vbuf[BP - STRIDE * 2] = DL;

            unsigned cf_add = add32_cf(&ECX, 0x01E00000u << 4);
            adc16_into_low_u32(&ECX, 0, cf_add);
            BP -= STRIDE * 3;

            AXu = (u16)(EAX & 0xFFFFu);
            if ((s16)AXu >= BX) break;
        }

        if (blk->mode == 1) {
            unsigned cf = add32_cf(&EAX, ECX);
            adc16_into_low_u32(&EAX, -1, cf);
        } else {
            unsigned cf = add32_cf(&EAX, ECX);
            cf = adc32_cf(&EAX, ECX, cf);
            adc16_into_low_u32(&EAX, -1, cf);
        }
    }
}

static int sin1024[1024];

static void docol(int xw, int yw, int xa, int ya, int b)
{
    theloop_xsina1 = (short)(xa & ~1);
    theloop_ysina1 = (short)(ya & ~1);
    theloop_xsina2 = (short)(theloop_xsina1 << 1);
    theloop_ysina2 = (short)(theloop_ysina1 << 1);
    theloop3(xw & 0xFFFF, yw & 0xFFFF, b);
}

int main(int argc, char **argv)
{
    FILE *f = fopen("reference/SecondReality/COMAN/W1DTA.BIN", "rb");
    if (!f) { perror("w1"); return 1; }
    fread(w1, 1, 65536, f); fclose(f);
    f = fopen("reference/SecondReality/COMAN/W2DTA.BIN", "rb");
    if (!f) { perror("w2"); return 1; }
    fread(w2, 1, 65536, f); fclose(f);

    for (int i = 0; i < 1024; i++) {
        double v = 256.0 * __builtin_sin(i * 2.0 * 3.14159265358979323846 / 1024.0);
        sin1024[i] = (int)(v < 0 ? v - 0.5 : v + 0.5);
    }

    int frames = argc > 1 ? atoi(argv[1]) : 1;
    int outn = argc > 2 ? atoi(argv[2]) : 0;
    int rot = 0, rot2 = 0, xwav = 0, ywav = 0;

    for (int fr = 0; fr < frames; fr++) {
        rot2 += 4;
        rot += sin1024[rot2 & 1023] / 15;
        int r = rot >> 3;
        int rsin = sin1024[r & 1023];
        int rcos = sin1024[(r + 256) & 1023];
        int rsin2 = sin1024[(r + 177) & 1023];
        int rcos2 = sin1024[(r + 177 + 256) & 1023];
        int xw = xwav, yw = ywav;

        memset(vbuf, 0, sizeof vbuf);
        for (int a = 0; a < 160; a++) {
            int x = a - 80, y = 160;
            int xa = (x * rcos + y * rsin) / 256;
            int ya = (y * rcos2 - x * rsin2) / 256;
            int b = (a & 1) * 80 + (a >> 1) + 199 * 160;
            docol(xw, yw, xa, ya, b);
            if (a == 80) { xwav += xa * 4; ywav += ya * 4; }
        }
    }

    char name[64];
    snprintf(name, sizeof name, "run/vbuf_ref_%d.bin", outn);
    f = fopen(name, "wb");
    fwrite(vbuf, 1, 160 * 200, f);
    fclose(f);
    printf("wrote %s\n", name);
    return 0;
}
