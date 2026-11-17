/*
 * Second Reality A1200: LNS&ZOOM (Lens + Rotazoomer)
 *
 * オリジナル: reference/SecondReality/LENS/MAIN.C + ASM.ASM + CALC.C
 *
 *   part1: 2 本のカーテン状のラインで背景画像をワイプイン (300 フレーム)
 *   part2: レンズ (LENS.EX1..EX4 の表示リスト) をパスに沿って移動
 *   part3: ロトズーマー (LENS.EXP のパスで 256x256 画像を回転/ズーム)
 *
 * 画面は 320x200 を 320x256 の中央 (y=28) に置く。
 */
#include "scenes.h"
#include "../framework/video.h"
#include "../framework/vblank.h"
#include "../framework/dis.h"
#include "../framework/c2p.h"
#include "../music/music.h"
#define LENS_POS() (((int)np_ord << 8) | (np_row & 0xff))

extern const u8 *g_lens_back;
extern const u8 *g_lens_pal;      /* 768 bytes (8bit RGB) */
extern const u8 *g_lens_ex0;      /* s16 wid, s16 hig, 9 bytes RGB offsets */
extern const u8 *g_lens_ex1;
extern const u8 *g_lens_ex2;
extern const u8 *g_lens_ex3;
extern const u8 *g_lens_ex4;
extern const u8 *g_lens_path;     /* u16 n1, path1(n1), path2(...) */

#define LW 320
#define LH 200
#define LENS_YOFF 28
#define LENS_PIXELS (LW * LH)

static u8 back[LENS_PIXELS];        /* 背景 (64 色) */
static u8 rotpic[256 * 256];        /* part3 用 */
static u8 rotpic90[256 * 256];
static u8 pal6[768];                /* 6bit 256 色 (base + lens) */
static u8 pal8[768];                /* 8bit (video_set_palette 用) */

static int lenswid, lenshig, lensxs, lensys;
static int rgb_add[9];
static const u16 *tbl[5];           /* 1..4 */
static const u16 *path1;            /* int16 pairs */
static const u16 *path2;            /* int16 quads */
static int path1_frames;

static int phase;                   /* -1=開始待ち 0=wipe 10=part2 待ち
                                     * 1=lens 2=rotazoom */
static int logframe;                /* 70Hz 換算の経過フレーム (段階ごと) */
static u32 phase_vbl;               /* 段階の開始 VBL */
static int lens_done;

/* 画面転送 (c2p) を、chunky に書いた範囲だけにする (2026-11-15 ユーザー承認)。
 * フレームごとに書き込んだ範囲の外枠 (wr_*、窓の中の座標) を記録し、
 * 裏画面ごとに「前回変換してから書いた範囲」(pend) だけを変換する。
 * レンズ (part2) は表示リストで書く範囲だけ、ワイプ (part1) は線の位置
 * だけ、回転ズーム (part3) と画面消去は窓全体になる。
 * 書かなかった場所は chunky も裏画面も変わっていないので、全体を
 * 変換したのと同じプレーンになる。初めて使う裏画面は 256 行全体を変換する。 */
struct lr { s16 y0, y1, x0, x1; };           /* 窓の中の行 y0..y1-1、列 x0..x1-1 */
static int wr_y0, wr_y1, wr_x0, wr_x1;
static u8 *lz_buf[VIDEO_NBUF];
static struct lr lz_pend[VIDEO_NBUF][3];
static int lz_npend[VIDEO_NBUF];

static inline void wr_mark(int y, int xa, int xb)   /* 行 y の xa..xb (含む) */
{
    if (y < wr_y0) wr_y0 = y;
    if (y + 1 > wr_y1) wr_y1 = y + 1;
    if (xa < wr_x0) wr_x0 = xa;
    if (xb + 1 > wr_x1) wr_x1 = xb + 1;
}

/* 線形の書き込み位置 lo..hi (含む、窓の中) を外枠に加える */
static void wr_mark_lin(int lo, int hi)
{
    if (lo < 0) lo = 0;
    if (hi > LENS_PIXELS - 1) hi = LENS_PIXELS - 1;
    if (hi < lo)
        return;
    int ya = lo / LW, yb = hi / LW;
    if (ya == yb)
        wr_mark(ya, lo - ya * LW, hi - ya * LW);
    else {
        wr_mark(ya, 0, LW - 1);
        wr_mark(yb, 0, LW - 1);
    }
}

