/*
 * M4 scene: SINUS (3D-Sinusfield) -- faithful port of COMAN/3DSINFLD.EXE
 *
 * The original renders a Comanche-style voxel landscape with a 386 assembly
 * routine (COMAN/THELOOP.INC, 5,207 lines, 192 unrolled ray-march steps).
 * That routine is translated to C 1:1 by tools/theloop_gen.py, so the output
 * is bit-exact with the original.
 *
 * Driver ported from COMAN/MAIN.C:
 *   - wave tables W1DTA.BIN / W2DTA.BIN (2 x 32768 x 16 bit)
 *   - rot/rot2 camera rotation, xwav/ywav wave scroll
 *   - 160 columns x 200 rows frame buffer, each column is 2 screen pixels
 *     wide; columns 0..79 are the left pixel of each pair, 80..159 the right
 *
 * PAL 50Hz: the original advances one frame per 70Hz vsync, so all motion
 * values are scaled by 7/5 (see AGENTS.md).
 */
#include "scenes.h"
#include "../framework/mem.h"
#include "../framework/video.h"
#include "../framework/vblank.h"
#include "../framework/dis.h"
#include "../framework/c2p.h"
#include "../music/music.h"
#include "theloop_gen.h"
#include "theloop_blocks.h"

/* 1: use the hand written 68k loop, 0: use the C translation */
#define USE_ASM_THELOOP 1

extern void sinus_theloop_run(void);

#include <proto/exec.h>
#include <exec/memory.h>

#define S_W         320
#define S_H         200
#define S_YOFF      28

#define NWAVE       32768

extern const u8 *g_sinus_w1;
extern const u8 *g_sinus_w2;

static u16 *sin_w1;
static u16 *sin_w2;
static u8 *sin_vbuf;
static u8 *sin_scratch;

/* theloop_gen.c interface */
u16 tl_si, tl_di, tl_cx, tl_dy, tl_bp, tl_ax;
const u16 *tl_wave1;
const u16 *tl_wave2;
u8 *tl_vbuf;

static int sin1024[1024];
static int rot, rot2, xwav, ywav, sin_frame, startrise;
/* 原作 COMAN/MAIN.C:55-60:
 *   dis_waitb(); while(!dis_exit() && dis_musplus()<0);
 *   while(frame<4444) { a=dis_musplus(); if(a>-8 && a<0) break; ... } */
static int sin_started, sin_done, sin_armed;
static int sin_ready;                /* テーブルと作業バッファが揃った */

/* 画面転送 (c2p) を、画面下の地形を写す行だけにする (2026-11-15
 * ユーザー指示)。docopy は窓の 70+startrise 行目から下 (画面の
 * 98+startrise..227 行) だけに地形を写し、それ以外の行は 0。
 * 地形の行範囲 (sn_y0..227) の行と、範囲から外れた行だけを変換し、
 * 外れた行だけを 0 にする。開始待ちの間は最初に 1 回だけ全体を 0 にする。
 * 初めて使う裏画面は 256 行全体を変換する。 */
static c2p_track sn_trk;
static int sn_y0;                   /* 地形を写している最初の行 (画面座標) */
static int sn_cleared;              /* chunky 全体を 0 にした */

int sinus_finished(void) {
    return sin_done;
}
static u32 sinus_start_vbl, sinus_last_vbl;
static int sinus_step_acc;
static int sinus_move_acc;
/* 地形の動きの歩数/秒 (原作は描画 1 回 = 1 歩)。PAL の上限は 50。 */
int g_sinus_step_hz = 50;

static int isin(int deg) { return sin1024[deg & 1023]; }

