/*
 * TECHNO (KOE.C / KOEA.ASM / KOEB.ASM) — 第 1 段: 干渉円。
 *
 * KOE.C の順序:
 *   dointerference2()  -> KOEB.ASM の 256 フレーム。
 *                         パターン描画ループは jmp @@OVER3 で飛ばし、
 *                         VGA ページ 0 の静的パターンに pal0 (暗い緑) の
 *                         フェード/回転だけを掛ける。
 *   initinterference() -> KOEA.ASM。ページを作り直す。
 *   dointerference()   -> KOEA.ASM。power0 の正弦波で走査線ごとに
 *                         横シフトし、pal1/pal2 (紫/灰) でスクロールする。
 *
 * したがって最初は「スクロールしない暗い緑の波紋」、その後「紫の
 * スクロール干渉円」になる。ここでは両方を実装する。
 */
#include "scenes.h"
#include "../framework/video.h"
#include "../framework/c2p.h"
#include "../framework/mem.h"
#include <exec/memory.h>
#include "../framework/dis.h"
#include "../framework/vblank.h"
#include "../music/music.h"
#include <string.h>

#define TW 320
#define GY 28
#define PAGE_W 640
#define PAGE_H 400

extern const u8 *g_techno_circle0;     /* 8000 = 1 plane (_circle2) */
extern const u8 *g_techno_circle123;   /* 24000 = 3 planes (_circle) */
extern const u8 *g_techno_sin;         /* 1024 x s16 BE */
extern const u8 *g_techno_troll;       /* 320x400 x 8bit */
extern const u8 *g_techno_troll_pal;   /* 256 x RGB (8bit) */

static u8  base0[PAGE_W * PAGE_H];     /* KOEA: _circle plane 0 */
static u8  base1[PAGE_W * PAGE_H];     /* KOEA: _circle plane 1 */
static u8  base2[PAGE_W * PAGE_H];     /* KOEA: _circle plane 2 */
static u8  base3[PAGE_W * PAGE_H];     /* KOEA: _circle2 (第 2 の円) */
static u8  static_page[PAGE_W * PAGE_H];/* KOEB: _circle (3 planes) 静的画像 */
static s16 sin1024[1024];              /* SIN1024.INC (s16) */
static s8  power0[16 * 256];
static u8  gpal[256 * 3];

/* pal0 (KOEB 39-55)。暗い緑。index 0 と 8 のみ (0,30,40)。 */
static const u8 pal0[48] = {
    0, 30, 40,  0, 0, 0,  0, 0, 0,  0, 0, 0,
    0, 0, 0,    0, 0, 0,  0, 0, 0,  0, 0, 0,
    0, 30, 40,  0, 0, 0,  0, 0, 0,  0, 0, 0,
    0, 0, 0,    0, 0, 0,  0, 0, 0,  0, 0, 0,
};

/* KOEA pal1 (KOEA.ASM 460-476): 色 0..7 = _circle (plane 0..2)。
 * 8/9 の明るいグレー系。16 エントリを 8 個の窓で回す。 */
static const u8 pal1[48] = {
    30, 30 * 8 / 9, 30,
    60, 60 * 8 / 9, 60,
    50, 50 * 8 / 9, 50,
    40, 40 * 8 / 9, 40,
    30, 30 * 8 / 9, 30,
    20, 20 * 8 / 9, 20,
    10, 10 * 8 / 9, 10,
    0, 0, 0,
    30, 30 * 8 / 9, 30,
    60, 60 * 8 / 9, 60,
    50, 50 * 8 / 9, 50,
    40, 40 * 8 / 9, 40,
    30, 30 * 8 / 9, 30,
    20, 20 * 8 / 9, 20,
    10, 10 * 8 / 9, 10,
    0, 0, 0,
};
/* KOEA pal2 (KOEA.ASM 442-458): 色 8..15 = _circle2 (plane 3)。
 * 7/9 の紫系。 */
static const u8 pal2[48] = {
    0, 0, 0,
    10, 10 * 7 / 9, 10,
    20, 20 * 7 / 9, 20,
    30, 30 * 7 / 9, 30,
    40, 40 * 7 / 9, 40,
    50, 50 * 7 / 9, 50,
    60, 60 * 7 / 9, 60,
    30, 30 * 7 / 9, 30,
    0, 0, 0,
    10, 10 * 7 / 9, 10,
    20, 20 * 7 / 9, 20,
    30, 30 * 7 / 9, 30,
    40, 40 * 7 / 9, 40,
    50, 50 * 7 / 9, 50,
    60, 60 * 7 / 9, 60,
    30, 30 * 7 / 9, 30,
};

static int tl_frame;                   /* 0..255 = KOEB, 以降 KOEA */
static int pa_started;                 /* KOE.C:213 の待ちを抜けた */
/* (旧) TUNNELI → TECHNO の読み込み時間。2026-11-15 に、待ち時間ではなく
 * KOE.C:213 の待ちを抜ける音楽位置 (17:11) を直接待つ方式に変更した
 * (techno_render 参照)。 */
#define TECHNO_LOAD_VBL 0   /* 使わない (音楽位置 17:11 を待つ) */
static u32 techno_init_vbl;
static int techno_prepared;
static int pb_done;                    /* mframe >= 925 */
static int palanimc, patdir;
static int sinurot, scrnrot, overrot, sinuspower, framecount;
static int powercnt;
static int scrnx, scrny, overx, overy, scrnposl;
static u32 pb_last_vbl;                /* 実際の VBL 数から 70Hz 相当に進める */
static int pb_acc;                     /* 1/50 フレーム単位の端数 */

/* ---- シーン進行 (KOE.C の順序) ---- */
#define PH_A        0                   /* KOEB dointerference2 (静的) */
#define PH_B        1                   /* KOEA do_interference (干渉円) */
#define PH_TRANS    2                   /* flash + 白画面 + ワイプ */
#define PH_DOIT     3                   /* doit1/2/3 */
#define PH_TROLL    4                   /* TROLL.UP */
#define PH_END      5

/* 干渉円の終了: KOEA.ASM:887-893 で dis_getmframe() >= 925 になったら抜ける。
 * mframe は KOE.C:213-214 の「dis_musplus() >= -4 まで待ってから
 * dis_setmframe(0)」から数えた音楽 tick 数 (プレイヤーの np_zframe)。
 * 以前は 70*13 フレームの固定時間で、PH_A も描画回数で数えていたため、
 * 描画が遅いと切り替えが音楽より遅れていた (2026-11-14 ユーザー指摘)。 */
#define PB_END_MFRAME 925

static int scene_phase = PH_A;

/* 音楽位置 (std order:row)。原作 dis_musplus()/muscode_6 の CX:BX。 */
static int mus_pos(void) {
    return ((int)np_ord << 8) | (np_row & 0xff);
}

/* ---- KOEA doit1/2/3: Mode X の 4 プレーン x 8 ページ ---- */
#define VPAGES      8
#define VPLANES     4
#define VPSTRIDE    16384               /* doit3 は 80 バイト x 200 行 = 16000 */
#define VROW        40

static u8 vplane[VPLANES][VPAGES * VPSTRIDE];
static u8 vbuf[200 * VROW];            /* 1bpp の輪郭 (XOR 描画) */
static u8 trscreen[320 * 200];         /* 遷移中の 16 色画面 */
static u8 last_b_img[320 * 200];       /* 干渉円の最終フレーム */

/* 白フラッシュ・白画面・縦ワイプ (PH_TRANS) の画面転送を、変わった所だけに
 * する (2026-11-15 ユーザー報告: この場面がとても重い)。以前は毎フレーム
 * 320x200 を chunky へ写し、200 行を c2p し、8 プレーン x 256 行をチップ
 * RAM へ写していた。画面が変わるのは
 *   - 干渉円の最終フレームを出すとき・全面を白 (15) にするとき (全体)
 *   - 縦ワイプで 80 ドット幅の帯を 0 にした行 (KOE.C:289-292)
 * だけなので、その矩形だけを chunky へ写して変換し、変換した行だけを
 * チップ RAM へ写す。裏画面ごとに前回からの変化を c2p_track で持つ。 */
static c2p_track tr_trk;
static int tr_cnt;
static s16 tr_rect[8][4];               /* このフレームで変わった矩形 (窓の座標) */

static void tr_changed(int y0, int y1, int x0, int x1) {
    if (tr_cnt < 8) {
        tr_rect[tr_cnt][0] = (s16)y0; tr_rect[tr_cnt][1] = (s16)y1;
        tr_rect[tr_cnt][2] = (s16)x0; tr_rect[tr_cnt][3] = (s16)x1;
        tr_cnt++;
    } else {                            /* あふれたら窓全体 */
        tr_rect[7][0] = 0; tr_rect[7][1] = 200;
        tr_rect[7][2] = 0; tr_rect[7][3] = 320;
    }
}
static u8 phase_a_cache[4][200 * VROW];
static u8 *phase_a_screen[VIDEO_NBUF];
static u8 phase_a_written[VIDEO_NBUF];
static u8 pal16[16][48];               /* KOE.C の pal[16][16][3] (6bit) */
static u8 prefix8[256];                /* 1 バイトの prefix XOR */
static u8 parity8[256];
static u8 pop8[256];
static int curpal, last_musrow;

