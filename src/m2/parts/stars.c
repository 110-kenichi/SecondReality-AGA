/*
 * M3 シーン 1: 3D スターフィールド (planar 直接描画 + 音楽同期)
 *
 * - 256 個の星を透視投影で手前に飛ばす (Second Reality の定番演出)
 * - 前フレームの星の位置だけを消す (全画面クリア不要・c2p 不要)
 * - 音楽の行 (np_row) に合わせてパレットを回転 → 色が音楽と同期して変化
 */
#include "scenes.h"
#include "../framework/video.h"
#include "../music/music.h"

#define NSTARS 256
#define PLANE_ROWBYTES 40
#define PLANE_SIZE (PLANE_ROWBYTES * 256)

typedef struct {
    int x, y, z;
    u8 color;
    int px, py;                 /* 前フレームの画面位置 */
    int px2, py2;               /* 2 フレーム前の画面位置 (現在のバックバッファ上の星) */
} Star;

static Star stars[NSTARS];
static u32 rnd_state = 0x12345678;
static u8 palette[256 * 3];
static u16 last_row = 0xFFFF;

static u32 rnd(void) {
    rnd_state = rnd_state * 1103515245 + 12345;
    return rnd_state >> 16;
}

static void respawn(Star *s) {
    s->x = (int)(rnd() % 4096) - 2048;
    s->y = (int)(rnd() % 4096) - 2048;
    s->z = 2048;
}

/* 1 ピクセルを 8 プレーンへ直接描画 (256 色) */
static inline void plot(u8 *planes, int x, int y, u8 color) {
    if ((unsigned)x >= 320u || (unsigned)y >= 256u)
        return;
    u8 *p = planes + y * PLANE_ROWBYTES + (x >> 3);
    u8 bit = (u8)(0x80u >> (x & 7));
    u8 nbit = (u8)~bit;
    if (color & 0x01) p[0 * PLANE_SIZE] |= bit; else p[0 * PLANE_SIZE] &= nbit;
    if (color & 0x02) p[1 * PLANE_SIZE] |= bit; else p[1 * PLANE_SIZE] &= nbit;
    if (color & 0x04) p[2 * PLANE_SIZE] |= bit; else p[2 * PLANE_SIZE] &= nbit;
    if (color & 0x08) p[3 * PLANE_SIZE] |= bit; else p[3 * PLANE_SIZE] &= nbit;
    if (color & 0x10) p[4 * PLANE_SIZE] |= bit; else p[4 * PLANE_SIZE] &= nbit;
    if (color & 0x20) p[5 * PLANE_SIZE] |= bit; else p[5 * PLANE_SIZE] &= nbit;
    if (color & 0x40) p[6 * PLANE_SIZE] |= bit; else p[6 * PLANE_SIZE] &= nbit;
    if (color & 0x80) p[7 * PLANE_SIZE] |= bit; else p[7 * PLANE_SIZE] &= nbit;
}

/* パレット生成: 色 0 = 黒 / 色 1 = 白 (デバッグ表示用) / 他はレインボー */
static void make_palette(int shift) {
    palette[0] = 0;
    palette[1] = 0;
    palette[2] = 0;
    palette[3] = 255;
    palette[4] = 255;
    palette[5] = 255;
    for (int i = 2; i < 256; i++) {
        int v = (i + shift) & 0xFF;
        palette[i * 3 + 0] = (u8)((v * 2) & 0xFF);
        palette[i * 3 + 1] = (u8)((v * 3) & 0xFF);
        palette[i * 3 + 2] = (u8)((v * 5) & 0xFF);
    }
}

void stars_init(void) {
    for (int i = 0; i < NSTARS; i++) {
        respawn(&stars[i]);
        stars[i].z = (int)(rnd() % 2048) + 16;
        stars[i].px = -1;
        stars[i].py = -1;
        stars[i].px2 = -1;
        stars[i].py2 = -1;
    }
    make_palette(0);
    video_set_palette(palette);
    last_row = 0xFFFF;
}

int stars_render(u8 *chunky, u8 *planes, int width, int height) {
    (void)chunky;
    (void)width;
    (void)height;

    /* 音楽同期: 行が変わるたびにパレットを回転 */
    if (np_row != last_row) {
        last_row = np_row;
        make_palette(np_row * 3);
        video_set_palette(palette);
    }

    /* 2 フレーム前の星 (現在のバックバッファに描かれている) を消す */
    for (int i = 0; i < NSTARS; i++) {
        Star *s = &stars[i];
        if (s->px2 >= 0)
            plot(planes, s->px2, s->py2, 0);
    }

    /* 更新 + 描画 */
    for (int i = 0; i < NSTARS; i++) {
        Star *s = &stars[i];
        s->px2 = s->px;
        s->py2 = s->py;
        s->z -= 16;
        if (s->z < 16)
            respawn(s);

        int z = s->z;
        int sx = 160 + s->x * 256 / z;
        int sy = 128 + s->y * 128 / z;

        if ((unsigned)sx < 320u && (unsigned)sy < 256u) {
            int c = 255 - (z >> 3);
            if (c < 32)
                c = 32;
            s->color = (u8)c;
            plot(planes, sx, sy, s->color);
            s->px = sx;
            s->py = sy;
        } else {
            s->px = -1;
        }
    }
    return 1;                   /* planar 直接描画 (c2p 不要) */
}