static int lr_area(int y0, int y1, int x0, int x1)  /* 32 ドット列 x 行 */
{
    return (y1 - y0) * (((x1 + 31) >> 5) - (x0 >> 5));
}

static void lz_add(int slot, const struct lr *r)
{
    /* 既にある枠とまとめたほうが変換量が増えないなら、まとめる
     * (レンズは少しずつ動くので前回の枠とほぼ重なる。回転ズームは毎回
     * 窓全体なので 1 つになる)。 */
    for (int i = 0; i < lz_npend[slot]; i++) {
        struct lr *m = &lz_pend[slot][i];
        int y0 = m->y0 < r->y0 ? m->y0 : r->y0, y1 = m->y1 > r->y1 ? m->y1 : r->y1;
        int x0 = m->x0 < r->x0 ? m->x0 : r->x0, x1 = m->x1 > r->x1 ? m->x1 : r->x1;
        if (lr_area(y0, y1, x0, x1) <=
            lr_area(m->y0, m->y1, m->x0, m->x1) + lr_area(r->y0, r->y1, r->x0, r->x1)) {
            m->y0 = (s16)y0; m->y1 = (s16)y1; m->x0 = (s16)x0; m->x1 = (s16)x1;
            return;
        }
    }
    if (lz_npend[slot] < 3) {
        lz_pend[slot][lz_npend[slot]++] = *r;
        return;
    }
    struct lr *m = &lz_pend[slot][2];      /* あふれたら最後の枠に合わせる */
    if (r->y0 < m->y0) m->y0 = r->y0;
    if (r->y1 > m->y1) m->y1 = r->y1;
    if (r->x0 < m->x0) m->x0 = r->x0;
    if (r->x1 > m->x1) m->x1 = r->x1;
}

static void lz_c2p(const u8 *chunky, u8 *planes)
{
    struct lr cur;
    int have = wr_y1 > wr_y0 && wr_x1 > wr_x0;
    if (have) {
        cur.y0 = (s16)wr_y0; cur.y1 = (s16)wr_y1;
        cur.x0 = (s16)wr_x0; cur.x1 = (s16)wr_x1;
        for (int i = 0; i < VIDEO_NBUF; i++)
            lz_add(i, &cur);
    }
    /* 画面バッファの番号 (トリプルバッファ、2026-11-17) */
    int slot = video_back_index();
    if (lz_buf[slot] != planes) {
        lz_buf[slot] = planes;
        c2p8_fast(chunky, planes, LW, 256);
    } else {
        for (int i = 0; i < lz_npend[slot]; i++) {
            const struct lr *r = &lz_pend[slot][i];
            c2p8_cols(chunky + (LENS_YOFF + r->y0) * LW, planes,
                      LENS_YOFF + r->y0, r->y1 - r->y0, r->x0 >> 5, (r->x1 + 31) >> 5);
        }
    }
    lz_npend[slot] = 0;
}

/* オリジナルは VGA 70Hz。Amiga は 50Hz なので、経過時間を
 * 70Hz フレームに換算してパスを進める (1 VBL = 7/5 フレーム)。
 * 以前は描画 1 回ごとに vbl++ していたため、描画が遅いと遅れた。
 * 経過時間は vblank_frames() の差分から取る (AGENTS.md)。 */
static int logical_frame(void) {
    return (int)((video_frame_vbl() - phase_vbl) * 7u / 5u);
}

/* 原作 LENS/MAIN.C:
 *   part1 (90-133): while(dis_musplus()<-6); dis_setmframe(0); 300 フレーム
 *   main (379-381): part1(); while(dis_musplus()<-20); part2(); part3();
 *   part3 (211-213): while(frame<2000) { if(dis_musplus()>-4) break; ... } */
int lens_finished(void) {
    return lens_done;
}

/* ---- テーブル 1 行分の描画 ----
 * u1: 320x200 座標系の画面オフセット, y: テーブルの行
 * ex1: 連続した destination, source = di + delta
 * ex2/3: (dest delta, src delta) のペア
 * ex4: 背景の復元 (dest = src = di + delta)
 */