static int doit_phase;                 /* 0=終了, 1..3 */
static int doit_count;
static int doit_rot, doit_rota, doit_rot2;
static int doit_vm, doit_vma;
static int doit_pl, doit_plv;
static int doit_xpos, doit_xposa;
static int doit_disp_page, doit_disp_stride, doit_disp_xpos;

static int tr_state, tr_timer, tr_rep, tr_zly, tr_zy, tr_zya;
static int tr_hold_ready;
/* 音楽待ち: 1 = KOE.C:275 (musplus >= -3) のあと 277-281 (row&7==7),
 *           2 = KOE.C:303-307 (row&7==7) */
static int tr_wait;
static u8 tr_base[48];                 /* flash の基準パレット (6bit) */

/* ---- TROLL.UP (KOE.C 588-727) ---- */
static int troll_frame;
static int troll_pan;
static int troll_xpos, troll_xposa;    /* KOE.C:649-673 の加速パン */
static int troll_pan_done;
static int troll_ripple_frames;
static int troll_ripple, troll_ripplep;
static int troll_band_top, troll_band_h, troll_band_w;
static int troll_dim;                  /* 消灯時の減光 (256=そのまま) */
static int troll_tv;                   /* 1=消灯中 (画像の 0 番を白へ) */
static int troll_shake_x, troll_shake_y;/* インパクトの画面揺れ */
static int troll_flash;                /* インパクトの白飛び */
static int troll_go;                   /* KOE.C:637-648 の音楽待ちを抜けた */

/* KOEB init_interference と同じ: _circle (3 planes) を 640x400 に。
 * 通常 320 + 鏡像 320、上 200 行 + 下 200 行 (上下反転)。 */
static void build_static_page(void) {
    int x, y, p;
    for (y = 0; y < 200; y++) {
        for (x = 0; x < TW; x++) {
            int idx = 0;
            int off = y * 120;          /* 3 plane x 40 */
            int byte = x >> 3;
            int bit = 0x80 >> (x & 7);
            for (p = 0; p < 3; p++)
                if (g_techno_circle123[off + p * 40 + byte] & bit)
                    idx |= 1 << p;
            static_page[y * PAGE_W + x] = (u8)idx;
            static_page[y * PAGE_W + (PAGE_W - 1 - x)] = (u8)idx;
            static_page[(PAGE_H - 1 - y) * PAGE_W + x] = (u8)idx;
            static_page[(PAGE_H - 1 - y) * PAGE_W + (PAGE_W - 1 - x)] = (u8)idx;
        }
    }
}

/* KOEA init_interference の circles[] を 3 プレーン分作る。
 * 実データは _circle (3 planes)。1 ソース行 = 3 plane x 40 バイト。 */
static void build_scroll_page(void) {
    u8 *bp[3] = { base0, base1, base2 };
    int x, y, p;
    for (y = 0; y < 200; y++) {
        for (x = 0; x < TW; x++) {
            int byte = x >> 3;
            int bit = 0x80 >> (x & 7);
            int off = y * 120;
            for (p = 0; p < 3; p++) {
                int v = (g_techno_circle123[off + p * 40 + byte] & bit) ? 1 : 0;
                bp[p][y * PAGE_W + x] = (u8)v;
                bp[p][y * PAGE_W + (PAGE_W - 1 - x)] = (u8)v;
                bp[p][(PAGE_H - 1 - y) * PAGE_W + x] = (u8)v;
                bp[p][(PAGE_H - 1 - y) * PAGE_W + (PAGE_W - 1 - x)] = (u8)v;
            }
            {
                int v = (g_techno_circle0[y * 40 + byte] & bit) ? 1 : 0;
                base3[y * PAGE_W + x] = (u8)v;
                base3[y * PAGE_W + (PAGE_W - 1 - x)] = (u8)v;
                base3[(PAGE_H - 1 - y) * PAGE_W + x] = (u8)v;
                base3[(PAGE_H - 1 - y) * PAGE_W + (PAGE_W - 1 - x)] = (u8)v;
            }
        }
    }
}

static void build_doit_tables(void);
static void build_packed(void);
static void build_phase_a_cache(void);

void techno_prepare(void) {
    int i, b, c;

    if (techno_prepared)
        return;

    for (i = 0; i < 1024; i++)
        sin1024[i] = (s16)((g_techno_sin[i * 2] << 8) | g_techno_sin[i * 2 + 1]);

    /* KOE.C 173-211: power0 */
    for (b = 0; b < 16; b++) {
        for (c = 0; c < 256; c++) {
            int ch;
            if (b == 15)
                ch = c;
            else {
                int a = (c > 127) ? c - 256 : c;
                ch = a * b / 15;
            }
            power0[b * 256 + c] = (s8)ch;
        }
    }

    build_static_page();
    build_scroll_page();
    build_packed();
    build_phase_a_cache();
    build_doit_tables();
    techno_prepared = 1;
}

void techno_init(void) {
    techno_prepare();

    tl_frame = 0;
    pa_started = 0;             /* KOE.C:213-214 の待ちは techno_render で */
    techno_init_vbl = vblank_frames();
    pb_done = 0;
    pb_last_vbl = vblank_frames();
    scene_phase = PH_A;
    palanimc = 0;
    patdir = -3;                /* KOEB patdir dw -3 */
    sinurot = 0;
    scrnrot = overrot = 0;
    scrnx = scrny = overx = overy = scrnposl = 0;
    sinuspower = 0;
    framecount = 0;
    powercnt = 0;
    pb_acc = 0;
    curpal = 0;
    last_musrow = -1;
    doit_phase = 0;
    tr_state = 0;
    for (int i = 0; i < VIDEO_NBUF; i++) {
        phase_a_screen[i] = 0;
        phase_a_written[i] = 0;
    }
}

/* KOEB mixpal: dx<=256 はフェードイン、dx>256 は明るさ加算。 */
static int mixpal1(int s, int dx) {
    if (dx <= 256)
        return (s * dx) >> 8;
    s += dx - 256;
    return (s > 63) ? 63 : s;
}

/* ---- 1 ビット/ドットの詰めたページ (実機 68060 の速度対策、2026-11-15) ----
 * 原作の VGA は 4 プレーンをそのまま表示するので、干渉円 (KOEB/KOEA) は
 * 「プレーンごとにずらしたコピー」で済む。以前は 1 バイト/ドットの配列から
 * 1 ドットずつ chunky を組み立てて c2p していた。ここでは 640x400 のページを
 * 1 ビット/ドットに詰めておき、表示する 320 ドットを 32 ビット単位で切り出して
 * 4 プレーンへ直接書く。ビット位置は元の配列の添字そのまま (行をまたいで
 * 連続) なので、負のずれ (overx + d - 7) も同じ結果になる。 */
#define PK_BYTES (PAGE_W * PAGE_H / 8)
static u8 pk_mem[7][PK_BYTES + 16];
static const u8 *pkS[3], *pkB[4];

static void pack_page(u8 *dst, const u8 *src, int bitsel) {
    for (int i = 0; i < PK_BYTES; i++) {
        u8 v = 0;
        for (int b = 0; b < 8; b++)
            if ((src[i * 8 + b] >> bitsel) & 1)
                v |= (u8)(0x80 >> b);
        dst[i] = v;
    }
}

static void build_packed(void) {
    for (int k = 0; k < 7; k++)
        for (int i = 0; i < PK_BYTES + 16; i++)
            pk_mem[k][i] = 0;
    for (int p = 0; p < 3; p++) {
        pack_page(pk_mem[p] + 8, static_page, p);
        pkS[p] = pk_mem[p] + 8;
    }
    pack_page(pk_mem[3] + 8, base0, 0); pkB[0] = pk_mem[3] + 8;
    pack_page(pk_mem[4] + 8, base1, 0); pkB[1] = pk_mem[4] + 8;
    pack_page(pk_mem[5] + 8, base2, 0); pkB[2] = pk_mem[5] + 8;
    pack_page(pk_mem[6] + 8, base3, 0); pkB[3] = pk_mem[6] + 8;
}

/* bit (元の配列の添字) から 32 ドット分のビットを取り出す (MSB = 左)。 */
static inline u32 get_bits32(const u8 *pk, s32 bit) {
    const u8 *q = pk + (bit >> 3);
    int sh = bit & 7;
    u32 v = ((u32)q[0] << 24) | ((u32)q[1] << 16) | ((u32)q[2] << 8) | q[3];
    if (sh)
        v = (v << sh) | ((u32)q[4] >> (8 - sh));
    return v;
}

