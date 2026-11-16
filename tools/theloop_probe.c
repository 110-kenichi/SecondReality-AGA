/* Probe: render one column with controlled inputs and dump it.
 * Used to measure how sensitive the ray march is to the ray direction.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "theloop_gen.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef signed short s16;

static u16 wave1[32768];
static u16 wave2[32768];
static u8 vbuf[65536];

u16 tl_si, tl_di, tl_cx, tl_dy, tl_bp, tl_ax;
const u16 *tl_wave1 = wave1;
const u16 *tl_wave2 = wave2;
u8 *tl_vbuf = vbuf;

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
    FILE *f = fopen("reference/SecondReality/COMAN/W1DTA.BIN", "rb");
    fread(wave1, 2, 32768, f); fclose(f);
    f = fopen("reference/SecondReality/COMAN/W2DTA.BIN", "rb");
    fread(wave2, 2, 32768, f); fclose(f);

    int xw = argc > 1 ? atoi(argv[1]) : 0;
    int yw = argc > 2 ? atoi(argv[2]) : 0;
    int xa0 = argc > 3 ? atoi(argv[3]) : -80;
    int ya = argc > 4 ? atoi(argv[4]) : 160;
    int step = argc > 5 ? atoi(argv[5]) : 1;
    int n = argc > 6 ? atoi(argv[6]) : 6;

    for (int k = 0; k < n; k++) {
        memset(vbuf, 0, sizeof vbuf);
        int xa = xa0 + k * step;
        docol(xw, yw, xa, ya, 199 * 160);
        int top = 200, cnt = 0;
        for (int y = 0; y < 200; y++) {
            if (vbuf[y * 160]) { if (top == 200) top = y; cnt++; }
        }
        printf("xa=%5d top=%3d filled=%3d  colours@(top+2,top+10,top+30)=", xa, top, cnt);
        printf("%3d %3d %3d\n",
               vbuf[(top + 2) * 160], vbuf[(top + 10) * 160], vbuf[(top + 30) * 160]);
    }
    return 0;
}