static void draw_row(u8 *dst, const u8 *backp, int u1, int y)
{
    const u16 *t;
    const u16 *list;
    int start, count, di, i;

    /* layer 1 */
    t = tbl[1];
    start = (int)(s16)t[y * 2];
    count = (int)(s16)t[y * 2 + 1];
    if (count > 0) {
        list = (const u16 *)((const u8 *)t + start);
        di = (int)u1 + (s16)list[0];
        for (i = 0; i < count; i++) {
            int src = di + (s16)list[1 + i];
            int dd = di + i;
            if ((u32)src < LENS_PIXELS && (u32)dd < LENS_PIXELS)
                dst[dd] = backp[src] | 0x40;
        }
        wr_mark_lin(di, di + count - 1);
    }
    /* layers 2, 3 */
    for (int layer = 2; layer <= 3; layer++) {
        t = tbl[layer];
        start = (int)(s16)t[y * 2];
        count = (int)(s16)t[y * 2 + 1];
        if (count <= 0)
            continue;
        list = (const u16 *)((const u8 *)t + start);
        di = (int)u1 + (s16)list[0];
        int lo = 0x7fffffff, hi = -1;
        for (i = 0; i < count; i++) {
            int dd = di + (s16)list[1 + i * 2];
            int src = di + (s16)list[2 + i * 2];
            if ((u32)src < LENS_PIXELS && (u32)dd < LENS_PIXELS) {
                dst[dd] = backp[src] | (u8)(layer == 2 ? 0x80 : 0xC0);
                if (dd < lo) lo = dd;
                if (dd > hi) hi = dd;
            }
        }
        wr_mark_lin(lo, hi);
    }
    /* layer 4: restore */
    t = tbl[4];
    start = (int)(s16)t[y * 2];
    count = (int)(s16)t[y * 2 + 1];
    if (count > 0) {
        list = (const u16 *)((const u8 *)t + start);
        di = (int)u1 + (s16)list[0];
        int lo = 0x7fffffff, hi = -1;
        for (i = 0; i < count; i++) {
            int p = di + (s16)list[1 + i];
            if ((u32)p < LENS_PIXELS) {
                dst[p] = backp[p];
                if (p < lo) lo = p;
                if (p > hi) hi = p;
            }
        }
        wr_mark_lin(lo, hi);
    }
}

static void draw_lens(u8 *dst, int x0, int y0)
{
    int top = (x0 - lensxs) + (y0 - lensys) * LW;
    int ys = lenshig / 2;
    int ye = lenshig - 1;
    for (int y = 0; y < ys; y++) {
        int u1 = top + y * LW;
        int u2 = (x0 - lensxs) + (y0 + lensys - 1) * LW - y * LW;
        if (u1 >= -LENS_PIXELS && u1 <= LENS_PIXELS)
            draw_row(dst, back, u1, y);
        if (u2 >= -LENS_PIXELS && u2 <= LENS_PIXELS)
            draw_row(dst, back, u2, ye - y);
    }
}

/* ---- パレット ---- */
static void build_palette(void)
{
    /* pal6: base 0..63 + 3 shifted groups。
     * g_lens_pal は 8bit (6bit<<2) なので 6bit に戻す。 */
    for (int i = 0; i < 192; i++)
        pal6[i] = (u8)(g_lens_pal[i] >> 2);
    for (int gi = 0; gi < 3; gi++) {
        int r = rgb_add[gi * 3 + 0];
        int g = rgb_add[gi * 3 + 1];
        int b = rgb_add[gi * 3 + 2];
        for (int c = 0; c < 64; c++) {
            int o = (64 * (gi + 1) + c) * 3;
            int v;
            v = (g_lens_pal[c * 3 + 0] >> 2) + r; if (v > 63) v = 63;
            pal6[o + 0] = (u8)v;
            v = (g_lens_pal[c * 3 + 1] >> 2) + g; if (v > 63) v = 63;
            pal6[o + 1] = (u8)v;
            v = (g_lens_pal[c * 3 + 2] >> 2) + b; if (v > 63) v = 63;
            pal6[o + 2] = (u8)v;
        }
    }
    for (int i = 0; i < 768; i++)
        pal8[i] = (u8)(pal6[i] << 2);
}