static void put_row(u8 *planes, int plane, int y, const u8 *pk, s32 bit) {
    u32 *d = (u32 *)(planes + plane * (40 * 256) + (GY + y) * 40);
    for (int i = 0; i < 10; i++)
        d[i] = pk ? get_bits32(pk, bit + i * 32) : 0;
}

static void build_phase_a_cache(void) {
    for (int y = 0; y < 200; y++) {
        s32 bit = (100 + y) * PAGE_W + 160;
        for (int p = 0; p < 3; p++) {
            u32 *d = (u32 *)(phase_a_cache[p] + y * VROW);
            for (int i = 0; i < 10; i++)
                d[i] = get_bits32(pkS[p], bit + i * 32);
        }
        memset(phase_a_cache[3] + y * VROW, 0, VROW);
    }
}

/* KOEB フェーズ: 256 フレーム。静的 _circle パターン (VGA ページ 0) を
 * scrnx=160 / scrny=100 の窓で表示し、pal0 をフェード/回転させる。 */
static void render_phase_a(u8 *chunky) {
    int x, y, i, k;
    int fader = (tl_frame + 1) * 2;
    if (fader > 512) fader = 512;

    for (i = 0; i < 8; i++) {
        for (k = 0; k < 3; k++) {
            int s = pal0[(palanimc + i * 3) % 48 + k];
            int v = mixpal1(s, fader);
            gpal[(0 + i) * 3 + k] = (u8)((v << 2) | (v >> 4));
            gpal[(8 + i) * 3 + k] = (u8)((v << 2) | (v >> 4));
        }
    }
    for (i = 16 * 3; i < 256 * 3; i++)
        gpal[i] = 0;

    /* 上下の余白 */
    for (i = 0; i < GY * TW; i++)
        chunky[i] = 0;
    for (i = (GY + 200) * TW; i < 256 * TW; i++)
        chunky[i] = 0;

    /* KOEB do_interference の CRTC 開始位置 (sizefade=0 で固定):
     * scrnx=160, scrny=100。 */
    for (y = 0; y < 200; y++) {
        const u8 *src = static_page + (100 + y) * PAGE_W + 160;
        u8 *dst = chunky + (y + GY) * TW;
        for (x = 0; x < TW; x++)
            dst[x] = src[x];
    }

    video_set_palette(gpal);
}

/* KOEA do_interference の初期状態。KOEA と KOEB は別モジュールなので、
 * KOEA 側の scrnx/scrny/... は 0 から始まる。overrot だけは 211。 */
static void phase_b_init(void) {
    palanimc = 0;
    /* 固定シーンでは音楽行が 0/4 に来ないことがあるため、最初から
     * 回す (KOEA の最初の row 0/4 と同じ)。 */
    patdir = -3;
    sinurot = 0;
    scrnrot = 0;
    overrot = 211;              /* KOEA.ASM 639: overrot dw 211 */
    scrnx = scrny = overx = overy = scrnposl = 0;
    sinuspower = 0;
    framecount = 0;
    powercnt = 0;
    pb_acc = 0;
    pb_last_vbl = vblank_frames();
}

/* KOEA の 1 フレーム分の状態更新。オリジナルは 70Hz なので、
 * 50fps 描画では 1.4 回/描画だけ進める (pb_acc)。
 *
 * ASM の順序: パレット -> sinurot+14 -> 描画 -> スクロール更新
 * -> framecount/powercnt なので、ここでは描画前の更新だけを行う。 */
static void pb_step(void) {
    /* パレット回転 (KOEA 757-777) */
    int row = dis_musrow() & 7;
    if (row == 0 || row == 4)
        patdir = -3;
    palanimc += patdir;
    if (palanimc < 0)
        palanimc = 8 * 3 - 3;
    if (palanimc >= 8 * 3)
        palanimc = 0;

    /* 走査線の位相 (KOEA 784-787) */
    sinurot = (sinurot + 7 * 2) & 2047;

    /* スクロール / オーバーレイ (KOEA 832-876) */
    scrnrot = (scrnrot + 5) & 1023;
    scrnx = (sin1024[scrnrot] >> 2) + 160;
    scrny = (sin1024[(scrnrot + 256) & 1023] >> 2) + 100;
    overrot = (overrot + 7) & 1023;
    overx = (sin1024[overrot] >> 2) + 160;
    overy = (sin1024[(overrot + 256) & 1023] >> 2) + 100;
    scrnposl = scrnx & 7;

    /* sinuspower (KOEA 887-897) */
    framecount++;
    if (framecount >= 70 * 5) {
        powercnt++;
        if (powercnt >= 16) {
            powercnt = 0;
            if (sinuspower < 15)
                sinuspower++;
        }
    }
}

/* KOEA do_interference の描画。
 *
 * プレーン 0..2 = _circle  (静的ページを CRTC 窓 scrnpos で表示)
 * プレーン 3   = _circle2 (走査線ごとに ax だけ横シフトして書き込む)
 *
 * ASM の 8 ページ回転 (rotate1) と pixel pan (scrnposl) は打ち消し合い、
 * プレーン 3 の読み出し位置は
 *   overx + power0[sinuspower][(sin1024[bp/2]>>3)&0xff] - 7
 * になる。bp は走査線ごとに +18 の **バイト**オフセット (表は 1,024 語)。 */
/* 実際に経過した VBL 数から 70Hz 相当のステップ数を返す。 */
static int timeline_steps(void) {
    u32 now = video_frame_vbl();
    u32 elapsed = now - pb_last_vbl;
    int steps = 0;
    pb_last_vbl = now;
    if (elapsed > 8)
        elapsed = 8;                /* 極端な遅延時の上限 */
    pb_acc += (int)elapsed * 70;
    while (pb_acc >= 50) {
        pb_acc -= 50;
        steps++;
    }
    return steps;
}

static void render_phase_b(u8 *chunky) {
    int x, y, i;

    for (i = 0; i < GY * TW; i++)
        chunky[i] = 0;
    for (i = (GY + 200) * TW; i < 256 * TW; i++)
        chunky[i] = 0;

    for (y = 0; y < 200; y++) {
        int bp = (sinurot + 9 * 2 * (y + 1)) & 2047;
        int d = (s8)power0[(sinuspower << 8) |
                           ((sin1024[bp >> 1] >> 3) & 0xff)];
        int pidx = (scrny + y) * PAGE_W + scrnx;
        int sidx = (overy + y) * PAGE_W + overx + d - 7;
        u8 *dst = chunky + (y + GY) * TW;

        for (x = 0; x < TW; x++) {
            int pink = (int)base0[pidx + x] |
                       ((int)base1[pidx + x] << 1) |
                       ((int)base2[pidx + x] << 2);
            dst[x] = (u8)(pink | ((int)base3[sidx + x] << 3));
        }
    }

    /* パレット: pal1 を色 0..7、pal2 を色 8..15 に 8 個ずつ。
     * palanimc は 3 バイト単位 (エントリ) で 0..21 を回る。 */
    for (i = 0; i < 8; i++) {
        int o = palanimc + i * 3;
        int k;
        for (k = 0; k < 3; k++) {
            int v1 = pal1[o + k], v2 = pal2[o + k];
            gpal[(0 + i) * 3 + k] = (u8)((v1 << 2) | (v1 >> 4));
            gpal[(8 + i) * 3 + k] = (u8)((v2 << 2) | (v2 >> 4));
        }
    }
    for (i = 16 * 3; i < 256 * 3; i++)
        gpal[i] = 0;
    video_set_palette(gpal);
}

static void render_phase_a_planes(u8 *planes) {
    int i, k, slot = -1;
    u8 *screen = video_back_planes();
    int fader = (tl_frame + 1) * 2;
    if (fader > 512) fader = 512;
    for (i = 0; i < 8; i++) {
        for (k = 0; k < 3; k++) {
            int s = pal0[(palanimc + i * 3) % 48 + k];
            int v = mixpal1(s, fader);
            gpal[(0 + i) * 3 + k] = (u8)((v << 2) | (v >> 4));
            gpal[(8 + i) * 3 + k] = (u8)((v << 2) | (v >> 4));
        }
    }
    for (i = 16 * 3; i < 256 * 3; i++)
        gpal[i] = 0;
    for (i = 0; i < VIDEO_NBUF; i++) {
        if (phase_a_screen[i] == screen) {
            slot = i;
            break;
        }
        if (!phase_a_screen[i]) {
            phase_a_screen[i] = screen;
            slot = i;
            break;
        }
    }
    if (slot >= 0 && phase_a_screen[slot] == screen) {
        if (!phase_a_written[slot]) {
            for (i = 0; i < 4; i++)
                memcpy(planes + i * (40 * 256) + GY * 40, phase_a_cache[i],
                       sizeof(phase_a_cache[i]));
            phase_a_written[slot] = 1;
            video_set_commit_area(4, GY, 200);
        } else {
            video_set_commit_area(0, 0, 0);
        }
    } else {
        for (i = 0; i < 4; i++)
            memcpy(planes + i * (40 * 256) + GY * 40, phase_a_cache[i],
                   sizeof(phase_a_cache[i]));
        video_set_commit_area(4, GY, 200);
    }
    video_set_palette(gpal);
}