/* ---- palette (COMAN/MAIN.C) ---- */
static void make_palette(void)
{
    u8 pal[768];
    for (int i = 0; i < 768; i++)
        pal[i] = 0;

    for (int a = 0; a < 256; a++) {
        int uc = (223 - a * 22 / 26) * 3;
        int b = (230 - a) / 4;
        b += sin1024[(a * 4) & 1023] / 32;
        if (b < 0) b = 0;
        if (b > 63) b = 63;
        pal[uc + 1] = (u8)b;
        b = (255 - a) / 3;
        if (b > 63) b = 63;
        pal[uc + 2] = (u8)b;
        b = a - 220;
        if (b < 0) b = -b;
        if (b > 40) b = 40;
        b = 40 - b;
        pal[uc + 0] = (u8)(b / 3);
    }
    for (int a = 0; a < 768 - 16 * 3; a++) {
        int b = pal[a];
        b = b * 9 / 6;
        if (b > 63) b = 63;
        pal[a] = (u8)b;
    }
    for (int a = 0; a < 24; a++) {
        int uc = (255 - a) * 3;
        int b = a - 4;
        if (b < 0) b = 0;
        pal[uc + 0] = (u8)(b / 2);
        pal[uc + 1] = 0;
        pal[uc + 2] = 0;
    }
    pal[0] = pal[1] = pal[2] = 0;

    /* 6 bit -> 8 bit */
    u8 out[768];
    for (int i = 0; i < 768; i++)
        out[i] = (u8)(pal[i] * 255 / 63);
    video_set_palette(out);
}

static void docol(int si, int di, int cx, int dy, int bp)
{
    tl_si = (u16)(si & ~1);
    tl_di = (u16)(di & ~1);
    tl_cx = (u16)(cx & ~1);
    tl_dy = (u16)(dy & ~1);
    tl_bp = (u16)bp;
    tl_ax = (u16)(s16)(-270);
#if USE_ASM_THELOOP
    sinus_theloop_run();
#else
    theloop_run();
#endif
}

/* 波形テーブルと作業バッファを用意する。失敗したら 0。
 * 開始時に失敗したとき (前のパートのファスト RAM がまだ返されていない)
 * は、描画のたびにやり直す (RAYSCRL のバッファは切り替えの 2 フレーム後に
 * 返る。2026-11-16 -Start 6 で SINUS-nomem を確認)。 */
static int sinus_prepare(void)
{
    if (!sin_w1) sin_w1 = (u16 *)mem_alloc(NWAVE * 2, MEMF_FAST | MEMF_CLEAR);
    if (!sin_w2) sin_w2 = (u16 *)mem_alloc(NWAVE * 2, MEMF_FAST | MEMF_CLEAR);
    if (!sin_vbuf) sin_vbuf = (u8 *)mem_alloc(65536, MEMF_FAST | MEMF_CLEAR);
    if (!sin_scratch) sin_scratch = (u8 *)mem_alloc(320 * 256, MEMF_FAST | MEMF_CLEAR);
    if (!sin_w1 || !sin_w2 || !sin_vbuf || !sin_scratch) {
        static int warned;
        if (!warned) {
            warned = 1;
            prof_mark("SINUS-nomem");   /* 診断 (2026-11-16 黒画面の報告) */
        }
        return 0;
    }
    if (!g_sinus_w1 || !g_sinus_w2) {
        prof_mark("SINUS-nodata");
        return 0;
    }

    /* the PC tables are little endian 16 bit values */
    for (int i = 0; i < NWAVE; i++) {
        sin_w1[i] = (u16)(g_sinus_w1[i * 2] | (g_sinus_w1[i * 2 + 1] << 8));
        sin_w2[i] = (u16)(g_sinus_w2[i * 2] | (g_sinus_w2[i * 2 + 1] << 8));
    }

    tl_wave1 = sin_w1;
    tl_wave2 = sin_w2;
    tl_vbuf = sin_vbuf;

    for (int i = 0; i < 65536; i++)
        sin_vbuf[i] = 0;
    sin_ready = 1;
    return 1;
}

