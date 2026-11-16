/*
 * M4 シーン: WATER (RAYSCRL) — オリジナル WATER/DEMO.PAS + ROUTINES.ASM の移植
 *
 * オリジナルは VGA 320x200 (Mode 13h) で、TAUSTA の背景に
 *  - WAT1-3.DAT: 「レイヤ (34) x 色 (158)」ごとの更新ピクセル位置リスト
 *  - MIEKKA.SCI: 400x35 のフォント/水面マスク (横スクロール)
 * を重ねる。putrouts1 は各リスト位置について
 *     color = fbuf[entry] ? fbuf[entry] : background[pos]
 * を書き込む (fbuf は 158x34 のスクロールバッファ)。
 *
 * Amiga 版:
 *  - 320x256 の 8 プレーンをカスタム画面としてチップ RAM に保持
 *    (背景は 320x200 を y=28 に配置)
 *  - WAT リストはビッグエンディアン化した water1-3.bin を使用
 *  - fbuf を 3 フレームごとに 1 列スクロールし、フォント列を右端へ追加
 *  - リスト位置のみを planar に直接書き換える (全画面コピーなし)
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

#define W_W         320
#define W_H         200
#define W_RB        40                  /* 320 / 8 */
#define W_ROWS      256
#define W_PS        (W_RB * W_ROWS)     /* 1 プレーン 10,240 バイト */
#define W_YOFF      28                  /* 320x200 を 256 行の中央へ */

#define FB_W        158
#define FB_H        34
#define FB_SIZE     (FB_W * FB_H)

#define FONT_W      400
#define FONT_H      35
#define FONT_END    390                 /* scp の上限 (オリジナル same) */

extern const u8 *g_water_bg;
extern const u8 *g_water_pal;
extern const u8 *g_water_font;
extern const u8 *g_water_wat[3];

static u8 *water_planes;                /* 8 プレーン x W_PS (チップ RAM) */
/* 描画はファスト RAM の写し (water_fast) に行い、フレームの最後に
 * チップ RAM (water_planes) へ 32bit で写す。1 ドットごとに 8 プレーンを
 * 読み書きする wput をチップ RAM に直接行うと、実機で非常に遅い
 * (2026-11-15、68060 で確認された問題への対策)。 */
static u8 *water_fast;

/* 画面転送 (c2p) を、剣を描く点がある場所だけにする (2026-11-15 ユーザー
 * 指示)。water_step が書くのは WAT1-3 の位置リストの点だけで、リストは
 * 固定なので、行ごとに「点がある 32 ドット列」のビット (ws_mask) を最初に
 * 一度だけ求めておく。点を書いたフレームは、その列だけを変換する (1 行に
 * 離れた場所が数か所あってもよい)。画面は 1 枚 (カスタム画面) なので、
 * それ以外は前回変換したまま変わらない。背景を描いた直後 (init) は
 * 200 行全体を変換する。 */
static u16 ws_mask[W_H];
static int ws_ready;
static int water_dirty;

static void water_build_mask(void) {
    for (int y = 0; y < W_H; y++)
        ws_mask[y] = 0;
    for (int k = 0; k < 3; k++) {
        const u8 *list = g_water_wat[k];
        for (int e = 0; e < FB_SIZE; e++) {
            int n = ((int)list[0] << 8) | list[1];
            list += 2;
            for (int i = 0; i < n; i++) {
                int pos = ((int)list[0] << 8) | list[1];
                list += 2;
                int y = pos / W_W, x = pos % W_W;
                if (y < W_H)
                    ws_mask[y] |= (u16)(1u << (x >> 5));
            }
        }
    }
    ws_ready = 1;
}

static void water_flush(int full) {
    /* water_fast は 1 バイト/ドットの chunky (320x256)。 */
    if (full || !ws_ready) {
        c2p8_rect(water_fast + W_YOFF * W_W, water_planes, W_W, 256, W_YOFF, W_H);
        water_dirty = 0;
        return;
    }
    if (!water_dirty)
        return;
    water_dirty = 0;
    /* 同じビットの行が続くところはまとめ、ビットが続く列ごとに変換する */
    int y = 0;
    while (y < W_H) {
        u16 m = ws_mask[y];
        int y1 = y + 1;
        while (y1 < W_H && ws_mask[y1] == m)
            y1++;
        int b = 0;
        while (m && b < 10) {
            if (!(m & (1u << b))) {
                b++;
                continue;
            }
            int b1 = b;
            while (b1 < 10 && (m & (1u << b1)))
                b1++;
            c2p8_cols(water_fast + (W_YOFF + y) * W_W, water_planes,
                      W_YOFF + y, y1 - y, b, b1);
            b = b1;
        }
        y = y1;
    }
}
static u8 fbuf[FB_SIZE];
static int sss, scp, frame;
static u32 water_last_vbl;
static int water_step_acc;

