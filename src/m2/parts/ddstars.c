/*
 * M4 シーン: DDSTARS (隠しパート) — オリジナル DDSTARS/STARS.ASM の移植
 *
 * オリジナルは 1024 個の 3D 星を 2 プレーン (4 色) に OR 蓄積し、
 * 途中から TEXTS のテキスト画像を横方向に開く。
 *
 * Amiga 版:
 *  - 320x256 のカスタム画面 (チップ RAM)。プレーン 0/1 を星 (色 1-3)、
 *    プレーン 2 をテキスト (色 4 = 白) に使う。
 *  - 星は z を 2 ずつ減らし、透視投影して 1 ピクセルで描く。
 *    前回位置も暗い色で描いて簡単な軌跡にする。
 *  - テキストは TEXTS.LBM を 1bit マスク化し、12 秒後から
 *    1 フレーム 4 列ずつ横に開く (プレーン 2 に一度描けば保持される)。
 */
#include "scenes.h"
#include "../framework/mem.h"
#include "../framework/video.h"
#include "../framework/vblank.h"

#include <proto/exec.h>
#include <exec/memory.h>

#define D_W         320
#define D_H         256
#define D_RB        40
#define D_PS        (D_RB * D_H)
#define D_YOFF      28

#define NSTARS      1024
#define STAR_START  512             /* 最初は 512 個、6 秒後に 1024 個 */
#define STAR_FULL   300             /* 6 秒後に 1024 個へ */
#define TEXT_START  600             /* 12 秒 (50fps) からテキストを開く */
#define TEXT_STEP   4               /* 1 フレームに開く列数 */

#define TEXT_W      320
#define TEXT_H      200
#define TEXT_PS     (40 * TEXT_H)   /* 1 プレーン 8,000 バイト */
#define TEXT_DATA   0x310

extern const u8 *g_ddstars_text;

typedef struct {
    s16 x, y;                       /* -512..511 */
    u8 z;                           /* 0..255 */
    s16 px, py;                     /* 前回の画面位置 (軌跡用) */
} Star;

static u8 *dd_planes;
static Star stars[NSTARS];
static u8 text_mask[40 * TEXT_H];
static int text_mask_ready;
static u32 seed = 0x12345678u;
static int frame, reveal, active;
static u32 dd_last_vbl;
static int dd_step_acc;
static u16 recip[256];              /* 1/(150+4z) の Q16 逆数 (除算回避) */

static inline void wput(int x, int y, u8 c) {
    u8 *p = dd_planes + y * D_RB + (x >> 3);
    u8 bit = (u8)(0x80u >> (x & 7));
    u8 nb = (u8)~bit;
    u8 v;
    /* 星はプレーン 0/1 のみ。プレーン 2 以降 (テキスト) には触らない。 */
    v = (u8)(p[0 * D_PS] & nb); if (c & 0x01) v |= bit; p[0 * D_PS] = v;
    v = (u8)(p[1 * D_PS] & nb); if (c & 0x02) v |= bit; p[1 * D_PS] = v;
}

/* プレーン p に 1 ピクセル (テキスト用、OR のみ) */
static inline void wput_bit(int p, int x, int y) {
    u8 *q = dd_planes + p * D_PS + y * D_RB + (x >> 3);
    *q |= (u8)(0x80u >> (x & 7));
}

static u16 rnd16(void) {
    seed = seed * 0x343fdu + 0x269ec3u;
    return (u16)(seed >> 16);
}

static void respawn(Star *s) {
    s->x = (s16)((rnd16() & 1023) - 512);
    s->y = (s16)((rnd16() & 1023) - 512);
    s->z = 255;
    s->px = -1;
}

static void build_text_mask(void) {
    const u8 *t = g_ddstars_text + TEXT_DATA;
    for (int i = 0; i < TEXT_PS; i++)
        text_mask[i] = 0;
    for (int p = 0; p < 8; p++) {
        const u8 *pl = t + p * TEXT_PS;
        for (int i = 0; i < TEXT_PS; i++)
            text_mask[i] |= pl[i];
    }
    text_mask_ready = 1;
}