static void render_phase_b_planes(u8 *planes) {
    int x, y, i;
    (void)x;
    for (y = 0; y < 200; y++) {
        int bp = (sinurot + 9 * 2 * (y + 1)) & 2047;
        int d = (s8)power0[(sinuspower << 8) |
                           ((sin1024[bp >> 1] >> 3) & 0xff)];
        s32 pidx = (s32)(scrny + y) * PAGE_W + scrnx;
        s32 sidx = (s32)(overy + y) * PAGE_W + overx + d - 7;
        put_row(planes, 0, y, pkB[0], pidx);
        put_row(planes, 1, y, pkB[1], pidx);
        put_row(planes, 2, y, pkB[2], pidx);
        put_row(planes, 3, y, pkB[3], sidx);
    }
    for (i = 0; i < 8; i++) {
        int o = palanimc + i * 3;
        int k;
        for (k = 0; k < 3; k++) {
            int v1 = pal1[o + k], v2 = pal2[o + k];
            gpal[(0 + i) * 3 + k] = (u8)((v1 << 2) | (v1 >> 4));
            gpal[(8 + i) * 3 + k] = (u8)((v2 << 2) | (v2 >> 4));
        }
    }
    for (i = 16 * 3; i < 256 * 3; i++)
        gpal[i] = 0;
    video_set_commit_area(4, GY, 200);
    video_set_palette(gpal);
}

/* ================= KOEA doit1/2/3 (ページフリップ) ================= */

/* KOE.C 119-168 の pal[16][16][3] (6bit) と blit16 の prefix XOR 表。 */
static void build_doit_tables(void) {
    static const u8 base_tbl[5][3] = {
        {0, 0, 0},
        {38 * 64 / 111, 33 * 64 / 111, 44 * 64 / 111},
        {52 * 64 / 111, 45 * 64 / 111, 58 * 64 / 111},
        {67 * 64 / 111, 61 * 64 / 111, 73 * 64 / 111},
        {83 * 64 / 111, 77 * 64 / 111, 89 * 64 / 111},
    };
    int a, b, c, k;

    for (a = 0; a < 256; a++) {
        int v = 0, out = 0;
        for (b = 7; b >= 0; b--) {
            v ^= (a >> b) & 1;
            out |= v << b;
        }
        prefix8[a] = (u8)out;
        parity8[a] = (u8)v;
        {
            int n = 0, t = a;
            while (t) {
                n += t & 1;
                t >>= 1;
            }
            pop8[a] = (u8)n;
        }
    }

    for (c = 0; c < 16; c++) {
        for (a = 0; a < 16; a++) {
            int x = (a & 1) + ((a >> 1) & 1) +
                    ((a >> 2) & 1) + ((a >> 3) & 1);
            int r = base_tbl[x][0];
            int g = base_tbl[x][1];
            int bl = base_tbl[x][2];
            r = r * (10 + c * 9 / 9) / 10;
            g = g * (10 + c * 7 / 9) / 10;
            bl = bl * (10 + c * 5 / 9) / 10;
            if (r > 63) r = 63;
            if (g > 63) g = 63;
            if (bl > 63) bl = 63;
            pal16[c][a * 3 + 0] = (u8)r;
            pal16[c][a * 3 + 1] = (u8)g;
            pal16[c][a * 3 + 2] = (u8)bl;
        }
    }
}

/* KOE.C waitborder(): 音楽行が 7 になるたび curpal=15 に戻し、
 * 1 フレームごとに 1 段暗くする。 */
static void waitborder_pal(void) {
    static const u8 level_mask[5] = {0, 1, 3, 7, 15};
    int a = dis_musrow();
    const u8 *p;
    int i, k;

    if (a != last_musrow) {
        last_musrow = a;
        if ((a & 7) == 7)
            curpal = 15;
    }
    p = pal16[curpal];
    /* KOE.C palette colors depend on the number of set bits, so these masks
     * select the exact original colors for encoded levels 0..4. */
    for (i = 0; i < 5; i++) {
        for (k = 0; k < 3; k++) {
            int v = p[level_mask[i] * 3 + k];
            gpal[i * 3 + k] = (u8)((v << 2) | (v >> 4));
        }
    }
    for (i = 5 * 3; i < 256 * 3; i++)
        gpal[i] = 0;
    if (curpal)
        curpal--;
}

static void xor_pixel(u8 *vb, int x, int y) {
    if ((unsigned)x < 320u && (unsigned)y < 200u)
        vb[y * VROW + (x >> 3)] ^= (u8)(0x80 >> (x & 7));
}

/* オリジナル drawline 相当: y 主軸で 1 走査線に 1 ピクセル。
 * 水平線 (dy==0) は描かない。標準 Bresenham だと 1 行に複数ピクセルが
 * 乗り、prefix XOR の塗りが崩れてしまう。 */
static void xor_line(u8 *vb, int x0, int y0, int x1, int y1) {
    int dy, dx, adx, sx, bx, x, y;
    if (y0 == y1)
        return;
    if (y0 > y1) {
        int t;
        t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    dy = y1 - y0;
    dx = x1 - x0;
    adx = (dx >= 0) ? dx : -dx;
    sx = (dx >= 0) ? 1 : -1;
    bx = -(dy >> 1);
    x = x0;
    for (y = y0; y <= y1; y++) {
        xor_pixel(vb, x, y);
        bx += adx;
        while (bx >= 0) {
            x += sx;
            bx -= dy;
        }
    }
}

/* POLYCLIP.ASM の clipmacro 相当。
 * v1 += (v2-v1)*(wl-w1)/(w2-w1), w1=wl。C の整数除算 (0 方向切り捨て)。 */
static void clip_interp(int *v1, int *w1, int v2, int w2, int wl) {
    int den = w2 - *w1;
    if (den != 0)
        *v1 += (v2 - *v1) * (wl - *w1) / den;
    *w1 = wl;
}

/* 1 本の辺を y (0..199) でクリップ。完全に外なら 0。 */
static int clip_seg_y(int *x1, int *y1, int *x2, int *y2) {
    if ((*y1 < 0 && *y2 < 0) || (*y1 > 199 && *y2 > 199))
        return 0;
    if (*y1 < 0)
        clip_interp(x1, y1, *x2, *y2, 0);
    else if (*y1 > 199)
        clip_interp(x1, y1, *x2, *y2, 199);
    if (*y2 < 0)
        clip_interp(x2, y2, *x1, *y1, 0);
    else if (*y2 > 199)
        clip_interp(x2, y2, *x1, *y1, 199);
    return 1;
}

/* 1 本の辺を x (0..319) でクリップ。完全に外なら 0。 */
static int clip_seg_x(int *x1, int *y1, int *x2, int *y2) {
    if ((*x1 < 0 && *x2 < 0) || (*x1 > 319 && *x2 > 319))
        return 0;
    if (*x1 < 0)
        clip_interp(y1, x1, *y2, *x2, 0);
    else if (*x1 > 319)
        clip_interp(y1, x1, *y2, *x2, 319);
    if (*x2 < 0)
        clip_interp(y2, x2, *y1, *x1, 0);
    else if (*x2 > 319)
        clip_interp(y2, x2, *y1, *x1, 319);
    return 1;
}

/* 多角形の各辺をクリップして頂点列を作り直す (POLYCLIP.ASM 相当)。 */
static int clip_poly(int which, const int *ix, const int *iy, int n,
                     int *ox, int *oy) {
    int m = 0, i;
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        int ax = ix[i], ay = iy[i], bx = ix[j], by = iy[j];
        int ok = (which == 0) ? clip_seg_y(&ax, &ay, &bx, &by)
                              : clip_seg_x(&ax, &ay, &bx, &by);
        if (!ok) continue;
        if (m > 20) break;
        if (m == 0 || ox[m - 1] != ax || oy[m - 1] != ay) {
            ox[m] = ax; oy[m] = ay; m++;
        }
        if (ox[m - 1] != bx || oy[m - 1] != by) {
            ox[m] = bx; oy[m] = by; m++;
        }
    }
    if (m > 1 && ox[0] == ox[m - 1] && oy[0] == oy[m - 1])
        m--;
    return m;
}

/* asmbox 相当: 画面 (0..319, 0..199) にクリップしてから辺を XOR。
 * クリップでできる境界エッジが prefix XOR の行頭パリティを決める。 */