void sinus_init(void)
{
    for (int i = 0; i < 1024; i++) {
        /* sin1024[i] = 256 * sin(2*pi*i/1024), integer */
        int deg = i & 1023;
        int sign = 1;
        if (deg >= 512) { deg -= 512; sign = -1; }
        int u = deg * (512 - deg);
        sin1024[i] = sign * (16 * u * 256 / (5 * 262144 - 4 * u));
    }

    rot = rot2 = 0;
    xwav = ywav = 0;
    sin_frame = 0;
    sin_started = 0;
    sin_ready = 0;
    c2p_track_reset(&sn_trk);
    sn_y0 = 0;                      /* 最初は全体を対象にする */
    sn_cleared = 0;
    sin_armed = 0;
    sin_done = 0;
    startrise = 160;
    sinus_step_acc = 0;
    sinus_move_acc = 0;
    sinus_start_vbl = vblank_frames();
    sinus_last_vbl = sinus_start_vbl;

    make_palette();

    sinus_prepare();
}

int sinus_render(u8 *chunky, u8 *planes, int width, int height)
{
    (void)width;
    (void)height;

    if (!sin_ready && !sinus_prepare()) {
        static int warned;
        if (!warned) {
            warned = 1;
            prof_mark("SINUS-notready");
        }
        return 0;
    }

    if (!sin_started) {
        /* COMAN/MAIN.C:56。窓 (0..31) を描画の遅さで丸ごと見逃しても
         * 止まらないよう、マーカー (std 79) を過ぎていれば始める (2026-11-15)。 */
        if (dis_musplus() < 0 &&
            (((int)np_ord << 8) | (np_row & 0xff)) < (79 << 8)) {
            if (!sn_cleared) {
                prof_mark("SINUS-wait");    /* 診断: 開始待ちに入った位置 */
                for (int i = 0; i < 320 * 256; i++)
                    chunky[i] = 0;
                sn_cleared = 1;
                c2p_track_add(&sn_trk, 0, 256, 0, 320);
                sn_y0 = 256;        /* 地形はまだ無い */
            }
            sinus_last_vbl = vblank_frames();
            c2p_track_flush(&sn_trk, chunky, planes);
            return 1;
        }
        sin_started = 1;
        prof_mark("SINUS-start");
    }
    {
        /* COMAN/MAIN.C:59-60 は -7..-1 の窓。遅い実機で見逃しても
         * 止まらないように dis_mus_reached を使う (2026-11-15)。 */
        if (dis_mus_reached(&sin_armed, -8) || sin_frame >= 4444)
            sin_done = 1;
    }

    /* original: one iteration per 70 Hz frame -> 1.4 per PAL VBL */
    u32 now = vblank_frames();
    u32 el = now - sinus_last_vbl;
    if (el > 6)
        el = 6;
    sinus_last_vbl = now;
    sinus_step_acc += (int)el * 7;
    int steps = sinus_step_acc / 5;
    sinus_step_acc -= steps * 5;
    if (steps > 4)
        steps = 4;

    /* per-frame state update (COMAN/MAIN.C:57-76) */
    for (int st = 0; st < steps; st++) {
        int mp = dis_musplus();
        /* 終了 30 行前から startrise を +1 して下へスライドアウト
         * (COMAN/MAIN.C:63-66)。 */
        if (mp > -30 && mp < 0) {
            if (startrise < 160)
                startrise++;
        }
        sin_frame++;
        if (sin_frame < 400 && startrise > 0)
            startrise--;
    }

    /* 地形の回転・前進 (rot/rot2/xwav/ywav)。原作 COMAN/MAIN.C:67-90 では
     * frepeat の外にあり、描画 1 回につき 1 歩だけ進む。描画は vsync
     * 待ちなので最大で表示の更新回数まで (VGA 70 回/秒、原作動画の PC では
     * 約 32 回/秒)。PAL ではまず最大 50 歩/秒で動かす (2026-11-15
     * ユーザー承認)。g_sinus_step_hz で歩数/秒を変えられる。描画 1 回で
     * 進めるのは原作どおり最大 1 歩なので、描画が遅ければそれ以下になる。
     * startrise と sin_frame (原作の frame += frepeat) は経過時間のまま。 */
    sinus_move_acc += (int)el * g_sinus_step_hz;
    int msteps = sinus_move_acc / 50;          /* 1 VBL = 1/50 s */
    if (msteps > 1)
        msteps = 1;
    sinus_move_acc -= msteps * 50;
    if (sinus_move_acc > 50)
        sinus_move_acc = 50;
    if (msteps) {
        rot2 += 4;
        rot += isin(rot2) / 15;
    }

    int r = rot >> 3;
    int rsin = isin(r);
    int rcos = isin(r + 256);
    int rsin2 = isin(r + 177);
    int rcos2 = isin(r + 177 + 256);

    int xw = xwav;
    int yw = ywav;

    /* COMAN/ASM.ASM docopy:@@cl1 は毎フレーム vbuf の 68..97 行 (30*160 バイト)
     * だけを 0 にする。ray march が書かない画素 (手前の山の陰など) は
     * 前のフレームの値が残る (以前は 50..199 行をすべて消していた)。
     * なお画面下に一瞬出る黒い切り欠きは原作にもある (原作動画
     * 454.45-454.55 s、画面幅の 65-80% の位置。2026-11-15 計測)。 */
    for (int i = 68 * 160; i < 98 * 160; i++)
        sin_vbuf[i] = 0;

    for (int a = 0; a < 160; a++) {
        int x = a - 80;
        int y = 160;
        int xa = (x * rcos + y * rsin) / 256;
        int ya = (y * rcos2 - x * rsin2) / 256;
        int b = (a & 1) * 80 + (a >> 1) + 199 * 160;
        docol(xw, yw, xa, ya, b);
        if (a == 80) {
            xwav += xa * 4 * msteps;
            ywav += ya * 4 * msteps;
        }
    }


    /* docopy(): source starts at vbuf row 60, destination at screen row
     * 70 + startrise, for (140 - startrise) lines.  Each source column is
     * two screen pixels wide; the two 80-column halves interleave. */
    int lines = 140 - startrise;
    if (lines < 0)
        lines = 0;
    if (lines > 140)
        lines = 140;

    /* 地形を写す行: 画面の y_top..S_YOFF+S_H-1 (窓の 70+startrise 行から) */
    int y_top = S_YOFF + 70 + startrise;
    if (y_top > S_YOFF + S_H)
        y_top = S_YOFF + S_H;
    if (!sn_cleared) {
        /* 開始待ちを通らなかったとき (chunky は main が 0 にしてある) */
        sn_cleared = 1;
        c2p_track_add(&sn_trk, 0, 256, 0, 320);
    }
    /* 前のフレームで地形があって今回は外れた行を 0 にする */
    for (int y = sn_y0; y < y_top; y++) {
        u8 *dst = chunky + y * 320;
        for (int x2 = 0; x2 < 320; x2++)
            dst[x2] = 0;
    }
    if (sn_y0 < y_top)
        c2p_track_add(&sn_trk, sn_y0, y_top, 0, 320);
    for (int y = y_top - S_YOFF; y < S_H; y++) {
        u8 *dst = chunky + (y + S_YOFF) * 320;
        int ty = y - 70 - startrise;          /* line within the copy */
        const u8 *src = sin_vbuf + (60 + ty) * 160;
        for (int z = 0; z < 80; z++) {
            u8 v0 = src[z];
            u8 v1 = src[z + 80];
            dst[4 * z + 0] = v0;
            dst[4 * z + 1] = v0;
            dst[4 * z + 2] = v1;
            dst[4 * z + 3] = v1;
        }
    }
    c2p_track_add(&sn_trk, y_top, S_YOFF + S_H, 0, 320);
    sn_y0 = y_top;

    c2p_track_flush(&sn_trk, chunky, planes);
    return 1;
}
