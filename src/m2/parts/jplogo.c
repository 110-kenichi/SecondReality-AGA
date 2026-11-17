/*
 * Second Reality A1200: JPLOGO (Ice Kingdom)
 *
 * Original: reference/SecondReality/JPLOGO/JP.C + ASM.ASM
 *   - 320x400 picture (ICEKNGDM.UP), visible columns x = 70..253 (184 px)
 *   - y1/y2 bounce tables (200 frames -> 800 subframes)
 *   - per scanline: b = (y-y1)*400/(y2-y1)
 *       xsc = (400-(y2-y1))/8
 *       a  = 184 + (sin1024[b*32/25]*xsc + 32)/64   (rounded down to even)
 *     the 184-px row is scaled to a px and centered at x=160
 *   - the original is 70 Hz; PAL runs at 50 Hz, so frame = vbl_delta*7/5
 *
 * The 400-line virtual screen is shown in the usual 320x200 letterbox
 * (y = 28..227), i.e. two source lines per output line, which keeps the
 * original's vertical pixel aspect.
 */
#include "scenes.h"
#include "../framework/video.h"
#include "../framework/vblank.h"
#include "../framework/c2p.h"

extern const u8 *g_jplogo_pic;   /* 320*400 palette indices */
extern const u8 *g_jplogo_pal;   /* 768 bytes RGB (8-bit) */
extern const u8 *g_jplogo_sin;   /* 1024 x big-endian s16, amplitude 255 */

#define SRC_W 320
#define SRC_H 400
#define ROW_X 70
#define ROW_W 184

#define OUT_ROWS 200
#define YOFF 28
#define PART_VBLS 500          /* 700 frames at 70 Hz = 10 s = 500 VBL at 50 Hz */

static s16 framey1t[800];
static s16 framey2t[800];
static u32 zoom_start_vbl;

/* 原作 JP.C:203-221 の開始スライド (scrolly)。
 * y=400*64 から a=64, a+=6 で減らし、start=y/64 を表示開始行にする。 */
static int slide_y, slide_a, slide_done, slide_acc;
static u32 jp_last_vbl;

static int jp_sin(int i) {
    i &= 1023;
    return (s16)((g_jplogo_sin[i * 2] << 8) | g_jplogo_sin[i * 2 + 1]);
}

static void build_tables(void) {
    static s16 fy1[201], fy2[201];
    int y1 = 0, y1a = 500;
    int y2 = 399 * 16, y2a = 500;
    int mika = 1, halt = 0;
    int a = 0, la = 0;

    for (int frame = 0; frame < 200; frame++) {
        if (!halt) {
            y1 += y1a;
            y2 += y2a;
            y2a += 16;
            if (y2 > 400 * 16) {
                y2 -= y2a;
                y2a = -y2a * mika / 8;
                if (mika < 4)
                    mika += 3;
            }
            y1a += 16;
            la = a;
            a = (y2 - y1) - 400 * 16;
            if (((a & 0x8000) ^ (la & 0x8000)) != 0)
                y1a = y1a * 7 / 8;
            y1a += a / 8;
            y2a -= a / 8;
        }
        if (frame > 90) {
            if (y2 >= 399 * 16) {
                y2 = 400 * 16;
                halt = 1;
            } else {
                y2a = 8;
            }
            y1 = y2 - 400 * 16;
        }
        fy1[frame] = (s16)y1;
        fy2[frame] = (s16)y2;
    }
    fy1[200] = fy1[199];
    fy2[200] = fy2[199];

    for (int i = 0; i < 800; i++) {
        int b = i / 4, c = i & 3, d = 3 - c;
        framey1t[i] = (s16)((fy1[b] * d + fy1[b + 1] * c) / 3);
        framey2t[i] = (s16)((fy2[b] * d + fy2[b + 1] * c) / 3);
    }
}

static u8 jp_pal[768];

