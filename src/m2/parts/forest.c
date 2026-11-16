/*
 * M4 シーン: FOREST (MNTSCRL / Vuori-Scrolli) — オリジナル FOREST/READ2.PAS の移植
 * (最終版。FOREST/X.BAT が read2.exe を mntscrl.exe としてコピーする)
 *
 * オリジナルは VGA 320x200 (Mode 13h) で、HILLBACK の山背景に
 *  - POS1-3.DAT: 31 レイヤ x 237 色の更新ピクセル位置リスト
 *  - O2.SCI: 640x31 のスクロール文字 (fbuf) を 237x31 の窓 font へ流し込む
 * を重ねる。putrouts は各リスト位置について
 *     screen[pos] = background[pos] + font[bx]
 * を書き込む。font は 3 フレームごとに 1 列左へ送り、右端に fbuf の次の列を入れる。
 *
 * Amiga 版:
 *  - 320x256 のカスタム画面に 320x200 を中央配置 (y=28)
 *  - POS リストはビッグエンディアン化した forest1-3.bin
 *  - 文字は forest_mask.bin (640x31、非ゼロに +128)。
 *  - リスト位置のみ planar に直接書き込む
 */
#include "scenes.h"
#include "../framework/mem.h"
#include "../framework/video.h"
#include "../framework/c2p.h"
#include "../framework/vblank.h"
#include "../framework/dis.h"
#include "../music/music.h"

#include <proto/exec.h>
#include <exec/memory.h>

#define F_W         320
#define F_H         200
#define F_RB        40
#define F_ROWS      256
#define F_PS        (F_RB * F_ROWS)
#define F_YOFF      28

#define MASK_W      237
#define MASK_H      31
#define FBUF_W      640             /* READ2.PAS fbuf: 640x31 (o2.sci) */

/* READ2.PAS の font: 237x31 の窓。putrouts は font[e] (e=0..237*31-1) を
 * 線形に参照する (ROUTINES.ASM)。 */
static u8 font[MASK_W * MASK_H + 1];

extern const u8 *g_forest_bg;
extern const u8 *g_forest_pal;
extern const u8 *g_forest_mask;
extern const u8 *g_forest_pos[3];

static u8 *forest_planes;
/* 描画はファスト RAM の写し (forest_fast) に行い、フレームの最後に
 * チップ RAM (forest_planes) へ 32bit で写す。1 ドットごとに 8 プレーンを
 * 読み書きする wput をチップ RAM に直接行うと、実機で非常に遅い
 * (2026-11-15、68060 で確認された問題への対策)。 */
static u8 *forest_fast;

/* 画面転送 (c2p) を、位置リストの点がある範囲だけにする (2026-11-15
 * ユーザーと相談)。forest_step が書くのは POS1-3 の位置リストの点だけで、
 * リストは固定なので、行ごとの左端・右端 (fs_x0/fs_x1) を最初に一度だけ
 * 求めておき、点を書いたフレームはその範囲だけを変換する。画面は 1 枚
 * (カスタム画面) なので、範囲外は前回変換したまま変わらない。
 * 背景を描いた直後 (init) は 200 行全体を変換する。 */
static s16 fs_x0[F_H], fs_x1[F_H];     /* 行ごとの点の範囲 (x1 は含まない) */
static int fs_spans_ready;
static int forest_dirty;

static void forest_build_spans(void) {
    for (int y = 0; y < F_H; y++) {
        fs_x0[y] = F_W;
        fs_x1[y] = 0;
    }
    for (int k = 0; k < 3; k++) {
        const u8 *list = g_forest_pos[k];
        for (int e = 0; e < MASK_W * MASK_H; e++) {
            int n = ((int)list[0] << 8) | list[1];
            list += 2;
            for (int i = 0; i < n; i++) {
                int pos = ((int)list[0] << 8) | list[1];
                list += 2;
                int y = pos / F_W, x = pos % F_W;
                if (y >= F_H)
                    continue;
                if (x < fs_x0[y]) fs_x0[y] = (s16)x;
                if (x + 1 > fs_x1[y]) fs_x1[y] = (s16)(x + 1);
            }
        }
    }
    fs_spans_ready = 1;
}

