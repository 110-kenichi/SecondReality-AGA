#ifndef MUL64_H
#define MUL64_H

/*
 * 32 x 32 -> 64 ビットの符号付き掛け算。結果は (long long)a * b と同じ。
 *
 * 68060 には 32x32->64 の MULS.L が無いので、(long long) の掛け算は
 * __muldi3 (support/muldi3.asm) の呼び出しになり、実機 bench (2026-11-15) で
 * 1 回 約 2.5us かかる。U2A/U2E は頂点 1 個の回転に 9 回使っていた。
 * ここでは 16 ビットずつに分け、32 ビットに収まる掛け算 4 回と足し算で
 * 求める (2026-11-16)。
 *   a = ah * 65536 + al (al は 0..65535)、b も同様。
 *   ah*bh: |ah|,|bh| <= 32768 なので |積| <= 2^30
 *   ah*bl, al*bh: -32768*65535 .. 32767*65535 で 32 ビット符号付きに収まる
 *   al*bl: 符号無し 32 ビットに収まる
 * 足し合わせは 2 の補数のまま符号無し 64 ビットで行う。
 */
static inline long long smul64(int a, int b)
{
    int ah = a >> 16, bh = b >> 16;
    unsigned int al = (unsigned int)a & 0xffffu, bl = (unsigned int)b & 0xffffu;
    long long hh = (long long)(ah * bh);
    long long hl = (long long)(ah * (int)bl) + (long long)((int)al * bh);
    unsigned int ll = al * bl;
    return (long long)(((unsigned long long)hh << 32) +
                       ((unsigned long long)hl << 16) +
                       (unsigned long long)ll);
}

#endif
