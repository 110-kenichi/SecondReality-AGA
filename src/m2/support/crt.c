/*
 * 最小限の C ランタイム (このプロジェクト独自のコード、Unlicense)。
 *
 * -nostdlib でリンクしているので、gcc が呼ぶ memset/memcpy/memmove/strlen
 * と、実行ファイルの入口 _start をここで用意する。以前は開発環境
 * (vscode-amiga-debug) のテンプレートのファイルを使っていたが、
 * 配布のライセンスを明確にするため自前のものに置き換えた (2026-11-15)。
 * 32bit 割り算などは -m68020-60 でハードウェア命令になるので不要、
 * 64bit 掛け算は muldi3.asm。
 */

/* gcc が下のループを memset/memcpy の呼び出しに置き換えて無限再帰に
 * しないよう、パターン置き換えを止める。 */
#define NO_PATTERNS __attribute__((optimize("no-tree-loop-distribute-patterns")))

int main(void);

/* 実行ファイルの入口。.text.unlikely に置くとリンカがコードの先頭に
 * 配置する (elf2hunk の入口は最初のハンクの先頭)。 */
__attribute__((used)) __attribute__((section(".text.unlikely")))
int _start(void) {
    return main();
}

NO_PATTERNS
unsigned long strlen(const char *s) {
    const char *p = s;
    while (*p)
        p++;
    return (unsigned long)(p - s);
}

/* 4 バイト境界では 32bit で書く (チップ RAM への書き込みを減らす)。 */
NO_PATTERNS
void *memset(void *dest, int val, unsigned long len) {
    unsigned char *p = (unsigned char *)dest;
    unsigned char c = (unsigned char)val;
    while (len && ((unsigned long)p & 3)) {
        *p++ = c;
        len--;
    }
    if (len >= 4) {
        unsigned long v = c;
        v |= v << 8;
        v |= v << 16;
        unsigned long *lp = (unsigned long *)p;
        unsigned long n = len >> 2;
        while (n--)
            *lp++ = v;
        p = (unsigned char *)lp;
        len &= 3;
    }
    while (len--)
        *p++ = c;
    return dest;
}

/* 送り先と送り元の境界がそろうときは 32bit (または 16bit) で写す。 */
NO_PATTERNS
void *memcpy(void *dest, const void *src, unsigned long len) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    unsigned long mis = (unsigned long)d ^ (unsigned long)s;
    if ((mis & 3) == 0) {
        while (len && ((unsigned long)d & 3)) {
            *d++ = *s++;
            len--;
        }
        unsigned long *ld = (unsigned long *)d;
        const unsigned long *ls = (const unsigned long *)s;
        unsigned long n = len >> 2;
        while (n--)
            *ld++ = *ls++;
        d = (unsigned char *)ld;
        s = (const unsigned char *)ls;
        len &= 3;
    } else if ((mis & 1) == 0) {
        if (len && ((unsigned long)d & 1)) {
            *d++ = *s++;
            len--;
        }
        unsigned short *wd = (unsigned short *)d;
        const unsigned short *ws = (const unsigned short *)s;
        unsigned long n = len >> 1;
        while (n--)
            *wd++ = *ws++;
        d = (unsigned char *)wd;
        s = (const unsigned char *)ws;
        len &= 1;
    }
    while (len--)
        *d++ = *s++;
    return dest;
}

NO_PATTERNS
void *memmove(void *dest, const void *src, unsigned long len) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || len == 0)
        return dest;
    if (d < s)
        return memcpy(dest, src, len);
    d += len;
    s += len;
    while (len--)
        *--d = *--s;
    return dest;
}