/* 開始の流れ (WATER/DEMO.PAS:97-147):
 *   W_WAITMUS: dis_musplus() >= 0 になるまで待ち、そのときの order を CO に取る
 *              (DEMO.PAS:97-104 の @a ループ。cx = np_ord, dx = musplus)
 *   W_FADEIN : y = 0..126 の 127 フレーム。毎フレーム scr(sss) を描き、
 *              奇数フレームでパレットを 1 段 (6bit) 目標へ近づける
 *              (DEMO.PAS:116-131)
 *   W_WAITROW: order が CO から変わり、row >= 16 になるまで待つ。この間は
 *              描画しない (DEMO.PAS:134-141)
 *   W_RUN    : sss = scp = 0 からソードの横スクロール (DEMO.PAS:149-189)
 * 70Hz の 1 フレームは 1.4 VBL ステップで進める (water_step_acc)。 */
enum { W_WAITMUS, W_FADEIN, W_WAITROW, W_RUN };
static int wstate, wfade_y, wco;
static u8 wpal_cur[769], wpal_tgt[769], wpal8[768];
/* 終了 (DEMO.PAS:158-176,189): musplus == -11 でフェードアウト開始、
 * 64 フレームかけてパレットを 0 へ下げ、fp=64 で quit。 */
static int wfadeout, wfp, wquit;

int water_finished(void) {
    return wquit;
}

/* ---- planar 1 ピクセル書き込み (範囲はリスト由来で有効) ---- */
static inline void wput(int x, int y, u8 c) {
    /* 1 バイト/ドットで書く (以前は 8 プレーンの読み書き)。1 歩で約 1 万
     * ドットを書くので、プレーン単位だと実機 (68060) で 1 歩が重く、
     * 追いつき歩数が増えて表示が 1/10 程度まで落ちていた (2026-11-15)。 */
    water_fast[y * W_W + x] = c;
}

/* ---- 背景の初期描画 (320x200 を y=W_YOFF に置く) ---- */
static void draw_background(void) {
    const u8 *bg = g_water_bg;
    for (int y = 0; y < W_H; y++) {
        const u8 *row = bg + y * W_W;
        for (int x = 0; x < W_W; x++)
            wput(x, y + W_YOFF, row[x]);
    }
}

static int water_armed;
void water_init(void) {
    water_armed = 0;
    sss = 0;
    scp = 0;
    frame = 0;
    water_last_vbl = vblank_frames();
    water_step_acc = 0;
    for (int i = 0; i < FB_SIZE; i++)
        fbuf[i] = 0;

    if (!g_water_bg || !g_water_pal || !g_water_font ||
        !g_water_wat[0] || !g_water_wat[1] || !g_water_wat[2])
        return;

    if (!water_planes) {
        water_planes = (u8 *)mem_alloc(8 * W_PS, MEMF_CHIP | MEMF_CLEAR);
        if (!water_planes)
            return;
        water_fast = (u8 *)mem_alloc(8 * W_PS, MEMF_FAST | MEMF_CLEAR);
        if (!water_fast)
            water_fast = (u8 *)mem_alloc(8 * W_PS, MEMF_ANY | MEMF_CLEAR);
        if (!water_fast)
            return;
    }
    /* 前回の内容を消す */
    for (int i = 0; i < 8 * W_PS; i++)
        water_planes[i] = water_fast[i] = 0;

    if (!ws_ready)
        water_build_mask();
    draw_background();
    water_flush(1);
    /* DEMO.PAS:110: 全色 0 にしてからフェードインする。
     * DEMO.PAS:113-114: tmppal = 目標パレット, pal = 0 (6bit)。 */
    for (int i = 0; i < 768; i++) {
        wpal_tgt[i] = (u8)((g_water_pal[i] * 63 + 127) / 255);
        wpal_cur[i] = 0;
        wpal8[i] = 0;
    }
    wpal_tgt[768] = wpal_cur[768] = 0;
    video_set_palette(wpal8);
    wstate = W_WAITMUS;
    wfade_y = 0;
    wfadeout = 0;
    wfp = 0;
    wquit = 0;
    wco = 0;
    video_set_custom_screen(water_planes, W_RB);
}

/* One original 70 Hz frame: draw one scanline group (wat1/2/3) and, every
 * third frame, scroll the text mask by one pixel.  PAL runs at 50 Hz, so we
 * must execute 70/50 = 1.4 of these steps per VBL or the scroll is 0.71x. */
static void water_step(int scroll)
{
    const u8 *list = g_water_wat[sss];
    const u8 *bg = g_water_bg;
    u8 *dst = water_fast + W_YOFF * W_W;

    for (int e = 0; e < FB_SIZE; e++) {
        int n = ((int)list[0] << 8) | list[1];
        list += 2;
        u8 fc = fbuf[e];
        for (int i = 0; i < n; i++) {
            int pos = ((int)list[0] << 8) | list[1];
            list += 2;
            /* wput(pos % 320, pos / 320 + W_YOFF) と同じ場所 (y*320+x = pos)。
             * 1 点ごとの割り算をやめた (bench: 割り算 1 回 約 0.8us)。 */
            dst[pos] = fc ? fc : bg[pos];
        }
    }
    water_dirty = 1;

    /* every 3rd frame: scroll the text mask one pixel left.
     * フェードイン中 (DEMO.PAS:116-131) は sss だけ回してスクロールしない。 */
    if (sss == 2 && !scroll) {
        sss = 0;
    } else if (sss == 2) {
        for (int row = 0; row < FB_H; row++) {
            u8 *r = fbuf + row * FB_W;
            for (int col = 0; col < FB_W - 1; col++)
                r[col] = r[col + 1];
            r[FB_W - 1] = g_water_font[row * FONT_W + scp];
        }
        if (scp < FONT_END)
            scp++;
        sss = 0;
    } else {
        sss++;
    }
    frame++;
}