void ddstars_init(void) {
    frame = 0;
    reveal = 0;
    active = STAR_START;
    seed = 0x12345678u;

    if (!g_ddstars_text)
        return;

    if (!dd_planes) {
        dd_planes = (u8 *)mem_alloc(8 * D_PS, MEMF_CHIP | MEMF_CLEAR);
        if (!dd_planes)
            return;
    }
    for (int i = 0; i < 8 * D_PS; i++)
        dd_planes[i] = 0;

    if (!text_mask_ready)
        build_text_mask();

    /* 透視投影の逆数テーブル (Q16)。星ごとの除算を避ける。 */
    for (int z = 0; z < 256; z++)
        recip[z] = (u16)(65536u / (u32)(150 + 4 * z));

    for (int i = 0; i < NSTARS; i++) {
        stars[i].x = (s16)((rnd16() & 1023) - 512);
        stars[i].y = (s16)((rnd16() & 1023) - 512);
        stars[i].z = (u8)(i & 255);
        stars[i].px = -1;
    }

    /* 元 STARS.ASM の星パレット (6bit -> 8bit)。色 4 はテキスト用の白。 */
    static u8 pal[256 * 3];
    for (int i = 0; i < 768; i++)
        pal[i] = 0;
    pal[1 * 3 + 0] = 100; pal[1 * 3 + 1] = 124; pal[1 * 3 + 2] = 152;
    pal[2 * 3 + 0] = 180; pal[2 * 3 + 1] = 232; pal[2 * 3 + 2] = 255;
    pal[3 * 3 + 0] = 255; pal[3 * 3 + 1] = 255; pal[3 * 3 + 2] = 255;
    /* テキスト (プレーン 2) は星の上でも白く見えるよう 4-7 を白にする */
    for (int i = 4; i < 8; i++) {
        pal[i * 3 + 0] = 255;
        pal[i * 3 + 1] = 255;
        pal[i * 3 + 2] = 255;
    }

    video_set_palette(pal);
    video_set_custom_screen(dd_planes, D_RB);
}

/* STARS.ASM does "call waitb" once per iteration, i.e. one simulation step
 * per 70 Hz frame.  PAL runs at 50 Hz, so execute 1.4 steps per VBL (7/5). */
/* the hidden part holds the finished text for a while, then the host
 * continues; 1500 frames ≈ 30 s at PAL 50 Hz */
int ddstars_finished(void) {
    return frame >= 1500;
}

static void ddstars_step(void)
{
    if (frame < STAR_FULL)
        active = STAR_START;
    else
        active = NSTARS;

    for (int i = 0; i < active; i++) {
        Star *s = &stars[i];
        if (s->z < 2) {
            respawn(s);
            continue;
        }
        s->z -= 2;
        u32 r = recip[s->z];
        int sx = 160 + (int)(((s32)s->x * 288 * (s32)r) >> 16);
        int sy = 128 + (int)(((s32)s->y * 216 * (s32)r) >> 16);
        if ((unsigned)sx >= D_W || (unsigned)sy >= D_H) {
            s->px = -1;
            continue;
        }
        int c = s->z >= 180 ? 1 : (s->z >= 110 ? 2 : 3);
        if (s->px >= 0 && (unsigned)s->px < D_W && (unsigned)s->py < D_H)
            wput(s->px, s->py, 1);          /* 軌跡 (暗い色) */
        wput(sx, sy, c);
        s->px = (s16)sx;
        s->py = (s16)sy;
    }

    /* テキストを横に開く (プレーン 2 に描いたら保持) */
    if (frame >= TEXT_START && reveal < TEXT_W) {
        int next = reveal + TEXT_STEP;
        if (next > TEXT_W)
            next = TEXT_W;
        for (int x = reveal; x < next; x++) {
            int bx = x >> 3;
            u8 bit = (u8)(0x80u >> (x & 7));
            for (int y = 0; y < TEXT_H; y++) {
                if (text_mask[y * 40 + bx] & bit)
                    wput_bit(2, x, y + D_YOFF);
            }
        }
        reveal = next;
    }

    frame++;
}

int ddstars_render(u8 *chunky, u8 *planes, int width, int height) {
    (void)chunky;
    (void)planes;
    (void)width;
    (void)height;

    if (!dd_planes || !g_ddstars_text)
        return 1;

    /* 星のプレーン 0/1 だけクリア (テキストのプレーン 2 は保持) */
    for (int p = 0; p < 2; p++) {
        u8 *q = dd_planes + p * D_PS;
        for (int i = 0; i < D_PS; i++)
            q[i] = 0;
    }

    u32 now = video_frame_vbl();
    u32 el = now - dd_last_vbl;
    if (el > 6)
        el = 6;
    dd_last_vbl = now;
    dd_step_acc += (int)el * 7;
    int steps = dd_step_acc / 5;
    dd_step_acc -= steps * 5;
    if (steps > 6)
        steps = 6;
    for (int st = 0; st < steps; st++)
        ddstars_step();

    return 1;
}