/* 画面転送 (c2p) を絵のある列だけにする (2026-11-15 ユーザー指摘)。
 * 拡大幅 a は落ち着くと 184 (xsc = 0) で、x = 68..251 = 32 ドット列
 * 2..7 だけに絵がある (揺れている間も 1..8)。それ以外の列は chunky が
 * 常に 0 なので、裏画面 2 枚それぞれについて「前回変換した列」と
 * 「今回絵のある列」を合わせた範囲だけを変換すれば、全体を変換したのと
 * 同じプレーンになる。初めて使う裏画面は 256 行全体を変換する
 * (前のパートの絵や上下の帯を消すため。c2p_scene の最初の 2 回と同じ)。 */
static u8 *jp_buf[VIDEO_NBUF];
static int jp_c0[VIDEO_NBUF], jp_c1[VIDEO_NBUF];

static void jp_c2p(u8 *chunky, u8 *planes, int x_min, int x_end) {
    /* 画面バッファの番号 (トリプルバッファ、2026-11-17) */
    int slot = video_back_index();
    if (jp_buf[slot] != planes)
        slot = -slot - 1;               /* 初めて (または別の画面): 全体を変換 */
    int b0 = x_min >> 5, b1 = (x_end + 31) >> 5;
    if (x_end <= x_min)
        b0 = b1 = 0;
    if (slot < 0) {
        slot = -slot - 1;
        jp_buf[slot] = planes;
        c2p8_fast(chunky, planes, SCREEN_W, SCREEN_H);
        jp_c0[slot] = b0;
        jp_c1[slot] = b1;
        return;
    }
    int u0 = b0, u1 = b1;
    if (jp_c1[slot] > jp_c0[slot]) {
        if (u1 <= u0) {
            u0 = jp_c0[slot];
            u1 = jp_c1[slot];
        } else {
            if (jp_c0[slot] < u0) u0 = jp_c0[slot];
            if (jp_c1[slot] > u1) u1 = jp_c1[slot];
        }
    }
    c2p8_cols(chunky + YOFF * SCREEN_W, planes, YOFF, OUT_ROWS, u0, u1);
    jp_c0[slot] = b0;
    jp_c1[slot] = b1;
}

/* JP.C:199: row[a][b]==0 -> 64 */
static inline u8 jp_px(u8 v) { return v ? v : 64; }

void jplogo_init(void) {
    build_tables();
    slide_y = 400 * 64;
    slide_a = 64;
    slide_done = 0;
    slide_acc = 0;
    zoom_start_vbl = 0;
    jp_last_vbl = 0;
    for (int i = 0; i < VIDEO_NBUF; i++)
        jp_buf[i] = 0;
    /* JP.C:186-200: palette[64] を黒にし、絵の中の色 0 を 64 に置き換える。
     * 色 0 は U2E のホワイトアウト (U2E.C:285-295、色 1..253 だけ白へ) でも
     * 黒のまま残るので、置き換えないと白の中に黒い点が残る
     * (2026-11-15 ユーザー指摘)。 */
    for (int i = 0; i < 768; i++)
        jp_pal[i] = g_jplogo_pal[i];
    /* jp_px() (色 0 -> 64) を絵そのものに一度だけ当てておき、描画ループの
     * 1 ドットごとの分岐を無くす。64 -> 64 なので何度やっても同じ。
     * 絵 (main.c が読むファスト RAM の 320x400) は JPLOGO しか使わない。 */
    {
        u8 *pic = (u8 *)g_jplogo_pic;
        for (int i = 0; i < SRC_W * SRC_H; i++)
            pic[i] = jp_px(pic[i]);
    }
    jp_pal[64 * 3 + 0] = jp_pal[64 * 3 + 1] = jp_pal[64 * 3 + 2] = 0;
    video_set_palette(jp_pal);
}

int jplogo_finished(void) {
    return slide_done && zoom_start_vbl != 0 &&
           (vblank_frames() - zoom_start_vbl) >= PART_VBLS;
}