static void set_pal_from6(const u8 *p6)
{
    for (int i = 0; i < 768; i++)
        pal8[i] = (u8)(p6[i] << 2);
    video_set_palette(pal8);
}

/* part2: レンズ用パレット (64..255) をフェードイン */
static void lens_fade_step(int level)
{
    /* level 0..31: 0 = offset のみ, 31 = 完全なシフトパレット */
    if (level < 0) level = 0;
    if (level > 31) level = 31;
    for (int c = 64; c < 256; c++) {
        for (int k = 0; k < 3; k++) {
            int y = c * 3 + k;
            int a = (c - 64) * 3 + k;
            int v = (int)pal6[y] - (int)pal6[a] * (31 - level) / 31;
            if (v < 0) v = 0;
            if (v > 63) v = 63;
            pal8[y] = (u8)(v << 2);
        }
    }
    video_set_palette(pal8);
}

/* part3: ベースパレット (0..63) をフェード (a=0: 通常, a=63: 白) */
static void rot_pal_step(int a)
{
    if (a < 0) a = 0;
    if (a > 63) a = 63;
    for (int c = 0; c < 64; c++) {
        for (int k = 0; k < 3; k++) {
            int v = ((int)pal6[c * 3 + k] * (63 - a) + a * 63) / 63;
            pal8[c * 3 + k] = (u8)(v << 2);
        }
    }
    video_set_palette(pal8);
}

/* part3: 白フラッシュ→通常 (frame 0..15) */
static void rot_pal_intro(int f)
{
    int v6;
    for (int c = 0; c < 64; c++) {
        for (int k = 0; k < 3; k++) {
            v6 = (int)pal6[c * 3 + k] + (15 - f) * 5;
            if (v6 > 63) v6 = 63;
            pal8[c * 3 + k] = (u8)(v6 << 2);
        }
    }
    video_set_palette(pal8);
}

/* ---- rotazoom (part3) ---- */
static void rotazoom(u8 *dst, int x, int y, int xa, int ya)
{
    const u8 *pic = rotpic;
    int use90 = 0;

    if ((xa < 0 ? -xa : xa) > (ya < 0 ? -ya : ya)) {
        use90 = 1;
        int t = x; x = -y; y = t;
        t = xa; xa = -ya; ya = t;
        pic = rotpic90;
    }
    (void)use90;

    /* オリジナルの回転行列:
     *   列: x += ya/1024, y -= xa/1024
     *   行: x += xa*307/(256*1024), y += ya*307/(256*1024)
     * (ASM.ASM の neg dx / sbb bh,0 による符号反転) */
    int cxs = ya << 6;              /* per column, Q16 */
    int cys = -(xa << 6);
    int rxs = (xa * 307) << 6 >> 8; /* per row, Q16 (aspect) */
    int rys = (ya * 307) << 6 >> 8;

    int xq = x << 16, yq = y << 16;
    for (int oy = 0; oy < 100; oy++) {
        xq += rxs;
        yq += rys;
        int sxq = xq, syq = yq;
        u8 *d0 = dst + (oy * 2 + 0) * LW;
        u8 *d1 = dst + (oy * 2 + 1) * LW;
        for (int ox = 0; ox < 160; ox++) {
            int sx = (sxq >> 16) & 255;
            int sy = (syq >> 16) & 255;
            u8 v = pic[(sy << 8) | sx];
            d0[ox * 2 + 0] = v;
            d0[ox * 2 + 1] = v;
            d1[ox * 2 + 0] = v;
            d1[ox * 2 + 1] = v;
            sxq += cxs;
            syq += cys;
        }
    }
}

/* ---- 初期化 ---- */
static int firfade1[LH], firfade2[LH], firfade1a[LH], firfade2a[LH];