static void forest_flush(int full) {
    /* forest_fast は 1 バイト/ドットの chunky (320x256)。 */
    if (full || !fs_spans_ready) {
        c2p8_rect(forest_fast + F_YOFF * F_W, forest_planes, F_W, 256, F_YOFF, F_H);
        forest_dirty = 0;
        return;
    }
    if (!forest_dirty)
        return;
    forest_dirty = 0;
    /* 同じ 32 ドット列の範囲が続く行はまとめて変換する */
    int y = 0;
    while (y < F_H) {
        if (fs_x1[y] <= fs_x0[y]) {
            y++;
            continue;
        }
        int b0 = fs_x0[y] >> 5, b1 = (fs_x1[y] + 31) >> 5;
        int y1 = y + 1;
        while (y1 < F_H && fs_x1[y1] > fs_x0[y1] &&
               (fs_x0[y1] >> 5) == b0 && ((fs_x1[y1] + 31) >> 5) == b1)
            y1++;
        c2p8_cols(forest_fast + (F_YOFF + y) * F_W, forest_planes,
                  F_YOFF + y, y1 - y, b0, b1);
        y = y1;
    }
}
static int scp, sss;
static u32 forest_last_vbl;

/* 原作 Forest.cpp:60-152 の開始フェード。
 *  phase 0: 64 フレーム、色 0..31 と 128..159 を黒のまま、
 *           それ以外を 0 から 1 ずつ (6bit) 上げる。
 *  phase 4: 音楽が MUSIC1 35:17 に達するまで待つ (原作動画の実測)。
 *  phase 1: 126 フレーム (63*2)、0..176 を 1/63 ずつ上げる。
 *           この間もスクロールは毎フレーム進む。
 *  phase 2: 通常スクロール (veke=2800 フレーム)。
 *  phase 3: Forest.cpp:154-212 の終了フェード (musplus == -11 で開始、
 *           64 フレームで 0 まで下げる)。 */
static int fade_phase;
static int fade_step;
static int fade_acc;
static int forest_frame;
static int fadeout_fp;
static u8 fade_pal[768];
static int forest_done;

static inline void wput(int x, int y, u8 c) {
    /* 1 バイト/ドットで書く (以前は 8 プレーンの読み書き)。1 歩で約 1 万
     * ドットを書くので、プレーン単位だと実機 (68060) で 1 歩が重く、
     * 追いつき歩数が増えて表示が 1/10 程度まで落ちていた (2026-11-15)。 */
    forest_fast[y * F_W + x] = c;
}

static void draw_background(void) {
    const u8 *bg = g_forest_bg;
    for (int y = 0; y < F_H; y++) {
        const u8 *row = bg + y * F_W;
        for (int x = 0; x < F_W; x++)
            wput(x, y + F_YOFF, row[x]);
    }
}

static int forest_armed;
void forest_init(void) {
    forest_armed = 0;
    /* READ2.PAS:149-150: font の 104 列目から fbuf の先頭 133 列を置き
     * (残りは 0)、scp=133。以後 3 フレームごとに 1 列ずつ左へ送り、
     * 右端 (236 列) に fbuf の scp 列を入れる。 */
    for (int i = 0; i < (int)sizeof(font); i++)
        font[i] = 0;
    if (g_forest_mask)
        for (int r = 0; r < MASK_H; r++)
            for (int c = 0; c < 133; c++)
                font[r * MASK_W + 104 + c] = g_forest_mask[r * FBUF_W + c];
    scp = 133;
    sss = 0;
    forest_last_vbl = vblank_frames();
    fade_phase = 0;
    fade_step = 0;
    fade_acc = 0;
    forest_frame = 0;
    fadeout_fp = 0;
    forest_done = 0;

    if (!g_forest_bg || !g_forest_pal || !g_forest_mask ||
        !g_forest_pos[0] || !g_forest_pos[1] || !g_forest_pos[2])
        return;

    if (!forest_planes) {
        forest_planes = (u8 *)mem_alloc(8 * F_PS, MEMF_CHIP | MEMF_CLEAR);
        if (!forest_planes)
            return;
        forest_fast = (u8 *)mem_alloc(8 * F_PS, MEMF_FAST | MEMF_CLEAR);
        if (!forest_fast)
            forest_fast = (u8 *)mem_alloc(8 * F_PS, MEMF_ANY | MEMF_CLEAR);
        if (!forest_fast)
            return;
    }
    for (int i = 0; i < 8 * F_PS; i++)
        forest_planes[i] = forest_fast[i] = 0;

    if (!fs_spans_ready)
        forest_build_spans();
    draw_background();
    forest_flush(1);
    for (int i = 0; i < 768; i++)
        fade_pal[i] = 0;
    video_set_palette(fade_pal);       /* 原作は黒から開始 */
    video_set_custom_screen(forest_planes, F_RB);
}