int water_render(u8 *chunky, u8 *planes, int width, int height) {
    (void)chunky;
    (void)planes;
    (void)width;
    (void)height;

    if (!water_planes || !g_water_bg)
        return 1;

    u32 now = vblank_frames();
    u32 el = now - water_last_vbl;
    if (el > 6)
        el = 6;
    water_last_vbl = now;
    water_step_acc += (int)el * 7;
    int steps = water_step_acc / 5;
    water_step_acc -= steps * 5;
    if (steps > 3)   /* 追いつきは 3 歩まで (描画が遅い機種での悪循環を防ぐ) */
        steps = 3;
    for (int sp = 0; sp < steps; sp++) {
        if (wstate == W_WAITMUS) {
            /* 窓 (0..31) を見逃しても止まらないよう、マーカー (std 75) を
             * 過ぎていれば始める (2026-11-15)。 */
            if (dis_musplus() < 0 &&
                (((int)np_ord << 8) | (np_row & 0xff)) < (75 << 8))
                continue;
            wco = np_ord;
            wstate = W_FADEIN;
            wfade_y = 0;
        }
        if (wstate == W_FADEIN) {
            if (wfade_y & 1) {
                /* DEMO.PAS:122-127: 現在値を出してから 1 段進める。
                 * pf := 0 to 3 なので次の色の R まで触る (原作どおり)。 */
                for (int x = 0; x < 256; x++) {
                    wpal8[x * 3 + 0] = (u8)(wpal_cur[x * 3 + 0] * 255 / 63);
                    wpal8[x * 3 + 1] = (u8)(wpal_cur[x * 3 + 1] * 255 / 63);
                    wpal8[x * 3 + 2] = (u8)(wpal_cur[x * 3 + 2] * 255 / 63);
                    for (int pf = 0; pf <= 3; pf++)
                        if (wpal_cur[x * 3 + pf] < wpal_tgt[x * 3 + pf])
                            wpal_cur[x * 3 + pf]++;
                }
                video_set_palette(wpal8);
            }
            water_step(0);      /* scr(sss) と sss の循環 (fbuf はまだ 0) */
            if (++wfade_y > 63 * 2)
                wstate = W_WAITROW;
            continue;
        }
        if (wstate == W_WAITROW) {
            if (np_ord == wco || np_row < 16)
                continue;
            /* DEMO.PAS:149-154 */
            sss = 0;
            scp = 0;
            for (int i = 0; i < FB_SIZE; i++)
                fbuf[i] = 0;
            wstate = W_RUN;
        }
        {   /* DEMO.PAS:158-165 は毎フレーム "== -11"。描画が遅い実機で 1 行を
             * 見逃しても止まらないよう、-11..-1 の間に気づけば始める。 */
            if (dis_mus_reached(&water_armed, -12))
                wfadeout = 1;
        }
        if (wfadeout && !wquit) {       /* DEMO.PAS:168-176 */
            if (wfp == 64)
                wquit = 1;
            else
                wfp++;
            for (int x = 0; x < 256; x++) {
                wpal8[x * 3 + 0] = (u8)(wpal_cur[x * 3 + 0] * 255 / 63);
                wpal8[x * 3 + 1] = (u8)(wpal_cur[x * 3 + 1] * 255 / 63);
                wpal8[x * 3 + 2] = (u8)(wpal_cur[x * 3 + 2] * 255 / 63);
                for (int pf = 0; pf <= 3; pf++)
                    if (wpal_cur[x * 3 + pf] > 0)   /* tmppal は全 0 */
                        wpal_cur[x * 3 + pf]--;
            }
            video_set_palette(wpal8);
        }
        water_step(1);
    }

    water_flush(0);
    return 1;
}

/* パートが終わったあと、チップ RAM の画面バッファを返す (main.c から、
 * 表示が標準画面に戻った後に呼ぶ)。Workbench から起動するとチップ RAM に
 * 余裕が少なく、後半の ENDSCRL の画面が確保できずに始まらなかった
 * (2026-11-15 ユーザー報告)。次にこのパートを使うときは確保し直す。 */
void water_release(void) {
    if (water_planes) {
        mem_free(water_planes, 8 * W_PS);
        water_planes = 0;
    }
    /* ファスト RAM の作業バッファも返す (2026-11-16: -Start 6 から通すと
     * 3DSINFLD の確保 (SINUS-nomem) が失敗して黒画面になった。終わった
     * パートのファスト RAM が残ったままだった)。次に使うときは確保し直す。 */
    if (water_fast) {
        mem_free(water_fast, 8 * W_PS);
        water_fast = 0;
    }
}
