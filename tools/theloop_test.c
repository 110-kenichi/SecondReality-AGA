/* Host-side harness for the generated COMAN/THELOOP.INC translation.
 * Renders frames of the 3D-Sinusfield driver (COMAN/MAIN.C doit()) and dumps
 * the raw 160x200 frame buffer so it can be inspected with Python.
 *
 *   gcc -O2 -o theloop_test.exe tools/theloop_test.c src/m2/parts/theloop_gen.c -Isrc/m2/parts
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "theloop_gen.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;

static u16 wave1[32768];
static u16 wave2[32768];
static u8 vbuf[65536];

u16 tl_si, tl_di, tl_cx, tl_dy, tl_bp, tl_ax;
const u16 *tl_wave1 = wave1;
const u16 *tl_wave2 = wave2;
u8 *tl_vbuf = vbuf;

static int sin1024[1024];

static void docol(int si, int di, int cx, int dy, int bp)
{
    tl_si = (u16)(si & ~1);
    tl_di = (u16)(di & ~1);
    tl_cx = (u16)(cx & ~1);
    tl_dy = (u16)(dy & ~1);
    tl_bp = (u16)bp;
    tl_ax = (u16)(s16)(-270);
    theloop_run();
}

int main(int argc, char **argv)
{
    FILE *f;
    int frames = argc > 1 ? atoi(argv[1]) : 1;
    int outn = argc > 2 ? atoi(argv[2]) : 0;

    f = fopen("reference/SecondReality/COMAN/W1DTA.BIN", "rb");
    if (!f) { perror("w1"); return 1; }
    fread(wave1, 2, 32768, f);
    fclose(f);
    f = fopen("reference/SecondReality/COMAN/W2DTA.BIN", "rb");
    if (!f) { perror("w2"); return 1; }
    fread(wave2, 2, 32768, f);
    fclose(f);

    for (int i = 0; i < 1024; i++)
        sin1024[i] = (int)lround(sin(i * 2.0 * M_PI / 1024.0) * 256.0);

    int rot = 0, rot2 = 0, xwav = 0, ywav = 0;
    int xw = 0, yw = 0;

    for (int fr = 0; fr < frames; fr++) {
        rot2 += 4;
        rot += sin1024[rot2 & 1023] / 15;
        int r = rot >> 3;
        int rsin = sin1024[r & 1023];
        int rcos = sin1024[(r + 256) & 1023];
        int rsin2 = sin1024[(r + 177) & 1023];
        int rcos2 = sin1024[(r + 177 + 256) & 1023];

        xw = xwav;
        yw = ywav;
        for (int a = 0; a < 160; a++) {
            int x = a - 80;
            int y = 160;
            int xa = (int)(((long)x * rcos + (long)y * rsin) / 256L);
            int ya = (int)(((long)y * rcos2 - (long)x * rsin2) / 256L);
            int b = (a & 1) * 80 + (a >> 1) + 199 * 160;
            docol(xw, yw, xa, ya, b);
            if (a == 80) {
                xwav += xa * 4;
                ywav += ya * 4;
            }
        }
    }

    {
        char name[64];
        snprintf(name, sizeof name, "run/vbuf_%d.bin", outn);
        f = fopen(name, "wb");
        fwrite(vbuf, 1, 160 * 200, f);
        fclose(f);
        printf("wrote %s (rot=%d xwav=%d ywav=%d)\n", name, rot, xwav, ywav);
    }
    return 0;
}