static void xor_quad(u8 *vb, int x1, int y1, int x2, int y2,
                     int x3, int y3, int x4, int y4) {
    /* POLYCLIP.ASM:3-6 uses inclusive bounds x=0..319, y=0..199.
     * When every vertex is inside, clipping leaves the polygon unchanged. */
    if ((unsigned)x1 < 320u && (unsigned)y1 < 200u &&
        (unsigned)x2 < 320u && (unsigned)y2 < 200u &&
        (unsigned)x3 < 320u && (unsigned)y3 < 200u &&
        (unsigned)x4 < 320u && (unsigned)y4 < 200u) {
        xor_line(vb, x1, y1, x2, y2);
        xor_line(vb, x2, y2, x3, y3);
        xor_line(vb, x3, y3, x4, y4);
        xor_line(vb, x4, y4, x1, y1);
        return;
    }

    /* clip_poly は 1 辺につき最大 2 頂点を積むので、8 角形を x で切ると
     * 最大 16 個になる。12 だと配列を溢れて座標が壊れ、xor_line が
     * 異常な長さの線を引いて固まる。 */
    int ax[24], ay[24], bx[24], by[24];
    int n, i;

    ax[0] = x1; ay[0] = y1;
    ax[1] = x2; ay[1] = y2;
    ax[2] = x3; ay[2] = y3;
    ax[3] = x4; ay[3] = y4;

    n = clip_poly(0, ax, ay, 4, bx, by);   /* y でクリップ */
    if (n < 2) return;
    n = clip_poly(1, bx, by, n, ax, ay);   /* x でクリップ */
    if (n < 2) return;

    for (i = 0; i < n; i++)
        xor_line(vb, ax[i], ay[i], ax[(i + 1) % n], ay[(i + 1) % n]);
}

/* blit16: 輪郭バッファを prefix XOR で塗りつぶし、1 プレーンへ書く。
 * rowstride = 40 (doit1/2) / 80 (doit3)、off = 行内の書き込み開始バイト。
 *
 * 画面端でクリップされた平行四辺形は「1 プレーンが 1 行まるごと点灯」する
 * ことがあり、これが 1〜2px の横線として模様を貫く。オリジナルの VGA 出力
 * にも存在するが、参照映像では縦ブラーで潰れている。Amiga のくっきりした
 * 表示では目立つため、孤立した 1〜2 行の全幅ラインだけを落とす。 */

static void doit_draw(int wx, int wy, int vm, int rot, int rowstride, int off,
                      int vmdiv64) {
    int c, y, bx;
    int hx = sin1024[(rot + 0) & 1023] * 16 * 6 / 5;
    int hy = sin1024[(rot + 256) & 1023] * 16;
    int vx = sin1024[(rot + 256) & 1023] * 6 / 5;
    int vy = sin1024[(rot + 512) & 1023];
    int plidx = (doit_pl == 1) ? 0 : (doit_pl == 2) ? 1 :
                (doit_pl == 4) ? 2 : 3;
    u8 *plane = vplane[plidx] + doit_plv * VPSTRIDE;

    if (vmdiv64) {
        vx = vx * (vm / 64) / 100;
        vy = vy * (vm / 64) / 100;
    } else {
        vx = vx * vm / 100;
        vy = vy * vm / 100;
    }

    memset(vbuf, 0, sizeof(vbuf));

    for (c = -10; c < 11; c += 2) {
        int cx = vx * c * 2;
        int cy = vy * c * 2;
        int x1 = (-hx - vx + cx) / 16 + wx;
        int y1 = (-hy - vy + cy) / 16 + wy;
        int x2 = (-hx + vx + cx) / 16 + wx;
        int y2 = (-hy + vy + cy) / 16 + wy;
        int x3 = (+hx + vx + cx) / 16 + wx;
        int y3 = (+hy + vy + cy) / 16 + wy;
        int x4 = (+hx - vx + cx) / 16 + wx;
        int y4 = (+hy - vy + cy) / 16 + wy;
        xor_quad(vbuf, x1, y1, x2, y2, x3, y3, x4, y4);
    }

    /* prefix XOR で塗る (搬送は行内のみ、行頭は 0)。 */
    for (y = 0; y < 200; y++) {
        u8 carry = 0;
        u8 *dst = plane + y * rowstride + off;
        const u8 *src = vbuf + y * VROW;
        for (bx = 0; bx < VROW; bx++) {
            u8 in = src[bx];
            dst[bx] = prefix8[in] ^ (carry ? 0xff : 0);
            carry ^= parity8[in];
        }
    }
}

static void doit_advance_page(int step) {
    doit_plv += step;
    doit_plv &= 7;
    if (doit_plv == 0) {
        doit_pl <<= 1;
        if (doit_pl > 15)
            doit_pl = 1;
    }
}

static void transition_init(void) {
    int i;
    for (i = 0; i < 48; i++)
        tr_base[i] = (u8)(gpal[i] >> 2);
    tr_state = 0;
    tr_timer = 0;
    tr_rep = 0;
    tr_wait = 0;
    tr_zly = tr_zy = tr_zya = 0;
    tr_hold_ready = 0;
    c2p_track_reset(&tr_trk);
    tr_cnt = 0;
    tr_changed(0, 200, 0, 320);         /* 最初は干渉円の最終フレーム全体 */
}

static void set_flash_pal(const u8 *base, int i) {
    int n;
    if (i < 0) i = 0;
    if (i > 256) i = 256;
    for (n = 0; n < 48; n++) {
        int v = (base[n] * (256 - i) + 63 * i) >> 8;
        gpal[n] = (u8)((v << 2) | (v >> 4));
    }
    for (n = 16 * 3; n < 256 * 3; n++)
        gpal[n] = 0;
}

/* KOE.C 224-315: flash x4 -> 白画面 -> 4 回の縦ワイプ (各 10 バイト幅)。 */
static void transition_step(void) {
    static const int flash_i[4] = {32, 64, 192, 256};
    int a, y;

    if (tr_wait == 1) {
        if (dis_musplus() < -3)
            return;
        tr_wait = 2;
    }
    if (tr_wait == 2) {
        if ((dis_musrow() & 7) != 7)
            return;
        tr_wait = 0;
    }

    if (tr_state == 0) {
        set_flash_pal(tr_base, flash_i[tr_timer]);
        tr_timer++;
        if (tr_timer < 4)
            return;
        /* 全プレーン 255 -> 全画面 15。パレットをランプに。 */
        for (y = 0; y < 320 * 200; y++)
            trscreen[y] = 15;
        tr_changed(0, 200, 0, 320);
        for (a = 0; a < 16; a++) {
            tr_base[a * 3 + 0] = (u8)(a * 6 / 2);
            tr_base[a * 3 + 1] = (u8)(a * 7 / 2);
            tr_base[a * 3 + 2] = (u8)(a * 8 / 2);
        }
        tr_state = 1;
        tr_timer = 0;
        tr_zly = tr_zy = tr_zya = 0;
        tr_wait = 1;            /* KOE.C:275-281 */
        return;
    }

    if (tr_state == 1) {
        tr_zly = tr_zy;
        tr_zya++;
        tr_zy += tr_zya;
        for (y = tr_zly; y < tr_zy && y < 200; y++)
            memset(trscreen + y * 320 + tr_rep * 80, 0, 80);
        if (tr_zly < 200 && tr_zy > tr_zly)
            tr_changed(tr_zly, tr_zy < 200 ? tr_zy : 200, tr_rep * 80, tr_rep * 80 + 80);
        a = 256 - 32 * tr_timer;
        set_flash_pal(tr_base, a);
        tr_timer++;
        if (tr_timer >= 21) {
            tr_state = 2;
            tr_timer = 0;
            tr_wait = 2;        /* KOE.C:303-307 */
        }
        return;
    }

    if (tr_state == 2) {
        set_flash_pal(tr_base, flash_i[tr_timer]);
        tr_timer++;
        if (tr_timer < 4)
            return;
        tr_rep++;
        if (tr_rep >= 4) {
            tr_state = 3;
            return;
        }
        tr_state = 1;
        tr_timer = 0;
        tr_zly = tr_zy = tr_zya = 0;
    }
}

static void transition_render(u8 *chunky, u8 *planes) {
    const u8 *img = (tr_state == 0) ? last_b_img : trscreen;
    int y, k, ry0, ry1;
    static int tr_bands_clear;
    if (!tr_bands_clear || tr_trk.buf[0] == 0) {
        /* 上下の帯は最初に 1 回だけ 0 にする (以前は毎フレーム) */
        for (k = 0; k < GY * TW; k++)
            chunky[k] = 0;
        for (k = (GY + 200) * TW; k < 256 * TW; k++)
            chunky[k] = 0;
        tr_bands_clear = 1;
    }
    for (k = 0; k < tr_cnt; k++) {
        int y0 = tr_rect[k][0], y1 = tr_rect[k][1];
        int x0 = tr_rect[k][2], x1 = tr_rect[k][3];
        for (y = y0; y < y1; y++)
            memcpy(chunky + (y + GY) * TW + x0, img + y * 320 + x0, (unsigned)(x1 - x0));
        c2p_track_add(&tr_trk, y0 + GY, y1 + GY, x0, x1);
    }
    tr_cnt = 0;
    c2p_track_flush_rows4(&tr_trk, chunky, planes, &ry0, &ry1);
    if (ry1 > ry0)
        video_set_commit_area(4, ry0, ry1 - ry0);
    else
        video_set_commit_area(0, 0, 0);
    video_set_palette(gpal);
}

