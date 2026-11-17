/*
 * M3 シーン 4: END (エンドピクチャ)
 *
 * オリジナル: reference/SecondReality/END/END.C + READP.C
 *   白フラッシュ (パレット全白) → 画像表示
 *   → 白から画像パレットへクロスフェード (128 段階)
 *   → dis_musplus() > -16 (音楽マーカー接近) を待つ
 *   → 黒へフェードアウト (64 段階)
 *
 * - main.c が TakeSystem 前に DF1:endpic.raw をロードし g_endpic_data に格納
 * - 画像は init で両画面のプレーンへコピー (静的)。render はパレット制御のみ
 * - パレットの色 1 はデバッグ表示用に白へ固定
 */
#include "scenes.h"
#include <string.h>
#include "../framework/video.h"
#include "../framework/dis.h"

#define IMG_PAL_OFF 0x10
#define IMG_DATA_OFF 0x310
#define PLANE_ROWBYTES 40
#define PLANE_SIZE (PLANE_ROWBYTES * 256)

/* 原本 END.C は 70Hz (VGA vsync) のフレーム数。50Hz へ換算する:
 *   白保持 32 × 50/70 = 22.9 → 23 VBL   (END.C:92)
 *   フェードイン 129 × 50/70 = 92.1 → 92 VBL (END.C:101, c=0..128)
 *   フェードアウト 64 × 50/70 = 45.7 → 46 VBL (END.C:112, c=63..0) */
#define WHITE_HOLD_FRAMES 23
#define FADE_IN_FRAMES    91          /* +1 で 92 段階 */
#define FADE_OUT_FRAMES   46
#ifdef TEST_FIXED_SCENE
#define MUSWAIT_MAX       150    /* テスト時は短縮 */
#else
#define MUSWAIT_MAX       5000   /* オリジナル END.C と同じ上限 */
#endif

extern const u8 *g_endpic_data;

static u8 img_pal[256 * 3];
static u8 work_pal[256 * 3];
static const u8 *end_logo_data;
static u8 end_logo_written[VIDEO_NBUF];   /* 画面バッファごとに絵を写したか */
static int state;                /* 0: 白 / 1: フェードイン / 2: 待ち / 3: フェードアウト / 4: 黒 */
static int frame;
static int end_done;             /* フェードアウト完了 (原作 END.C の終了) */

/* 原作 END.C:107-117 は「音楽マーカー (dis_musplus() > -16) を待つ →
 * 64 段階で黒へフェードアウト」で終わる。固定の 400 フレームで打ち切ると
 * 指輪の画像が原作より大幅に短くなるため、フェードアウト完了で終了する。 */
int end_finished(void) { return end_done; }

void end_init(void) {
    const u8 *img = g_endpic_data;
    state = 0;
    frame = 0;
    end_done = 0;
    end_logo_data = 0;
    for (int i = 0; i < VIDEO_NBUF; i++)
        end_logo_written[i] = 0;
    if (!img)
        return;

    u16 w = (u16)((img[4] << 8) | img[5]);
    u16 h = (u16)((img[6] << 8) | img[7]);
    if (w != 320 || h != 256)
        return;

    /* 基準パレット (色 1 はデバッグ表示用に白へ固定) */
    for (int i = 0; i < 256 * 3; i++)
        img_pal[i] = img[IMG_PAL_OFF + i];
    img_pal[3] = 255;
    img_pal[4] = 255;
    img_pal[5] = 255;

    /* 初期化中は表示中の前面へ書かず、render から裏面へ順に用意する。 */
    end_logo_data = img + IMG_DATA_OFF;

    /* 白フラッシュ (全色白) */
    for (int i = 0; i < 256 * 3; i++)
        work_pal[i] = 255;
    video_set_palette(work_pal);
}

int end_render(u8 *chunky, u8 *planes, int width, int height) {
    (void)chunky;
    (void)width;
    (void)height;

    /* 画面バッファすべてに 1 回ずつ絵を写す。以前は 2 枚だけで、トリプル
     * バッファでは 3 枚目に U2E の最後の絵 (FUTURE CREW の文字) が残り、
     * 交互に見えていた (2026-11-17 ユーザー報告)。 */
    {
        int bi = video_back_index();
        if (end_logo_data && !end_logo_written[bi]) {
            u8 *dst = planes;
            const u8 *src = end_logo_data;
            for (int p = 0; p < 8; p++)
                memcpy(dst + p * PLANE_SIZE, src + p * PLANE_SIZE, PLANE_SIZE);
            end_logo_written[bi] = 1;
        }
    }

    switch (state) {
    case 0:                              /* 白フラッシュ保持 */
        if (++frame >= WHITE_HOLD_FRAMES) {
            state = 1;
            frame = 0;
        }
        break;
    case 1:                              /* 白 → 画像 (クロスフェード) */
        if (frame <= FADE_IN_FRAMES) {
            int c = frame;
            for (int i = 0; i < 256 * 3; i++)
                work_pal[i] = (u8)(((FADE_IN_FRAMES - c) * 255 + img_pal[i] * c)
                                   / FADE_IN_FRAMES);
            video_set_palette(work_pal);
            frame++;
        } else {
            state = 2;
            frame = 0;
        }
        break;
    case 2:                              /* 音楽マーカー待ち */
        if (dis_musplus() > -16 || ++frame > MUSWAIT_MAX) {
            state = 3;
            frame = 0;
        }
        break;
    case 3:                              /* 画像 → 黒 */
        if (frame < FADE_OUT_FRAMES) {
            int c = FADE_OUT_FRAMES - 1 - frame;
            for (int i = 0; i < 256 * 3; i++)
                work_pal[i] = (u8)((img_pal[i] * c) / FADE_OUT_FRAMES);
            video_set_palette(work_pal);
            frame++;
        } else {
            state = 4;
            end_done = 1;            /* 原作 END.C の終了 (フェードアウト完了) */
        }
        break;
    default:                             /* 黒保持 */
#ifdef TEST_FIXED_SCENE
        if (++frame > 50) {              /* テスト: 繰り返し表示 */
            state = 0;
            frame = 0;
        }
#endif
        break;
    }
    return 1;                            /* プレーン直接 (c2p 不要) */
}