void lens_init(void)
{
    const u8 *p = g_lens_ex0;
    lenswid = (s16)((p[0] << 8) | p[1]);
    lenshig = (s16)((p[2] << 8) | p[3]);
    for (int i = 0; i < 9; i++)
        rgb_add[i] = p[4 + i];
    lensxs = lenswid / 2;
    lensys = lenshig / 2;

    tbl[1] = (const u16 *)g_lens_ex1;
    tbl[2] = (const u16 *)g_lens_ex2;
    tbl[3] = (const u16 *)g_lens_ex3;
    tbl[4] = (const u16 *)g_lens_ex4;

    {
        const u16 *pp = (const u16 *)g_lens_path;
        int n1 = (int)pp[0];
        path1 = pp + 1;
        path2 = pp + 1 + n1;
        path1_frames = n1 / 2;
    }

    for (int i = 0; i < LENS_PIXELS; i++)
        back[i] = g_lens_back[i];

    /* rotpic (main.c): a = y*10/11 - 18; 320x200 の x=32..287 */
    for (int y = 0; y < 256; y++) {
        int a = y * 10 / 11 - 18;
        if (a < 0 || a > 199)
            a = 0;
        for (int x = 0; x < 256; x++)
            rotpic[x + y * 256] = back[x + 32 + a * 320];
    }
    for (int x = 0; x < 256; x++)
        for (int y = 0; y < 256; y++)
            rotpic90[x + y * 256] = rotpic[y + (255 - x) * 256];

    build_palette();

    /* part1 のカーテン */
    for (int b = 0; b < LH; b++) {
        int a = b;
        firfade1a[b] = (19 + a / 5 + 4) & (~7);
        firfade2a[b] = (-(19 + (199 - a) / 5 + 4)) & (~7);
        firfade1[b] = 170 * 64 + (100 - b) * 50;
        firfade2[b] = 170 * 64 + (100 - b) * 50;
    }

    u8 *chunky = video_chunky();
    for (int i = 0; i < 320 * 256; i++)
        chunky[i] = 0;

    phase = -1;
    logframe = 0;
    lens_done = 0;
    phase_vbl = vblank_frames();
    video_set_palette(pal8);
    for (int i = 0; i < VIDEO_NBUF; i++) {
        lz_buf[i] = 0;
        lz_npend[i] = 0;
    }
}