static void doit_init(void) {
    memset(vplane, 0, sizeof(vplane));
    /* 直接ビットプレーン描画に切り替えるため、両バッファをクリアして
     * 3bit出力の未使用プレーン / 上下の黒帯に前シーンの残りが無いようにする。 */
    video_clear_planes();
    doit_phase = 1;
    doit_count = 70 * 6;
    doit_rot = 45;
    doit_vm = 50;
    doit_vma = 0;
    doit_pl = 1;
    doit_plv = 0;
    doit_rot2 = 0;
    doit_rota = 2;
    doit_xpos = 320;
    doit_xposa = 0;
    /* 参照 189 秒は明るい段 (curpal=15) から始まる。 */
    curpal = 15;
    last_musrow = -1;
    doit_disp_page = 0;
    doit_disp_stride = 40;
    doit_disp_xpos = 0;
}

static void doit_step(void) {
    waitborder_pal();

    if (doit_phase == 1) {
        doit_draw(160, 100, doit_vm, doit_rot, 40, 0, 0);
        doit_disp_page = doit_plv;
        doit_disp_stride = 40;
            doit_disp_xpos = 0;

        doit_rot += 2;
        doit_vm += doit_vma;
        if (doit_vm < 25) {
            doit_vm -= doit_vma;
            doit_vma = -doit_vma;
        }
        doit_vma--;
        doit_advance_page(1);
        if (--doit_count <= 0) {
            doit_phase = 2;
            doit_count = 70 * 12;
            doit_rot = 50;
            doit_rota = 10;
            doit_vm = 100 * 64;
            doit_vma = 0;
            doit_pl = 1;
            doit_plv = 0;
        }
        return;
    }

    if (doit_phase == 2) {
        doit_draw(160, 100, doit_vm, doit_rot, 40, 0, 1);
        doit_disp_page = doit_plv;
        doit_disp_stride = 40;
            doit_disp_xpos = 0;

        doit_rot += doit_rota / 10;
        doit_vm += doit_vma;
        if (doit_vm < 0) {
            doit_vm -= doit_vma;
            doit_vma = -doit_vma;
        }
        doit_vma--;
        doit_rota++;
        doit_advance_page(1);
        if (--doit_count <= 0) {
            doit_phase = 3;
            doit_count = 70 * 14;
            doit_rot = 45;
            doit_rota = 10;
            doit_rot2 = 0;
            doit_vm = 100 * 64;
            doit_vma = 0;
            doit_pl = 1;
            doit_plv = 0;
            doit_xpos = 320;
            doit_xposa = 0;
            memset(vplane, 0, sizeof(vplane));
        }
        return;
    }

    if (doit_phase == 3) {
        int wx, wy;
        if (doit_rot2 < 32) {
            wx = sin1024[(doit_rot2 + 0) & 1023] * doit_rot2 / 8 + 160;
            wy = sin1024[(doit_rot2 + 256) & 1023] * doit_rot2 / 8 + 100;
        } else {
            wx = sin1024[(doit_rot2 + 0) & 1023] / 4 + 160;
            wy = sin1024[(doit_rot2 + 256) & 1023] / 4 + 100;
        }
        doit_rot2 += 17;

        /* ソースどおり xpos は 320 -> 0。表示窓が左へ動くので、
         * パターンは右へスライドアウトする。 */
        if (doit_count < 333) {
            doit_xpos -= doit_xposa / 4;
            if (doit_xpos < 0)
                doit_xpos = 0;
            else
                doit_xposa++;
            if (doit_xpos == 0) {
                doit_phase = 0;
                return;
            }
        }

        doit_draw(wx, wy, doit_vm, doit_rot, 80, 40, 1);
        doit_disp_page = doit_plv;
        doit_disp_stride = 80;
        doit_disp_xpos = doit_xpos;

        doit_rot += doit_rota / 10;
        doit_vm += doit_vma;
        if (doit_vm < 0) {
            doit_vm -= doit_vma;
            doit_vma = -doit_vma;
        }
        doit_vma--;
        doit_rota++;
        doit_advance_page(2);
        doit_count--;
    }
}

/* 1 プレーンの行を比較し、孤立した 1〜2 行の全幅ラインを上の行で埋める。 */
static void clean_plane_lines(u8 *pl, int stride) {
    u8 bad[200];
    int y, bx;
    memset(bad, 0, sizeof(bad));
    for (y = 1; y < 199; y++) {
        const u8 *a = pl + (y - 1) * stride;
        const u8 *b = pl + y * stride;
        const u8 *c = pl + (y + 1) * stride;
        int d1 = 0, d2 = 0, d3 = 0;
        for (bx = 0; bx < stride; bx++) {
            d1 += pop8[a[bx] ^ b[bx]];
            d2 += pop8[b[bx] ^ c[bx]];
            d3 += pop8[a[bx] ^ c[bx]];
        }
        if (d1 >= 64 && d2 >= 64 && d3 * 3 < d1 + d2)
            bad[y] = 1;
    }
    for (y = 1; y < 199; ) {
        int y2;
        if (!bad[y]) {
            y++;
            continue;
        }
        for (y2 = y; y2 < 199 && bad[y2]; y2++)
            ;
        if (y2 - y <= 2)
            for (int r = y; r < y2; r++)
                memcpy(pl + r * stride, pl + (y - 1) * stride, stride);
        y = y2;
    }
}

/* 4プレーンの表示ページを元の色のまま3bitへ詰め、表示プレーンに書く。 */
static void doit_render_planes(u8 *planes) {
    int y, bx, pi;
    int sh = doit_disp_xpos & 7;
    int byte0 = doit_disp_xpos >> 3;
    int rowbytes = doit_disp_stride;      /* 40 (doit1/2) / 80 (doit3) */
    const int pb = 40 * 256;              /* PLANE_BYTES */
    for (y = 0; y < 200; y++) {
        for (pi = 0; pi < 4; pi++) {
            const u8 *p = vplane[pi] + doit_disp_page * VPSTRIDE +
                          y * doit_disp_stride;
            u8 *d = planes + pi * pb + (GY + y) * 40;
            if (sh == 0) {
                for (bx = 0; bx < 40; bx++) {
                    int bi = byte0 + bx;
                    d[bx] = (bi < rowbytes) ? p[bi] : 0;
                }
            } else {
                for (bx = 0; bx < 40; bx++) {
                    int bi = byte0 + bx;
                    if (bi >= rowbytes) {
                        d[bx] = 0;
                        continue;
                    }
                    u8 b1 = (bi + 1 < rowbytes) ? p[bi + 1] : 0;
                    d[bx] = (u8)((p[bi] << sh) | (b1 >> (8 - sh)));
                }
            }
        }
    }
    for (pi = 0; pi < 4; pi++)
        clean_plane_lines(planes + pi * pb + GY * 40, 40);

    /* Pack each original 4-bit pixel to its set-bit count (0..4).
     * Cleaning is deliberately done first, independently on the original
     * planes, to preserve the existing rendered pixels exactly. */
    for (y = 0; y < 200; y++) {
        u8 *p0 = planes + 0 * pb + (GY + y) * 40;
        u8 *p1 = planes + 1 * pb + (GY + y) * 40;
        u8 *p2 = planes + 2 * pb + (GY + y) * 40;
        u8 *p3 = planes + 3 * pb + (GY + y) * 40;
        for (bx = 0; bx < 40; bx++) {
            u8 ab = (u8)(p0[bx] ^ p1[bx]);
            u8 cd = (u8)(p2[bx] ^ p3[bx]);
            u8 ac = (u8)(p0[bx] & p1[bx]);
            u8 bd = (u8)(p2[bx] & p3[bx]);
            u8 carry = (u8)(ab & cd);
            p0[bx] = (u8)(ab ^ cd);
            p1[bx] = (u8)(ac ^ bd ^ carry);
            p2[bx] = (u8)(ac & bd);
        }
    }
    video_set_palette(gpal);
}

/* ---- TROLL.UP (KOE.C 588-727) ----
 * 320x400 の画像を 320x256 画面へ縦に伸ばして表示する。
 * 参照動画: 219-225 秒 静止、225-232 秒 黒、232-236 秒 フェードイン
 * (画像は左へ約 40px シフト)、以降 259 秒までホールド + リップル。 */
static int troll_gen;                   /* troll_init の回数 (変換の記録を消す) */

