/*
 * PANIC (PANICEND.EXE / SHUTDOWN.C)
 *
 * 原作 SHUTDOWN.C:57-137 の処理順をそのまま 320x256 の chunky に実装する。
 *
 *   - パレット 1..255 を白へ (fadepals[a]=(a*63+pal*(64-a))/64、色 0 は黒)
 *   - 画面を中央へ潰す (a=32,26,21,17,14,11,9,7,5,4,3 の 11 段)
 *   - 白い横一線を両端から 3 ドットずつ消して点にする (x=20..160)
 *   - 点を 60 フレーム明滅 (cos(a/120*6π)*31+32)
 *   - sleep(1)
 *
 * 原作の画面は 640x400。Amiga は 320x256 なので、縦だけ 256/400 に
 * 写す (帯の高さ = a*256/200、中心 = 128)。
 */
#include "scenes.h"
#include "../framework/video.h"
#include "../framework/c2p.h"
#include <string.h>

#define TW 320
#define TH 256
#define CY 128                 /* 画面中央 (原作の y=200/400 相当) */

extern const u8 *g_prev_frame;
extern const u8 *g_prev_pal;

static u8 kuva[TW * TH];
static u8 base[768];
static u8 fadepals[64][768];
static u8 ppal[768];

static int step;               /* 0=hold 1=squash 2=line 3=shorten 4=dot 5=black */
static int hold;
static int squash_i;
static int shorten_x;
static int dot_a;
static int black_frames;

/* 画面転送 (c2p) を、絵のある行の変化だけにする (2026-11-15 ユーザー指示:
 * 縦に潰すので c2p する行を徐々に減らせる)。chunky のうち 0 でない
 * 可能性がある行を pc_y0..pc_y1-1 で持ち、描き直すときは「前の行範囲」と
 * 「新しい行範囲」だけを変換対象にする (それ以外の行は前後とも 0)。
 * 線を短くする段階と点の段階は中央の 1 行だけ。初めて使う裏画面は
 * 256 行全体を変換する。 */
static c2p_track pn_trk;
static int pc_y0, pc_y1;

/* chunky の内容を行 y0..y1-1 に置き換える前に呼ぶ: 古い行のうち新しい
 * 範囲の外を 0 にし、古い範囲と新しい範囲を変換対象にする。 */
static void pn_rows(u8 *chunky, int y0, int y1) {
    for (int y = pc_y0; y < pc_y1; y++)
        if (y < y0 || y >= y1)
            memset(chunky + (u32)y * TW, 0, TW);
    c2p_track_add(&pn_trk, pc_y0, pc_y1, 0, TW);
    c2p_track_add(&pn_trk, y0, y1, 0, TW);
    pc_y0 = y0;
    pc_y1 = y1;
}

/* 原作の潰しの段 (SHUTDOWN.C:91  a=32; a>2; a=a*5/6) */
static const u8 squash_a[11] = { 32, 26, 21, 17, 14, 11, 9, 7, 5, 4, 3 };

/* 点の明滅 (SHUTDOWN.C:133  cos(a/120*6π)*31+32、6bit) */
static const u8 dot_pulse[60] = {
    63, 63, 61, 60, 57, 54, 50, 46, 42, 37, 32, 27,
    22, 18, 14, 10,  7,  4,  3,  1,  1,  1,  3,  4,
     7, 10, 14, 18, 22, 27, 32, 37, 42, 46, 50, 54,
    57, 60, 61, 63, 63, 63, 61, 60, 57, 54, 50, 46,
    42, 37, 32, 27, 22, 18, 14, 10,  7,  4,  3,  1,
};

/* パレット 1..255 を白へ。a=0 で元の色、a=63 で白。色 0 は黒。 */
static void set_fade(int a) {
    if (a < 0) a = 0;
    if (a > 63) a = 63;
    for (int i = 0; i < 768; i++)
        ppal[i] = (u8)((a * 255 + base[i] * (63 - a)) / 63);
    ppal[0] = ppal[1] = ppal[2] = 0;
    video_set_palette(ppal);
}

/* 画像を中央の帯 [CY-h/2, CY-h/2+h) へ縦圧縮する。外は黒。 */
static void draw_squash(u8 *chunky, int h) {
    int top = CY - h / 2;
    pn_rows(chunky, top, top + h);
    for (int y = 0; y < h; y++) {
        int sy = (y * TH + h / 2) / h;
        if (sy > TH - 1) sy = TH - 1;
        memcpy(chunky + (u32)(top + y) * TW, kuva + (u32)sy * TW, TW);
    }
}

