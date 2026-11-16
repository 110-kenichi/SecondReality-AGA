#ifndef MUL64_H
#define MUL64_H

/*
 * 32 x 32 -> 64 ビットの符号付き掛け算。結果は (long long)a * b と同じ。
 *
 * -m68020-60 でビルドしているので、gcc は (long long) の掛け算を
 * __muldi3 (support/muldi3.asm、16 ビット掛け算の組み合わせ) の呼び出しに
 * する。実機 68060 の bench (2026-11-15) で 1 回 約 2.5us。
 *
 * CPU ごとに速い方法を使う (2026-11-16 ユーザー指示: 030/040/060 の
 * それぞれで効果があること)。
 *   68020/030/040: MULS.L Dx,Dh:Dl (32x32->64) がハードウェアにある。
 *     命令 1 個 (68030 で約 44 サイクル、68040 で約 20 サイクル。
 *     MC68030UM / MC68040UM の命令実行時間表)。
 *   68060: MULS.L の 64 ビット形が無く、使うと例外で 68060.library が
 *     ソフトウェアで処理するので非常に遅い (MC68060UM 「未実装整数命令」)。
 *     代わりに 16 ビットずつに分けて、32 ビットに収まる掛け算 4 回と
 *     足し算で求める (68060 の MULS.L 32 ビット形は 2 サイクル)。
 *       a = ah * 65536 + al (al は 0..65535)、b も同様。
 *       ah*bh: |ah|,|bh| <= 32768 なので |積| <= 2^30
 *       ah*bl, al*bh: -32768*65535 .. 32767*65535 で 32 ビットに収まる
 *       al*bl: 符号無し 32 ビットに収まる
 * g_cpu_mul64 は main.c が起動時に ExecBase の AttnFlags から決める
 * (AFF_68060 が立っていなければ 1)。
 */
extern int g_cpu_mul64;

static inline long long smul64_split(int a, int b)
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

#if defined(__mc68000__) || defined(__m68k__)
static inline long long smul64_hw(int a, int b)
{
    int hi, lo = a;
    __asm__("muls.l %2,%0:%1" : "=d"(hi), "+d"(lo) : "d"(b) : "cc");
    return (long long)(((unsigned long long)(unsigned int)hi << 32) |
                       (unsigned long long)(unsigned int)lo);
}
#else
static inline long long smul64_hw(int a, int b) { return (long long)a * b; }
#endif

static inline long long smul64(int a, int b)
{
    return g_cpu_mul64 ? smul64_hw(a, b) : smul64_split(a, b);
}

#endif