/* READ2.PAS: "waitr; scr(sss); if sss=2 then sss:=0 else inc(sss);" and
 * scr() scrolls the font window on the third group, i.e. one step per 70 Hz frame and
 * one pixel of scroll every three frames.  PAL runs at 50 Hz, so we execute
 * 1.4 of those steps per VBL (7/5). */
static void forest_step(void)
{
    const u8 *list = g_forest_pos[sss];
    const u8 *bg = g_forest_bg;
    const u8 *mask = font;
    u8 *dst = forest_fast + F_YOFF * F_W;

    for (int e = 0; e < MASK_W * MASK_H; e++) {
        int n = ((int)list[0] << 8) | list[1];
        list += 2;
        int add = mask[e];
        for (int i = 0; i < n; i++) {
            int pos = ((int)list[0] << 8) | list[1];
            list += 2;
            /* wput(pos % 320, pos / 320 + F_YOFF) と同じ場所
             * (y*320+x = pos)。1 点ごとの割り算をやめた (bench: 割り算
             * 1 回 約 0.8us、1 歩で約 1 万点)。 */
            dst[pos] = (u8)(bg[pos] + add);
        }
    }
    forest_dirty = 1;

    if (sss == 2) {
        /* READ2.PAS:85-87: move(font[1],font[0],237*31);
         * font[ff*237+236] := fbuf[ff*640+scp]; if scp<639 then inc(scp) */
        for (int i = 0; i < MASK_W * MASK_H; i++)
            font[i] = font[i + 1];
        for (int r = 0; r < MASK_H; r++)
            font[r * MASK_W + MASK_W - 1] = g_forest_mask[r * FBUF_W + scp];
        if (scp < FBUF_W - 1)
            scp++;
        sss = 0;
    } else {
        sss++;
    }
}

int forest_finished(void) {
    return forest_done;
}