/* 中央の横一線 (画像の中央行)。x0..x1 の範囲だけ残す。 */
static void draw_line(u8 *chunky, int x0, int x1) {
    const u8 *src = kuva + (TH / 2) * TW;
    u8 *dst = chunky + (u32)CY * TW;
    pn_rows(chunky, CY, CY + 1);
    memset(dst, 0, TW);
    for (int x = x0; x <= x1 && x < TW; x++)
        if (x >= 0)
            dst[x] = src[x];
}

void panic_init(void) {
    if (g_prev_frame)
        memcpy(kuva, g_prev_frame, TW * TH);
    else
        memset(kuva, 0, sizeof(kuva));
    if (g_prev_pal)
        memcpy(base, g_prev_pal, 768);
    else
        memset(base, 0, sizeof(base));

    /* SHUTDOWN.C:62  fadepals[a][b]=(a*63+kuvapal[b]*(64-a))/64
     * 8bit では白=255。色 0..2 は 0 のまま。 */
    for (int a = 0; a < 64; a++)
        for (int b = 3; b < 768; b++)
            fadepals[a][b] = (u8)((a * 255 + base[b] * (64 - a)) / 64);

    step = 0;
    hold = 0;
    squash_i = 0;
    shorten_x = 20;
    dot_a = 0;
    black_frames = 0;

    set_fade(3);                   /* 原作の fadepals[3] */
    c2p_track_reset(&pn_trk);
    pc_y0 = 0;                     /* main が chunky を消すが、最初は全体扱い */
    pc_y1 = TH;
}

int panic_finished(void) {
    return step == 5 && black_frames >= 50;
}

int panic_render(u8 *chunky, u8 *planes, int width, int height) {
    (void)width;
    (void)height;

    switch (step) {
    case 0:                        /* 原作の最初の 2 フレーム */
        /* 画像全体を表示 (原作はミラーを下に置くが、Amiga では
         * 前フレームをそのまま見せる) */
        pn_rows(chunky, 0, TH);
        memcpy(chunky, kuva, TW * TH);
        if (++hold >= 2) {
            step = 1;
            squash_i = 0;
        }
        break;

    case 1: {                      /* 潰し (SHUTDOWN.C:91-102) */
        int a = squash_a[squash_i];
        int h = (a * TH + 100) / 200;   /* 400 行 -> 256 行 */
        set_fade(63 - a);          /* 31..60: 白へ */
        draw_squash(chunky, h);
        if (++squash_i >= 11)
            step = 2;
        break;
    }

    case 2:                        /* 白い横一線 (SHUTDOWN.C:104-105) */
        set_fade(60);
        draw_line(chunky, 0, TW - 1);
        step = 3;
        shorten_x = 20;
        break;

    case 3:                        /* 線を両端から消す (SHUTDOWN.C:116-127)
                                    * 線は case 2 で 1 回だけ描く。ここで
                                    * 描き直すと前フレームの消去が戻る。 */
        {
            int x = shorten_x;
            u8 *dst = chunky + (u32)CY * TW;
            c2p_track_add(&pn_trk, CY, CY + 1, 0, TW);
            for (int k = 0; k < 4; k++) {
                int l = x + k;
                int r = TW - 1 - l;
                if (l >= 0 && l < TW) dst[l] = 0;
                if (r >= 0 && r < TW) dst[r] = 0;
            }
            shorten_x += 3;
            if (shorten_x > 160)
                step = 4;
        }
        break;

    case 4:                        /* 点の明滅 (SHUTDOWN.C:129-135) */
        pn_rows(chunky, CY, CY + 1);
        memset(chunky + (u32)CY * TW, 0, TW);
        if (dot_a < 60) {
            int b = dot_pulse[dot_a];
            for (int i = 0; i < 768; i++)
                ppal[i] = 0;
            ppal[3] = ppal[4] = ppal[5] = (u8)(b * 255 / 63);
            video_set_palette(ppal);
            chunky[(u32)CY * TW + 160] = 1;
            dot_a++;
        } else {
            memset(ppal, 0, 768);
            video_set_palette(ppal);
            step = 5;
        }
        break;

    default:                       /* sleep(1) */
        pn_rows(chunky, CY, CY);
        if (black_frames < 50)
            black_frames++;
        break;
    }

    c2p_track_flush(&pn_trk, chunky, planes);
    return 1;
}