int jplogo_render(u8 *chunky, u8 *planes, int width, int height) {
    (void)width;
    (void)height;

    u8 *win = chunky + YOFF * SCREEN_W;

    u32 now = video_frame_vbl();
    u32 el = (jp_last_vbl == 0) ? 1 : now - jp_last_vbl;
    jp_last_vbl = now;
    if (el > 6)
        el = 6;
    slide_acc += (int)el * 7;
    while (slide_acc >= 5 && !slide_done) {
        slide_acc -= 5;
        slide_y -= slide_a;
        slide_a += 6;
        if (slide_y <= 0) {
            slide_y = 0;
            slide_done = 1;
            /* スライド終了時点の VBL。以降のズームは
             * vblank_frames() の差分から 70Hz フレームを出す。 */
            zoom_start_vbl = now;
        }
    }

    if (!slide_done) {
        /* JP.C:207-219: スライド中は生の 184px 画像を表示開始行
         * start=slide_y/64 で見せる (400 行 -> 200 行は 2:1)。 */
        int start = slide_y / 64;
        for (int i = 0; i < 256 * SCREEN_W; i++)
            chunky[i] = 0;
        for (int oy = 0; oy < OUT_ROWS; oy++) {
            int v = start + oy * 2;
            if (v < 0 || v >= SRC_H)
                continue;
            const u8 *src = g_jplogo_pic + v * SRC_W + ROW_X;
            u8 *dst = win + oy * SCREEN_W + (SCREEN_W - ROW_W) / 2;
            for (int i = 0; i < ROW_W; i++)
                dst[i] = src[i];       /* 色 0 -> 64 は init で済ませた */
        }
        /* 絵は x = (320-184)/2 .. +184 だけ */
        jp_c2p(chunky, planes, (SCREEN_W - ROW_W) / 2, (SCREEN_W - ROW_W) / 2 + ROW_W);
        return 1;
    }

    /* 原本 JP.C:72-104 の doit() は 70Hz の waitb() の戻り値を
     * frame に足す。PAL 50Hz では経過 VBL × 70/50 = 7/5 が
     * 70Hz フレーム数。描画回数 (vbl++) を基準にすると、
     * 描画が 50fps を下回ったときに遅くなる。 */
    u32 tv = now - zoom_start_vbl;
    int frame = (int)((tv * 7u) / 5u);
    int f = frame > 511 ? 511 : frame;
    int y1 = framey1t[f] / 16;
    int y2 = framey2t[f] / 16;
    int den = y2 - y1;
    if (den < 1)
        den = 1;
    int xsc = (400 - den) / 8;

    /* 200 行の各ドットを 1 回だけ書く (以前は 64000 バイトを先に消して
     * から絵を上書きしていた)。描かない行と、拡大した行の左右は 0。 */
    int x_min = SCREEN_W, x_end = 0;
    for (int oy = 0; oy < OUT_ROWS; oy++) {
        int vy = oy * 2;
        u8 *line = win + oy * SCREEN_W;
        if (vy < y1 || vy >= y2) {
            __builtin_memset(line, 0, SCREEN_W);
            continue;
        }
        int b = (vy - y1) * 400 / den;
        if (b < 0)
            b = 0;
        if (b > SRC_H - 1)
            b = SRC_H - 1;

        int a = 184 + (jp_sin(b * 32 / 25) * xsc + 32) / 64;
        a &= ~1;
        if (a < 2)
            a = 2;
        if (a > 318)
            a = 318;

        /* a は 2..318 の偶数なので x0 = 160-a/2 は 1..159、右端
         * x0+a-1 は 160..318。画面からはみ出さないので範囲判定は不要。 */
        const u8 *src = g_jplogo_pic + b * SRC_W + ROW_X;
        int x0 = 160 - a / 2;
        if (x0 < x_min) x_min = x0;
        if (x0 + a > x_end) x_end = x0 + a;
        __builtin_memset(line, 0, x0);
        __builtin_memset(line + x0 + a, 0, SCREEN_W - x0 - a);
        /* src の添字 i*ROW_W/a を 16.16 固定小数点で求める。
         * step = ROW_W*65536/a の切り上げで、a = 2..318 の全偶数、
         * i = 0..a-1 の全組で (i*step)>>16 == i*ROW_W/a を確認済み
         * (2026-11-15、Python で総当たり)。画素は割り算版と同一。 */
        u32 step = ((u32)ROW_W * 65536u + (u32)a - 1u) / (u32)a;
        u32 pos = 0;
        u8 *d = line + x0;
        for (int i = 0; i < a; i++) {
            d[i] = src[pos >> 16];
            pos += step;
        }
    }

    jp_c2p(chunky, planes, x_min, x_end);
    return 1;
}