/* ---- TROLL のスライドをハードウェアスクロールで行う (2026-11-16 ユーザー
 * 提案) ----
 * 原作 KOE.C:649-703 も表示開始位置 (パン) を動かすだけで、絵は描き直さない。
 * TROLL で動くのは横のパン (troll_pan) だけで、帯・揺れ・白飛びなどの値は
 * troll_step が毎回既定値に戻している。そこで、
 *   幅 704 ドットのチップ RAM の画面に [黒 320][絵 320][黒 64] を一度だけ
 *   作り (絵は troll_render の pan=0 の結果を c2p したもの)、表示開始 x を
 *   320 - troll_pan にする
 * と、troll_render (pan) + 全画面の c2p と同じ絵になる (pan > 0 は絵が右へ
 * ずれて左が黒、pan < 0 (リップル) は左へずれて右が黒)。毎フレームの
 * 描画も c2p も無くなる。横の細かいスクロールでは FMODE=3 が使えず表示
 * DMA が増えるが、この間 CPU はチップ RAM に書かない。
 * chunky は、PANIC が「直前の画面」として使うので、絵が止まってから
 * 1 回だけ troll_render で作る。 */
static void troll_render(u8 *chunky);
#define TW_WB   88                      /* 704 / 8 */
#define TW_X0   320                     /* 絵の左端 (ワールド座標) */
static u8 *tw_world;
static int tw_gen = -1, tw_shown, tw_chunky_pan = 0x7fff;

static int troll_hw_scroll(u8 *chunky) {
    if (tw_gen != troll_gen) {
        tw_gen = troll_gen;
        tw_shown = 0;
        tw_chunky_pan = 0x7fff;
        if (!tw_world)
            tw_world = (u8 *)mem_alloc(8 * TW_WB * 256, MEMF_CHIP | MEMF_CLEAR);
        if (!tw_world)
            return 0;                   /* 確保できなければ従来の方法 */
        /* 絵 (pan = 0) を chunky に描き、ファスト RAM の写しを作業場に
         * c2p してから、ワールドの x = 320 の位置へ行ごとに写す。 */
        {
            int keep = troll_pan;
            u8 *tmp = video_back_shadow();
            troll_pan = 0;
            troll_render(chunky);
            troll_pan = keep;
            c2p8_fast(chunky, tmp, TW, 256);
            for (int p = 0; p < 8; p++)
                for (int y = 0; y < 256; y++) {
                    const u32 *src = (const u32 *)(tmp + p * 10240 + y * 40);
                    u32 *dst = (u32 *)(tw_world + p * TW_WB * 256 + y * TW_WB + TW_X0 / 8);
                    for (int i = 0; i < 10; i++)
                        dst[i] = src[i];
                }
            tw_chunky_pan = 0;
        }
    }
    if (!tw_world)
        return 0;
    /* troll_render が扱う他の値が既定値でなければ従来の方法 */
    if (troll_band_top != 0 || troll_band_h != 256 || troll_band_w != 320 ||
        troll_shake_x || troll_shake_y || troll_flash || troll_tv || troll_dim <= 0 ||
        TW_X0 - troll_pan < 0 || TW_X0 - troll_pan > 353) {
        /* 704 ドットの範囲 (+1 ワードの先読み) を超える等。通常の画面に戻す */
        if (tw_shown) {
            video_set_custom_screen(0, 0);
            tw_shown = 0;
        }
        return 0;
    }
    {
        int sx = TW_X0 - troll_pan;
        if (!tw_shown) {
            video_switch_custom_screen(tw_world, TW_WB, sx);
            tw_shown = 1;
        } else {
            video_set_scroll(sx);
        }
    }
    /* PANIC 用の chunky: 絵が止まったら (パンとリップルの後) 1 回だけ作る */
    if (troll_pan_done && troll_ripple_frames >= 50 && tw_chunky_pan != troll_pan) {
        troll_render(chunky);
        tw_chunky_pan = troll_pan;
    } else {
        video_set_palette(gpal);
    }
    video_set_commit_area(0, 0, 0);
    return 1;
}

/* パートが終わって標準画面に戻った後に main から呼ぶ (チップ RAM を返す) */
void techno_release(void) {
    if (tw_world) {
        mem_free(tw_world, 8 * TW_WB * 256);
        tw_world = 0;
    }
    tw_gen = -1;
}
static int same9(const int *a, const int *b) {
    for (int k = 0; k < 9; k++)
        if (a[k] != b[k])
            return 0;
    return 1;
}
static void troll_init(void) {
    troll_gen++;
    memcpy(gpal, g_techno_troll_pal, 768);
    troll_frame = 0;
    troll_pan = 0;
    troll_xpos = 0;
    troll_xposa = 0;
    troll_pan_done = 0;
    troll_ripple_frames = 0;
    troll_ripple = 0;
    troll_ripplep = 8;
    troll_band_top = 0;
    troll_band_h = 256;
    troll_band_w = 320;
    troll_dim = 256;
    troll_tv = 0;
    troll_shake_x = 0;
    troll_shake_y = 0;
    troll_flash = 0;
    troll_go = 0;
}

static void troll_step(void) {
    int f = troll_frame;
    int i;

    memcpy(gpal, g_techno_troll_pal, 768);
    troll_band_top = 0;
    troll_band_h = 256;
    troll_band_w = 320;
    troll_dim = 256;
    troll_tv = 0;
    troll_shake_x = 0;
    troll_shake_y = 0;
    troll_flash = 0;

    /* 原作 KOE.C:649-673: xpos を 0 から 320 へ加速しながら動かす
     * (xpos += xposa/4; else xposa++)。約 50 フレーム (70Hz) かかる。
     * 以前は 20 フレームの等速 (320 - f*16) で 2.5 倍速かった。 */
    if (!troll_pan_done) {
        if (troll_xpos == 320) {
            troll_pan_done = 1;
            troll_pan = 0;
        } else {
            troll_xpos += troll_xposa / 4;
            if (troll_xpos > 320) {
                troll_xpos = 320;
                troll_pan = 0;
                troll_pan_done = 1;
            } else {
                troll_xposa++;
                troll_pan = 320 - troll_xpos;
            }
        }
    }
    /* 原作 KOE.C:674-703: パンの後 50 フレームのリップル。
     * xpos = 320 + sin1024[ripple]/ripplep、表示は 320-xpos。 */
    if (troll_pan_done && troll_ripple_frames < 50) {
        if (troll_ripplep > 1023)
            troll_ripplep = 1024;
        else
            troll_ripplep = troll_ripplep * 5 / 4;
        troll_pan = -(sin1024[troll_ripple & 1023] / troll_ripplep);
        troll_ripple += troll_ripplep + 100;
        /* KOE.C:621-631, 698-702: リップル中の最初の 16 フレームは
         * palfade[c] (= palette + (45 - 3c)、63 で頭打ち) を入れ、
         * 着地の瞬間に画面を明るくしてから元の色へ戻す。リップルが
         * 終わったら setpalarea(palette) (KOE.C:704)。6bit の 1 は
         * 8bit の 4。以前はこの明るさの変化が無かった
         * (2026-11-15 ユーザー指摘)。 */
        if (troll_ripple_frames < 16) {
            int add = (45 - troll_ripple_frames * 3) * 4;
            for (int k = 0; k < 768; k++) {
                int v = g_techno_troll_pal[k] + add;
                gpal[k] = (u8)(v > 255 ? 255 : v);
            }
        } else {
            memcpy(gpal, g_techno_troll_pal, 768);
        }
        troll_ripple_frames++;
        if (troll_ripple_frames >= 50)
            memcpy(gpal, g_techno_troll_pal, 768);
    } else if (troll_pan_done) {
        troll_pan = 0;
    }
    troll_frame++;
}

static void troll_render(u8 *chunky) {
    int x, y;
    int left = (320 - troll_band_w) / 2;
    /* 横の拡大率は行によらないので、ドットごとの割り算 (320x256 回) を
     * やめて 1 行分の表を先に作る (実機 68060 の速度対策、2026-11-15)。 */
    static s16 sxtab[TW];
    for (x = 0; x < TW; x++) {
        int xx = x - troll_shake_x;
        int sx;
        if (xx < left || xx >= left + troll_band_w) {
            sxtab[x] = -1;
            continue;
        }
        sx = ((2 * (xx - left) + 1) * 160) / troll_band_w;
        if (sx < 0) sx = 0;
        if (sx > 319) sx = 319;
        sx -= troll_pan;
        sxtab[x] = (s16)((sx >= 0 && sx < 320) ? sx : -2);
    }

    if (troll_band_h <= 0 || troll_band_w <= 0 || troll_dim <= 0) {
        for (y = 0; y < 256 * TW; y++)
            chunky[y] = 0;
        video_set_palette(gpal);
        return;
    }

    for (y = 0; y < 256; y++) {
        int yy = y - troll_shake_y;
        u8 *dst = chunky + y * TW;
        if (yy < troll_band_top || yy >= troll_band_top + troll_band_h) {
            memset(dst, 0, TW);
            continue;
        }
        {
            int sy = 200 + ((2 * (yy - troll_band_top) + 1 - troll_band_h)
                            * 200) / troll_band_h;
            const u8 *src;
            if (sy < 0) sy = 0;
            if (sy > 399) sy = 399;
            src = g_techno_troll + sy * 320;
            for (x = 0; x < TW; x++) {
                int sx = sxtab[x];
                if (sx == -1) {
                    dst[x] = 0;
                    continue;
                }
                if (troll_flash) {
                    dst[x] = 255;
                    continue;
                }
                if (sx >= 0) {
                    u8 v = src[sx];
                    if (troll_tv && v == 0)
                        v = 255;        /* 映像の黒も白飛びさせる */
                    dst[x] = v;
                } else {
                    dst[x] = 0;         /* 画面の外側は黒のまま */
                }
            }
        }
    }
    video_set_palette(gpal);
}

