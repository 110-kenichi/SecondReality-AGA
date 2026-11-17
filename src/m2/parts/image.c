/*
 * M3 シーン 3: タイトル画像 (オリジナルの SRTITLE.LBM を IMG1 へ変換したもの)
 *
 * - main.c が TakeSystem 前に DF1:title.raw をロードし g_image_data に格納
 * - init で両画面へプレーンをコピー (静的表示)、render は何もしない
 * - パレットは画像のもの (色 0 = 黒 / 色 1 = 白 はデバッグ表示用に固定)
 */
#include "scenes.h"
#include "../framework/video.h"
#include "../framework/vblank.h"

#define IMG_PAL_OFF 0x10
#define IMG_DATA_OFF 0x310
#define PLANE_ROWBYTES 40
#define PLANE_SIZE (PLANE_ROWBYTES * 256)

extern const u8 *g_image_data;

/* 原作 BEG/BEG.C:20-86:
 *   memset(vram,15,...) + パレット 0..254 を白にして 32 フレーム待つ
 *   → タイトル画像を描く
 *   → 128 フレームかけて白から画像パレットへフェードイン
 * 70Hz の 32/128 フレームを PAL 50Hz の VBL に換算する。
 */
static u8 img_pal[768];
static u8 img_work[768];
static u32 img_last_vbl;
static int img_acc, img_frame, img_done;

void image_init(void) {
    const u8 *img = g_image_data;
    if (!img)
        return;

    u16 w = (u16)((img[4] << 8) | img[5]);
    u16 h = (u16)((img[6] << 8) | img[7]);
    u8 planes = img[8];
    if (w != 320 || h != 256)
        return;

    /* タイトル画像のパレットを保存 */
    for (int i = 0; i < 256 * 3; i++)
        img_pal[i] = img[IMG_PAL_OFF + i];
    /* 原作 BEG.C:25-34: 0..254 を白、255 を黒にする */
    for (int i = 0; i < 255 * 3; i++)
        img_work[i] = 255;
    img_work[255 * 3 + 0] = 0;
    img_work[255 * 3 + 1] = 0;
    img_work[255 * 3 + 2] = 0;
    video_set_palette(img_work);

    img_frame = 0;
    img_acc = 0;
    img_done = 0;
    img_last_vbl = vblank_frames();

    /* 両画面のプレーンへコピー */
    const u8 *src = img + IMG_DATA_OFF;
    /* すべての画面バッファへ (トリプルバッファ、2026-11-17) */
    for (int k = 0; k < VIDEO_NBUF; k++) {
        u8 *dst = video_planes(k);
        for (int p = 0; p < 8; p++) {
            const u8 *sp = src + p * PLANE_SIZE;
            u8 *dp = dst + p * PLANE_SIZE;
            for (int i = 0; i < PLANE_SIZE; i++)
                dp[i] = sp[i];
        }
    }
    /* 直前フレームの控え (GLENZ の zoomer2) 用に chunky も作っておく。
     * このパートはプレーン直描きなので、chunky を埋めないと切替時に
     * 前のパートの残りが控えられてしまう。 */
    {
        u8 *ch = video_chunky();
        for (int y = 0; y < 256; y++) {
            for (int x = 0; x < 320; x++) {
                int b = x >> 3;
                int bit = 7 - (x & 7);
                u8 v = 0;
                for (int p = 0; p < 8; p++)
                    if ((src[p * PLANE_SIZE + y * PLANE_ROWBYTES + b] >> bit) & 1)
                        v |= (u8)(1 << p);
                ch[y * 320 + x] = v;
            }
        }
    }
}

/* 原作 BEG.C:24-86 は白 32 フレーム + 129 フレームのフェードで終わる
 * (固定長)。終わると U2.ASM:817-821 で MUSIC1 を order 0 から鳴らす。 */
int image_finished(void) {
    return img_done;
}

int image_render(u8 *chunky, u8 *planes, int width, int height) {
    (void)chunky;
    (void)planes;
    (void)width;
    (void)height;

    if (img_done)
        return 1;

    /* 70Hz フレーム数を VBL 経過から数える (1.4 フレーム/VBL) */
    u32 now = video_frame_vbl();
    u32 el = now - img_last_vbl;
    if (el > 10)
        el = 10;
    img_last_vbl = now;
    img_acc += (int)el * 7;
    while (img_acc >= 5) {
        img_acc -= 5;
        img_frame++;
    }

    if (img_frame < 32) {
        /* 白フラッシュ保持 (BEG.C:24-34: 32 フレーム) */
        return 1;
    }
    if (img_frame < 32 + 129) {
        /* 白 → タイトルパレット (BEG.C:80-85: 128 フレーム) */
        int c = img_frame - 32;
        if (c > 128)
            c = 128;
        for (int i = 0; i < 255 * 3; i++)
            img_work[i] = (u8)(((128 - c) * 255 + img_pal[i] * c) / 128);
        img_work[255 * 3 + 0] = 0;
        img_work[255 * 3 + 1] = 0;
        img_work[255 * 3 + 2] = 0;
        video_set_palette(img_work);
        return 1;
    }
    /* 最終パレット */
    for (int i = 0; i < 256 * 3; i++)
        img_work[i] = img_pal[i];
    img_work[255 * 3 + 0] = 0;
    img_work[255 * 3 + 1] = 0;
    img_work[255 * 3 + 2] = 0;
    video_set_palette(img_work);
    img_done = 1;
    return 1;
}