static void lens_draw(u8 *chunky)
{
    u8 *dst = chunky + LENS_YOFF * LW;

    if (phase == -1) {
        /* LENS/MAIN.C:104。描画が遅い実機で窓 (-6..31) を丸ごと見逃しても
         * 止まらないよう、窓の位置 (WinUAE で計測した 39:01) を過ぎていれば
         * 進む (2026-11-15)。以下の 2 か所も同じ。 */
        if (dis_musplus() < -6 && LENS_POS() < ((39 << 8) | 1))
            return;
        phase = 0;
        prof_mark("LENS-part1");
        phase_vbl = video_frame_vbl();
    }
    if (phase == 10) {
        if (dis_musplus() < -20 &&      /* LENS/MAIN.C:380 */
            LENS_POS() < ((40 << 8) | 44))
            return;
        phase = 1;
        prof_mark("LENS-part2");
        phase_vbl = video_frame_vbl();
    }
    logframe = logical_frame();

    if (phase == 0) {
        /* part1: 2 本のラインでワイプイン
         * 70Hz で 1 フレーム 6 回なので、50Hz では 8 回/VBL に増やす。 */
        if (logframe < 80) {
            for (int c = 0; c < 8; c++) {
                for (int y = 0; y < LH; y++) {
                    int x1 = firfade1[y] >> 6;
                    int x2 = firfade2[y] >> 6;
                    int i1 = y * LW + x1;
                    int i2 = y * LW + x2;
                    if ((u32)i1 < LENS_PIXELS) {
                        dst[i1] = back[i1];
                        wr_mark_lin(i1, i1);
                    }
                    if ((u32)i2 < LENS_PIXELS) {
                        dst[i2] = back[i2];
                        wr_mark_lin(i2, i2);
                    }
                    firfade1[y] += firfade1a[y];
                    firfade2[y] += firfade2a[y];
                }
            }
        }
        if (logframe >= 300) {
            phase = 10;
            video_set_palette(pal8);
            lens_fade_step(0);
        }
        return;
    }

    if (phase == 1) {
        /* part2: レンズ移動。
         * 開始待ちを抜けたあと dis_waitb() を 1 回してから part2 に入る
         * (LENS/MAIN.C:380-382) ので、uframe は 1 フレーム遅れて始まる。 */
        int uf = logframe - 1;
        if (uf < 96) {
            int a = (uf - 32) / 2;
            if (a < 0) a = 0;
            lens_fade_step(a);
        }
        if (uf < 0) {
            /* MAIN.C:381 の dis_waitb() 中 */
        } else if (uf < 715) {
            int f = uf;
            if (f >= path1_frames)
                f = path1_frames - 1;
            int x0 = (s16)path1[f * 2 + 0];
            int y0 = (s16)path1[f * 2 + 1];
            draw_lens(dst, x0, y0);
        } else if (uf < 720 + 1 ||
                   (((int)np_ord << 8) | (np_row & 0xff)) < ((44 << 8) | 11)) {
            /* さらに、白フラッシュ (part3 開始) を MUSIC1 44:11 まで待つ。
             * 原作動画では白フラッシュ (277.38 s) とドラム連打の始まりが
             * ほぼ同時 (+0.08 s)。原作の part1 開始 (MAIN.C:104 の
             * musplus>=-6) は LENS.EXE の読み込み後に来て窓の中で即通過
             * するので、開始時刻は読み込み時間で決まっていた。移植版は
             * 読み込みが無いぶん早く、連打の約 0.65 s 前に白フラッシュが
             * 出ていた。ユーザー指示 (2026-11-15) で連打の頭に合わせる。
             * 44:11 は -Start 9 のキャプチャで、連打の立ち上がり (振幅が
             * 1200 前後から 3000 超へ) が白フラッシュと同じフレームに
             * なる値 (44:20 では 0.51 s 早く、44:11 で +0.01 s)。 */
            /* MAIN.C:179-182: レンズを止めたまま uframe<720 まで待つ。
             * さらに part3 は waitb() を 1 回してから白フラッシュの
             * パレットを入れる (MAIN.C:199-200)。以前はこの 5+1 フレームを
             * 待たずに 715 で切り替えていた。 */
        } else {
            phase = 2;
            prof_mark("LENS-rotazoom");
            /* inittwk: 画面クリア + パレットを黒に */
            for (int i = 0; i < LENS_PIXELS; i++)
                dst[i] = 0;
            wr_mark(0, 0, LW - 1);
            wr_mark(LH - 1, 0, LW - 1);
            for (int i = 0; i < 768; i++)
                pal8[i] = 0;
            video_set_palette(pal8);
            rot_pal_intro(0);
            phase_vbl = video_frame_vbl();
        }
        return;
    }

    /* phase 2: rotazoomer */
    {
        int f = logframe;
        if (dis_musplus() > -4 || f >= 2000 ||  /* LENS/MAIN.C:211-213 */
            LENS_POS() >= ((52 << 8) | 0)) {
            /* LENS/MAIN.C:274-275: 抜けたら全色 63 (白) */
            static u8 white[768];
            for (int i = 0; i < 768; i++)
                white[i] = 255;
            video_set_palette(white);
            if (!lens_done)
                prof_mark("LENS-end");
            lens_done = 1;
            return;
        }
        if (f < 0) f = 0;
        if (f > 1999) f = 1999;
        int x = (s16)path2[f * 4 + 0];
        int y = (s16)path2[f * 4 + 1];
        int xa = (s16)path2[f * 4 + 2];
        int ya = (s16)path2[f * 4 + 3];
        rotazoom(dst, x, y, xa, ya);
        wr_mark(0, 0, LW - 1);
        wr_mark(LH - 1, 0, LW - 1);
        if (f < 16) {
            rot_pal_intro(f);
        } else if (f > 2000 - 128) {
            int a = (f - (2000 - 128)) / 2;
            if (a > 63) a = 63;
            rot_pal_step(a);
        } else if (f == 16) {
            rot_pal_step(0);
        }
    }
    return;
}

int lens_render(u8 *chunky, u8 *planes, int width, int height)
{
    (void)width;
    (void)height;
    wr_y0 = LH; wr_y1 = 0; wr_x0 = LW; wr_x1 = 0;
    lens_draw(chunky);
    lz_c2p(chunky, planes);
    return 1;
}