/* シーン終了 (TV が消えて黒) を main に知らせる。 */
int techno_finished(void) {
    return scene_phase == PH_END;
}

static int steps;
int techno_render(u8 *chunky, u8 *planes, int width, int height) {    int i;
    (void)planes;
    (void)width;
    (void)height;

    steps = timeline_steps();

    if (scene_phase == PH_A) {
        /* KOE.C:213-214: musplus >= -4 まで待ってから mframe を 0 にする。
         * (2026-11-14 に一度外したが、止まった原因は待ちではなく
         * LTO の複数パーティションによる bra.l の誤リロケーションだった。
         * Makefile の -flto-partition=one 参照。) */
        if (!pa_started) {
            /* 白フラッシュは「この待ちを抜けた音楽位置 + 925 tick」で決まる
             * (KOE.C:213-214, KOEA.ASM:887-893)。原作は実機の読み込み時間の
             * 分だけ遅れて order 17 (マーカー直後の窓) でこの待ちを抜けて
             * いた。原作動画ではフラッシュが order 22 頭の連打と同時なので、
             * 抜けた位置は 22:00 - 925 tick = 17:11 (16:60→22:00 は
             * 324 行 x speed 3 = 972 tick、差 47 tick ≒ 15.7 行)。
             * 読み込み時間 (VBL) ではなく、この音楽位置を直接待つ。 */
            if (dis_musplus() < -4 ||
                mus_pos() < ((17 << 8) | 11)) {
                render_phase_a_planes(planes);
                return 1;
            }
            dis_setmframe(0);
            pb_last_vbl = video_frame_vbl();
            pa_started = 1;
        }
        /* KOEB do_interference: 1 waitb ごとに 1 フレーム、256 フレーム
         * (KOEB.ASM:600)。70Hz フレームを 1.4/VBL で進める。 */
        for (i = 0; i < steps && tl_frame < 256; i++) {
            palanimc += patdir;
            if (palanimc < 0)
                palanimc = 8 * 3 - 3;
            if (palanimc >= 8 * 3)
                palanimc = 0;
            tl_frame++;
        }
        render_phase_a_planes(planes);
        if (tl_frame >= 256) {
            phase_b_init();
            scene_phase = PH_B;
        }
        return 1;
    }

    if (scene_phase == PH_B) {
        for (i = 0; i < steps && !pb_done; i++) {
            pb_step();
            if (dis_getmframe() >= PB_END_MFRAME)
                pb_done = 1;
        }
        if (!pb_done) {
            render_phase_b_planes(planes);
            return 1;
        }
        /* 最後のフレームだけ chunky を作り、遷移用に控える。 */
        render_phase_b(chunky);
        for (i = 0; i < 320 * 200; i++)
            last_b_img[i] = chunky[GY * TW + i];
        transition_init();
        scene_phase = PH_TRANS;
        return 0;
    }

    if (scene_phase == PH_TRANS) {
        for (i = 0; i < steps && tr_state < 3; i++)
            transition_step();
        if (tr_state == 0 && tr_hold_ready) {
            /* KOE.C:224-228 changes only the palette during the four flash
             * steps. Keep the already displayed interference frame; avoid
             * rebuilding chunky, running c2p, and copying unchanged planes. */
            video_set_palette(gpal);
            video_set_commit_area(0, 0, 0);
            return 1;
        }
        if (tr_state == 0)
            tr_hold_ready = 1;
        transition_render(chunky, planes);
        if (tr_state >= 3) {
            doit_init();
            scene_phase = PH_DOIT;
        }
        return 1;
    }

    if (scene_phase == PH_DOIT) {
        for (i = 0; i < steps && doit_phase != 0; i++)
            doit_step();
        if (doit_phase == 0) {
            troll_init();
            scene_phase = PH_TROLL;
        } else {
            doit_render_planes(planes);
            video_set_commit_area(3, GY, 200);
            return 1;
        }
    }

    if (scene_phase == PH_TROLL) {
#ifndef TEST_FIXED_SCENE
        /* 原作 KOE.C:706-709: 次のマーカー (original order 38 =
         * std 32:59) で TROLL を終了する。以前は 40 秒固定で、
         * TROLL が長すぎて MNTSCRL が一瞬になっていた。
         * 遷移のタイミング (PH_B/DOIT) は安定版のまま変えない
         * (音楽同期の開始待ちを入れると白画面で止まるため)。 */
        if (mus_pos() >= ((32 << 8) | 59))
            scene_phase = PH_END;
#endif
        /* 原作 KOE.C:637-648: TROLL の絵を画面外に描いたあと、
         *   if(a>35 || (a==35 && b>48)) break;  (a=order, b=row)
         * まで待ってから右から入れる。原作 MUSIC1.S3M の order 35 は、
         * 0xFE (3,14,19,25,27) を除いた std では 30。待つ間は黒。
         * 以前はこの待ちが無く、TROLL が原作より約 1.2 秒早く出ていた
         * (2026-11-14 動画比較: 原作は doit の後 1 秒黒→パン)。 */
        if (scene_phase == PH_TROLL && !troll_go) {
            if (mus_pos() > ((30 << 8) | 48)) {
                troll_go = 1;
            } else {
                for (i = 0; i < 256 * TW; i++)
                    chunky[i] = 0;
                for (i = 0; i < 768; i++)
                    gpal[i] = 0;
                video_set_palette(gpal);
                return 0;
            }
        }
        if (scene_phase == PH_TROLL) {
            for (i = 0; i < steps; i++) {
                troll_step();
                if (troll_frame >= 70 * 40) {   /* マーカーが無い場合の保険 */
                    scene_phase = PH_END;
                    break;
                }
            }
        }
        /* 終了フレームも TROLL を描いて残す。次の PANIC は
         * 「直前の VGA 画面」を溶かす (SHUTDOWN.C) ので、ここで
         * 黒にすると TV 消灯風のエフェクトが黒一色になり見えなくなる。 */
        /* TROLL は chunky 全体 (256 行) を描くので、以前は c2p_scene が
         * ファスト RAM の写しへ 256 行を変換し、さらに 8 プレーン x 256 行を
         * チップ RAM へ写していた (右からのスライドインが重い、2026-11-16
         * ユーザー報告)。写しを使わずチップ RAM の裏画面へ直接 c2p し、
         * 写しの転送はしない。絵を決める値が裏画面の前回と同じなら
         * (パンとリップルが終わって止まっている間)、描画も変換もしない。
         * 写し (shadow) はこの後使わない (TECHNO の最後の段階。次に使う
         * ときはシーン切り替えの video_clear_planes で 0 になる)。 */
        if (troll_hw_scroll(chunky))
            return 1;
        {
            /* troll_render の結果を決める値 (パレット以外) */
            static u8 *tk_buf[VIDEO_NBUF];
            static int tk_key[VIDEO_NBUF][9], tk_drawn[9];
            static int tk_valid[VIDEO_NBUF], tk_drawn_valid;
            static int tk_scene_gen = -1;
            u8 *chip = video_back_planes();
            int key[9] = { troll_pan, troll_band_top, troll_band_h, troll_band_w,
                           troll_shake_x, troll_shake_y, troll_flash, troll_tv,
                           troll_dim > 0 };
            int slot;
            if (tk_scene_gen != troll_gen) {     /* TROLL に入るたびに初期化 */
                tk_scene_gen = troll_gen;
                for (int i = 0; i < VIDEO_NBUF; i++) {
                    tk_buf[i] = 0;
                    tk_valid[i] = 0;
                }
                tk_drawn_valid = 0;
            }
            slot = video_back_index();      /* トリプルバッファの番号 */
            if (tk_buf[slot] != chip) {
                tk_buf[slot] = chip;
                tk_valid[slot] = 0;             /* 必ず変換する */
            }
            if (!tk_drawn_valid || !same9(key, tk_drawn)) {
                troll_render(chunky);
                memcpy(tk_drawn, key, sizeof(key));
                tk_drawn_valid = 1;
            } else {
                video_set_palette(gpal);        /* パレットは毎回反映 */
            }
            if (!tk_valid[slot] || !same9(key, tk_key[slot])) {
                c2p8_fast(chunky, chip, TW, 256);
                memcpy(tk_key[slot], key, sizeof(key));
                tk_valid[slot] = 1;
            }
            video_set_commit_area(0, 0, 0);
        }
        return 1;
    }

    for (i = 0; i < 256 * TW; i++)
        chunky[i] = 0;
    return 0;
}