int forest_render(u8 *chunky, u8 *planes, int width, int height) {
    (void)chunky;
    (void)planes;
    (void)width;
    (void)height;

    if (!forest_planes || !g_forest_bg || !g_forest_mask)
        return 1;

    u32 now = vblank_frames();
    u32 el = now - forest_last_vbl;
    if (el > 6)
        el = 6;
    forest_last_vbl = now;

    /* 70Hz の 1 フレームを 1.4/VBL で進める (7/5)。 */
    fade_acc += (int)el * 7;
    int steps = fade_acc / 5;
    fade_acc -= steps * 5;
    if (steps > 3)   /* 追いつきは 3 歩まで (描画が遅い機種での悪循環を防ぐ) */
        steps = 3;

    for (int sp = 0; sp < steps; sp++) {
        if (fade_phase == 0) {
            /* Forest.cpp:68-82: 64 フレームのフェードイン。
             * tmppal の 0..31 と 128..159 は 0。 */
            /* READ2.PAS:132-143: 毎フレーム setrgb(fpal) してから、
             * fpal の各成分を tmppal に届くまで 1 ずつ上げる
             * (比例ではなく、暗い色ほど早く届く)。6bit の 1 = 8bit の 4。 */
            if (fade_step < 64) {
                video_set_palette(fade_pal);
                for (int c = 0; c < 256; c++) {
                    int masked = (c < 32) || (c >= 128 && c < 160);
                    for (int k = 0; k < 3; k++) {
                        int idx = c * 3 + k;
                        int t = masked ? 0 : g_forest_pal[idx];
                        int v = fade_pal[idx] + 4;
                        fade_pal[idx] = (u8)(v > t ? t : v);
                    }
                }
                fade_step++;
            }
            if (fade_step >= 64) {
                /* Forest.cpp:84-92: tmppal=pal, fpal=pal、
                 * ただし 0..31 と 128..159 は 0 から始める。 */
                for (int i2 = 0; i2 < 768; i2++)
                    fade_pal[i2] = g_forest_pal[i2];
                for (int c = 0; c < 32 * 3; c++)
                    fade_pal[c] = 0;
                for (int c = 128 * 3; c < 160 * 3; c++)
                    fade_pal[c] = 0;
                video_set_palette(fade_pal);
                fade_phase = 4;
                fade_step = 0;
            }
        } else if (fade_phase == 4) {
            /* 文字フェードの開始待ち。
             * 原作ソース READ2.PAS:156-159 ("cmp dx,0; jl @a" = musplus が
             * 負の間待つ) では、マーカー直後 (musplus>0, U2.ASM:843-850)
             * に始まる MNTSCRL ではほぼ待たず 35:09 頃に始まる。C++ 版
             * Forest.cpp:98-101 (正の間待つ) では 35:32。
             * 原作動画の文字フェード開始 (233.47 s) を音で位置合わせすると
             * 35:17 (2026-11-15 計測。35:20 で走らせたら原作より 0.31 s =
             * 2.7 行遅かった。1 行 = speed 6 / tempo 130 = 0.115 s)。
             * ソースの条件ではどちらも合わないため、ユーザー承認のうえ
             * TECHNO の 17:11 と同じく音楽位置で待つ。 */
            if (((int)np_ord << 8 | (np_row & 0xff)) < ((35 << 8) | 17))
                break;
            fade_phase = 1;
        } else if (fade_phase == 1) {
            /* Forest.cpp:108-152: 63*2 フレーム。奇数フレームで
             * パレットを書き、0..176 を 1 ずつ上げる。 */
            if (fade_step & 1) {
                video_set_palette(fade_pal);
                for (int i2 = 0; i2 <= 176 * 3; i2++) {
                    /* 原作の fpal は 6bit。1 フレームに 1 上げる =
                     * 8bit では 4 上げる (63 段階 -> 255)。 */
                    if (fade_pal[i2] < g_forest_pal[i2]) {
                        int v = fade_pal[i2] + 4;
                        fade_pal[i2] = (u8)(v > g_forest_pal[i2]
                                            ? g_forest_pal[i2] : v);
                    }
                }
            }
            forest_step();
            fade_step++;
            if (fade_step >= 126) {
                for (int i2 = 0; i2 < 768; i2++)
                    fade_pal[i2] = g_forest_pal[i2];
                video_set_palette(fade_pal);
                fade_phase = 2;
                forest_frame = 0;
            }
        } else if (fade_phase == 2) {
            forest_step();
            forest_frame++;
            /* 原作 READ2.PAS は毎フレーム "musplus == -11" を見る。描画が
             * 1 行 (約 0.1 秒) より遅い実機ではその 1 行を見逃し、終了フェードに
             * 入らず止まったように見えていた (2026-11-15 ユーザー報告)。
             * -11 から 0 になる手前 (-11..-1) のどこかで気づけば同じ所で始まる。 */
            if (dis_mus_reached(&forest_armed, -12))
                fade_phase = 3, fadeout_fp = 0;
            else if (forest_frame >= 2800)
                fade_phase = 3, fadeout_fp = 0;
        } else {
            /* Forest.cpp:161-212: 終了フェード。64 フレームかけて
             * fpal を 0 まで下げる。 */
            forest_step();
            if (fadeout_fp >= 64) {
                forest_done = 1;
                continue;
            }
            fadeout_fp++;
            video_set_palette(fade_pal);
            for (int i2 = 0; i2 < 768; i2++) {
                /* 原作の 6bit で 1 下げる = 8bit で 4 下げる。 */
                fade_pal[i2] = fade_pal[i2] > 4
                    ? (u8)(fade_pal[i2] - 4) : 0;
            }
        }
    }

    forest_flush(0);
    return 1;
}

/* パートが終わったあと、チップ RAM の画面バッファを返す (main.c から、
 * 表示が標準画面に戻った後に呼ぶ)。Workbench から起動するとチップ RAM に
 * 余裕が少なく、後半の ENDSCRL の画面が確保できずに始まらなかった
 * (2026-11-15 ユーザー報告)。次にこのパートを使うときは確保し直す。 */
void forest_release(void) {
    if (forest_planes) {
        mem_free(forest_planes, 8 * F_PS);
        forest_planes = 0;
    }
    /* ファスト RAM の作業バッファも返す (2026-11-16: -Start 6 から通すと
     * 3DSINFLD の確保 (SINUS-nomem) が失敗して黒画面になった。終わった
     * パートのファスト RAM が残ったままだった)。次に使うときは確保し直す。 */
    if (forest_fast) {
        mem_free(forest_fast, 8 * F_PS);
        forest_fast = 0;
    }
}
