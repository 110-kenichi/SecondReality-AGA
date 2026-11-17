/*
 * M4 シーン: PLZPART (フルスクリーン・プラズマ) ? オリジナル PLZPART/PLZ.C +
 * ASMYT.ASM の plz() の移植。
 *
 * オリジナルは VGA の 4 プレーンを使い、1 本の走査線を
 * 2 種類のパラメータ (k / l) で 2 回描く。k 側はプレーン 1,3
 * (色のビット 2,3,6,7)、l 側はプレーン 0,2 (ビット 0,1,4,5) に
 * 書き込むので、1 ドットの色は k と l の両方から作られる。
 *
 *   plzline(y, c1..c4) の 1 グループ (4 ドット分):
 *     L16 = lsini16[(c2 + y - 4*g + 320) & 4095]
 *     A   = psini  [(c1 + 32*g + L16)    & 4095]
 *     L4  = lsini4 [(c4 + 16*g + y)      & 4095]
 *     B   = psini  [(c3 + 2*y + L4 - 4*g + 320) & 4095]
 *     v   = (A + B) & 255
 *   g = 0..83 (84 グループ = 336 ドット)。グループ内の隣り合う
 *   2 ドットは psini +16 / lsini4 +32 だけ異なる。
 *   色 = (k の値 & 0xCC) | (l の値 & 0x33)。
 *
 * パラメータは毎 VBL 次のようにドリフトする (COPPER.ASM moveplz):
 *   k1-=3 k2-=2 k3+=1 k4+=2 / l1-=1 l2-=2 l3+=2 l4+=3  (mod 4096)
 *
 * パレットは ptau[] から作る。オリジナルは pals[] を 5 種類定義して
 * いるが、実際のデモで表示されるのは pals[0] (赤青系) → pals[1]
 * (レインボー系) → pals[2] (モノトーン) の 3 段階 (切り替え 2 回)。
 * pals[3] / pals[4] はパート終了で表示されない。
 * 切り替え時はオリジナルのラインコンペア (COPPER.ASM の do_drop +
 * dtau[]) と同じく、描画を下方向へスライドさせて画面外へ追い出し、
 * そのあと新しい色を出す。
 *
 * Amiga では 320x256 の chunky に描いて c2p する。
 */
#include "scenes.h"
#include <string.h>
#include "../framework/video.h"
#include "../framework/vblank.h"
#include "../framework/dis.h"
#include "../music/music.h"
#define PLZ_POS() (((int)np_ord << 8) | (np_row & 0xff))

extern const u8 *g_plasma_psini;
extern const u8 *g_plasma_l4;
extern const u8 *g_plasma_l16;

/* プラズマ後のキューブ (plzcub.c) */
extern void plzcub_init(void);
extern int plzcub_render(u8 *chunky);
static int cube_mode;

#define PLZ_W       320
#define PLZ_H       256
#define PLZ_GROUPS  (PLZ_W / 4)     /* 80 グループ (オリジナルは 84) */
/* オリジナルは 384x400 の画面 (TWEAK.ASM tw_opengraph2 の「400」) に
 * 280 行のプラズマを置く。帯は 60..339 行で上下対称 (280/400 = 70%)。
 * 256 行では同じ比率の 179 行にし、上下の黒帯を残す。 */
#define PLZ_SRC_H   280
#define PLZ_SRC_SCR 400
#define PLZ_BAND_H  ((PLZ_SRC_H * PLZ_H) / PLZ_SRC_SCR)   /* 179 */
#define PLZ_BAND_TOP ((PLZ_H - PLZ_BAND_H) / 2)           /* 38 */

/* ---- 正弦テーブル (assets/plasma/*.bin) ---- */
static const u8 *psini;
static const u16 *lsini4;
static const u16 *lsini16;

/* ---- プラズマ・パラメータ (k = 奇数ドット系 / l = 偶数ドット系) ---- */
static int kk[4], ll[4];

static const int inittable[6][8] = {
    /* l1,   l2,   l3,   l4,   k1,   k2,   k3,   k4 */
    { 1000, 2000, 3000, 4000, 3500, 2300, 3900, 3670 },
    { 1000, 2000, 4000, 4000, 1500, 2300, 3900, 1670 },
    { 3500, 1000, 3000, 1000, 3500, 3300, 2900, 2670 },
    { 1000, 2000, 3000, 4000, 3500, 2300, 3900, 3670 },
    { 1000, 2000, 3000, 4000, 3500, 2300, 3900, 3670 },
    { 1000, 2000, 3000, 4000, 3500, 2300, 3900, 3670 },
};

/* パレット切り替え (音楽フレーム mframe = np_zframe、PLZ.C:68-69 の
 * dis_setmframe(0) から)。PLZ.C:46 の timetable = 723, 1491, 1875, ...。
 * 最終版 (C++ 版 PLZ.cpp:468) は curpal==4 の落下で終わるので、
 * pals[0] → [1] → [2] と切り替え、1875 の落下でパートを抜ける。
 * 以前はこの値を VBL 数と比べていたため、tick (MUSIC1 は 52Hz) との
 * 差で切り替えが 0.6〜1 秒遅れていた (2026-11-14)。 */
static const u32 plz_timetable[2] = { 723, 1491 };
#define PLZ_END_MFRAME 1875
#define PLZ_PHASES  2
/* phase 0..2 が使うパレット番号 (RGB / RB-black / white) */
static const u8 plz_palorder[3] = { 0, 1, 2 };

/* ---- ptau[] (PLZ.C の ptau と同じ。a=0..63 のみ使用) ---- */
static const u8 ptau[64] = {
      0,   1,   1,   1,   1,   1,   2,   2,   3,   3,   4,   5,   6,   7,   8,   9,
     10,  11,  12,  13,  14,  16,  17,  18,  20,  21,  23,  24,  25,  27,  28,  30,
     31,  33,  35,  36,  38,  39,  40,  42,  43,  45,  46,  47,  49,  50,  51,  52,
     53,  54,  55,  56,  57,  58,  59,  60,  60,  61,  61,  62,  62,  62,  62,  62,
};

static u8 palette[256 * 3];

static u32 plz_start_vbl, plz_last_vbl;
static volatile int phase;          /* 0..2 */
static volatile int pending;        /* ワイプ完了後に適用する phase (0 = なし) */
static volatile int wipe;           /* 経過 VBL (0..wipe_len) */
static volatile int wipe_active;    /* スライド中 */
static volatile int wipe_done;      /* スライド完了 (描画側で後始末) */
static volatile int wipe_len;       /* 今回のスライド VBL 数 */
static volatile int end_wipe;       /* パート終了のスライド */
static volatile int end_done;

/* PLZ.C:68-73 / COPPER.ASM:216-264: musplus>=0 を待って mframe=0。
 * 全色白 (PLZ.C:179) から cop_drop=128..255 の 128 フレームで pals[0] へ。
 * 落下のあと (cop_drop 66..96) は黒から 31 フレームで新しい色へ
 * (1 フレームに pals*8/256 を足す)。 */
static volatile int plz_started;
static volatile u16 plz_mf0;
static int white_k, white_acc;      /* 0..128 (70Hz フレーム) */
static int black_k = -1, black_acc; /* -1 = なし / 0..31 */
static u8 fade_out_pal[256 * 3];

/* 70Hz のフレーム数を 50Hz の VBL に換算する。
 * パレット切り替え: 原作の dtau は 56 フレームで画面外まで下がる
 *   (dtau[c]=c*c/4*43/128+60、c=56 で 263 > 256 行)。
 *   56 * 50/70 = 40 VBL。
 * パート終了: 原作は cop_drop>64 (65 フレーム) で plz() を抜ける。
 *   65 * 50/70 = 46.4 → 46 VBL。 */
#define WIPE_VBLS       40
#define END_WIPE_VBLS   46

/* 6bit (0-63) -> 8bit */
static inline u8 c6(int v) {
    if (v < 0) v = 0;
    if (v > 63) v = 63;
    return (u8)((v * 255) / 63);
}

/* パレット生成 (PLZ.C init_plz の pals[] を 8bit に変換したもの) */
static void build_palette(int n) {
    for (int i = 0; i < 256 * 3; i++)
        palette[i] = 0;

    switch (n) {
    case 0:     /* RGB */
        for (int a = 1; a < 64; a++)
            palette[a * 3 + 0] = c6(ptau[a]);
        for (int a = 0; a < 64; a++)
            palette[(64 + a) * 3 + 0] = c6(ptau[63 - a]);
        for (int a = 0; a < 64; a++)
            palette[(128 + a) * 3 + 2] = c6(ptau[a]);
        for (int a = 0; a < 64; a++) {
            palette[(192 + a) * 3 + 0] = c6(ptau[a]);
            palette[(192 + a) * 3 + 2] = c6(ptau[63 - a]);
        }
        break;
    case 1:     /* RB-black */
        for (int a = 1; a < 64; a++)
            palette[a * 3 + 0] = c6(ptau[a]);
        for (int a = 0; a < 64; a++) {
            palette[(64 + a) * 3 + 0] = c6(ptau[63 - a]);
            palette[(64 + a) * 3 + 2] = c6(ptau[a]);
        }
        for (int a = 0; a < 64; a++) {
            palette[(128 + a) * 3 + 1] = c6(ptau[a]);
            palette[(128 + a) * 3 + 2] = c6(ptau[63 - a]);
        }
        for (int a = 0; a < 64; a++) {
            palette[(192 + a) * 3 + 0] = c6(ptau[a]);
            palette[(192 + a) * 3 + 1] = c6(ptau[63]);
            palette[(192 + a) * 3 + 2] = c6(ptau[a]);
        }
        break;
    case 2:     /* white (グレー)
                 * 0-63: 黒 / 64-127: ptau[0..63]/2 の上り /
                 * 128-191: ptau[63..0]/2 の下り / 192-255: 黒 */
        for (int a = 0; a < 256; a++) {
            int v;
            if (a < 64)
                v = ptau[0];
            else if (a < 128)
                v = ptau[a - 64];
            else if (a < 192)
                v = ptau[191 - a];
            else
                v = ptau[0];
            u8 g = c6(v / 2);
            palette[a * 3 + 0] = g;
            palette[a * 3 + 1] = g;
            palette[a * 3 + 2] = g;
        }
        break;
    case 3:     /* RB-white */
        for (int a = 1; a < 64; a++)
            palette[a * 3 + 0] = c6(ptau[a]);
        for (int a = 0; a < 64; a++) {
            palette[(64 + a) * 3 + 0] = c6(ptau[63]);
            palette[(64 + a) * 3 + 1] = c6(ptau[a]);
            palette[(64 + a) * 3 + 2] = c6(ptau[a]);
        }
        for (int a = 0; a < 64; a++) {
            palette[(128 + a) * 3 + 0] = c6(ptau[63 - a]);
            palette[(128 + a) * 3 + 1] = c6(ptau[63 - a]);
            palette[(128 + a) * 3 + 2] = c6(ptau[63]);
        }
        for (int a = 0; a < 64; a++)
            palette[(192 + a) * 3 + 2] = c6(ptau[63]);
        break;
    default:
        break;
    }
}

/* 現在の phase のパラメータを即座に設定 (オリジナルの initpparas) */
static void set_params(int idx) {
    ll[0] = inittable[idx][0];
    ll[1] = inittable[idx][1];
    ll[2] = inittable[idx][2];
    ll[3] = inittable[idx][3];
    kk[0] = inittable[idx][4];
    kk[1] = inittable[idx][5];
    kk[2] = inittable[idx][6];
    kk[3] = inittable[idx][7];
}

/* 毎 VBL、スライド量を進めて表示開始走査線を下げる。
 * 原作 COPPER.ASM の do_drop (VGA の line compare) と同じく、描画とは
 * 独立したハードウェア縦シフトなので、描画が 17fps に落ちても
 * スライド自体は 50Hz で滑らかに動く。 */
static void plasma_vbl_tick(void) {
    int w;
    if (cube_mode || !plz_started)
        return;
    u32 t = (u16)(np_zframe - plz_mf0);     /* mframe (音楽 tick) */

    if (!wipe_active) {
        if (!end_wipe && !end_done && phase >= PLZ_PHASES &&
            t > PLZ_END_MFRAME) {
            end_wipe = 1;
            wipe_len = END_WIPE_VBLS;
            wipe = 0;
            wipe_active = 1;
        } else if (phase < PLZ_PHASES && pending == 0 &&
                   t > plz_timetable[phase]) {
            pending = phase + 1;
            wipe_len = WIPE_VBLS;
            wipe = 0;
            wipe_active = 1;
        }
    }
    if (!wipe_active)
        return;

    w = wipe + 1;
    if (w > wipe_len)
        w = wipe_len;
    wipe = w;

    /* 70Hz フレーム数 wf から dtau の移動量 (60 を引いた分) を出す。 */
    {
        /* 原作 COPPER.ASM do_drop: dtau[c] = c*c/4*43/128 + 60 (c は 70Hz
         * フレーム)。c = w*7/5 を整数に切り捨てると、50Hz の VBL ごとの
         * c の増分が 1,1,2,1,2,... と不揃いになり、高速部でスライドが
         * カクつく (2026-11-14 ユーザー指摘)。c を切り捨てずに
         *   off = (w*7/5)^2 * 43/512 = w*w * 49*43 / (25*512)
         * として 1 回だけ割る。到達時間 (40/46 VBL) は変わらない。 */
        int off = (w * w * 49 * 43) / (25 * 512);
        if (off > PLZ_H)
            off = PLZ_H;
        video_set_display_start(off);
    }
    if (w >= wipe_len) {
        wipe_active = 0;
        wipe_done = 1;
    }
}

void plasma_init(void) {
    psini = g_plasma_psini;
    lsini4 = (const u16 *)g_plasma_l4;
    lsini16 = (const u16 *)g_plasma_l16;

    phase = 0;
    cube_mode = 0;
    wipe = 0;
    wipe_active = 0;
    wipe_done = 0;
    wipe_len = 0;
    end_wipe = 0;
    end_done = 0;
    pending = 0;
    plz_started = 0;
    white_k = 0;
    white_acc = 0;
    black_k = -1;
    black_acc = 0;
    set_params(0);
    build_palette(plz_palorder[0]);
    /* 直前の LNS&ZOOM は全色 63 (白) にして終わる (LENS/MAIN.C:274-275)。
     * PLZ.C:68 の musplus 待ちの間はその白い画面がそのまま残る。 */
    for (int i = 0; i < 768; i++)
        fade_out_pal[i] = 255;
    video_set_palette(fade_out_pal);
    video_set_display_start(0);

    plz_start_vbl = vblank_frames();
    plz_last_vbl = plz_start_vbl;
    dis_setcopper(3, plasma_vbl_tick);
}

/* 隣り合う 2 ドット分をまとめて計算 (L16 の参照を共有する) */
static inline void plz_pair(int i1, int i3, int u, int w, u8 *v0, u8 *v1) {
    int l16 = lsini16[u & 4095];
    int a0 = psini[(i1 + l16) & 4095];
    int a1 = psini[(i1 + 16 + l16) & 4095];
    int b0 = psini[(i3 + lsini4[w & 4095]) & 4095];
    int b1 = psini[(i3 + lsini4[(w + 32) & 4095]) & 4095];
    *v0 = (u8)(a0 + b0);
    *v1 = (u8)(a1 + b1);
}

int plasma_render(u8 *chunky, u8 *planes, int width, int height) {
    (void)planes;
    (void)width;
    (void)height;

    u32 now = video_frame_vbl();
    u32 el = now - plz_last_vbl;
    if (el > 8)
        el = 8;
    plz_last_vbl = now;
    u32 t = now - plz_start_vbl;

    /* スライド完了後の後始末:
     * パレット切り替えなら新しい色を出し、パート終了なら黒のまま
     * キューブ開始まで待つ。 */
    if (wipe_done) {
        if (end_wipe) {
            /* 原作 PLZ.C:119-121: plz() を抜けたら set_plzstart(500) で
             * 表示開始を画面外に置いたまま (黒) にする。ここで表示位置を
             * 0 に戻すと、キューブ開始までの間にプラズマが一瞬見えた
             * (2026-11-14 ユーザー指摘)。 */
            end_done = 1;
        } else {
            if (pending) {
                phase = pending;
                pending = 0;
                set_params(phase);
                build_palette(plz_palorder[phase]);
                black_k = 0;            /* 黒からフェードイン */
                black_acc = 0;
                for (int i = 0; i < 768; i++)
                    fade_out_pal[i] = 0;
                /* 表示位置はすべてのリストですぐ 0 に戻るので、パレットも
                 * すぐ全部黒にする。描画先だけに書くと、表示中・予約中の
                 * 前の場面の絵が前のパレットで一瞬見えた (トリプルバッファ
                 * 化後、2026-11-17 ユーザー指摘)。 */
                video_set_palette_now(fade_out_pal);
            }
            video_set_display_start(0);
        }
        wipe_done = 0;
    }

    /* 開始: PLZ.C:68-69 の musplus>=0 待ちと dis_setmframe(0)。 */
    if (!plz_started) {
        /* 窓 (0..31) を丸ごと見逃しても止まらないよう、マーカーの位置
         * (WinUAE で計測した 52:00) を過ぎていれば始める (2026-11-15)。 */
        if (dis_musplus() < 0 && PLZ_POS() < ((52 << 8) | 0)) {
            for (int i = 0; i < 320 * 256; i++)
                chunky[i] = 0;      /* 色 0 = 白 (上の fade_out_pal) */
            return 0;
        }
        plz_mf0 = np_zframe;
        plz_start_vbl = now;
        plz_started = 1;
        prof_mark("PLZ-start");
        el = 0;
    }
    /* 白からのフェードイン (128 フレーム、70Hz を 1.4/VBL で進める) */
    if (white_k < 128) {
        white_acc += (int)el * 7;
        while (white_acc >= 5 && white_k < 128) {
            white_acc -= 5;
            white_k++;
        }
        for (int i = 0; i < 768; i++)
            fade_out_pal[i] = (u8)(255 + ((palette[i] - 255) * white_k) / 128);
        video_set_palette(fade_out_pal);
    }
    /* 落下後の黒からのフェードイン (31 フレーム、最終 248/256) */
    if (black_k >= 0) {
        black_acc += (int)el * 7;
        while (black_acc >= 5 && black_k < 31) {
            black_acc -= 5;
            black_k++;
        }
        for (int i = 0; i < 768; i++)
            fade_out_pal[i] = (u8)((palette[i] * 8 * black_k) / 256);
        video_set_palette(fade_out_pal);
        if (black_k >= 31)
            black_k = -1;
    }

    /* プラズマの最後は原作どおり下へスライドアウトしてからキューブへ。
     * 原作 PLZ.C:94 は 5 回目の色切り替えで cop_drop>64 になった時点で
     * plz() を抜ける (do_drop が dtau[] に従って表示を下げる)。
     * そのあと VECT.C の getspl() が画面外からキューブを入れる。 */
    /* 原作 VECT.C:116: while(dis_musplus()<13); でキューブ開始 (マーカー
     * から 13 行後)。プラズマのスライドアウト (end_done) が済んでから待つ。 */
    /* 窓 (13..31) を見逃した場合は、計測した位置 57:13 を過ぎていれば始める。 */
    if (!cube_mode && end_done &&
        (dis_musplus() >= 13 || PLZ_POS() >= ((57 << 8) | 13))) {
        cube_mode = 1;
        prof_mark("PLZ-cube");
        dis_setcopper(3, 0);
        /* キューブは常にトリプルバッファ (2026-11-17 ユーザー指示)。
         * 次のパートの切り替えで main の pick_buffers が設定し直す。 */
        video_set_triple(1);
        /* 画面を消してから表示位置を戻す (プラズマの残りを見せない) */
        video_clear_planes();
        video_set_display_start(0);
        plzcub_init();
    }
    if (cube_mode)
        return plzcub_render(chunky);

    /* パラメータのドリフト (COPPER.ASM:107,145-170 moveplz)。
     * 原作は copper 割り込みから 70Hz で毎フレーム呼ぶ。以前は 50Hz の
     * VBL ごとに 1 回で 0.71 倍速になり、模様の動きが遅く、原作動画と
     * 明るさの推移が合わなかった (2026-11-14)。1 VBL = 7/5 回。 */
    static int move_acc;
    move_acc += (int)el * 7;
    int moves = move_acc / 5;
    move_acc -= moves * 5;
    for (int i = 0; i < moves; i++) {
        kk[0] = (kk[0] - 3) & 4095;
        kk[1] = (kk[1] - 2) & 4095;
        kk[2] = (kk[2] + 1) & 4095;
        kk[3] = (kk[3] + 2) & 4095;
        ll[0] = (ll[0] - 1) & 4095;
        ll[1] = (ll[1] - 2) & 4095;
        ll[2] = (ll[2] + 2) & 4095;
        ll[3] = (ll[3] + 3) & 4095;
    }

    /* COPPER.ASM:124-140 pompota (copper2 から毎フレーム呼ばれる):
     * ラインコンペア (分割画面の開始行) を 0x3c / 0x3d で交互に、
     * pel panning (cop_scrl, copper1 で 3c0h/33h に書く) を 4 / 0 で
     * 交互に切り替える。プラズマは分割画面側 (VRAM 先頭) に描かれ、
     * 400 ライン表示 (TWEAK.ASM tw_opengraph2: 09h=0) では 1 行 = 1 走査線
     * なので、交互のフレームで模様が 1 行上・2 ドット左 (256 色の
     * pel panning 4 = 2 ドット) にずれ、k と l の市松の位置が毎フレーム
     * 入れ替わる。原作は 70Hz の毎フレームだが、ここでは描画する
     * フレームごとに切り替える。 */
    static u8 pompi;
    pompi ^= 1;
    int pomp_y = pompi ? 1 : 0;     /* 0x3c: 1 行早く始まる = 1 行上 */
    int pomp_x = pompi ? 2 : 0;     /* cop_scrl=4 → 2 ドット */

    for (int sy = 0; sy < PLZ_H; sy++) {
        u8 *dst = chunky + (u32)sy * PLZ_W;

        /* オリジナルは 384x400 の画面で、プラズマはそのうち 280 行だけを
         * 描く (画面の上下に黒い帯が残る)。256 行の画面では 280/400 の
         * 高さの帯に収め、走査線あたりの模様の進みを 280/179 倍にして
         * 縦方向の模様の密度を原作に合わせる。 */
        int by = sy - PLZ_BAND_TOP;
        if (by < 0 || by >= PLZ_BAND_H) {
            memset(dst, 0, PLZ_W);
            continue;
        }
        int y = (by * PLZ_SRC_H) / PLZ_BAND_H + pomp_y;

        /* 原作 PLZ.C:96-117 + ASMYT.ASM plzline/setplzparas:
         * 1 バイト (= Mode X の 4 ドットグループ ccc の 1 プレーン) ごとに
         *   v = psini[c1 + 8*ccc + lsini16[y + c2 + 320 - 4*ccc]]
         *     + psini[c3 + 2*y + 320 - 4*ccc + lsini4[y + c4 + 16*ccc]]
         * を書く。プレーン 1,3 (mask 0x0A) には偶数行で k、奇数行で l、
         * プレーン 0,2 (mask 0x05) には偶数行で l、奇数行で k が入る。
         * つまり 4 ドットのグループ内で k と l が市松模様に並び、値は
         * グループ単位で変わる。以前は k と l の平均を 1 ドットごとに
         * 取っていたため、色の分布と模様が原作と違っていた
         * (2026-11-14 ユーザー指摘: 赤青と緑赤青のシーンの模様)。 */
        for (int g = 0; g < PLZ_GROUPS + 1; g++) {
            int kv = psini[(kk[0] + 8 * g +
                            lsini16[(y + kk[1] + 320 - 4 * g) & 8191]) & 16383]
                   + psini[(kk[2] + 2 * y + 320 - 4 * g +
                            lsini4[(y + kk[3] + 16 * g) & 8191]) & 16383];
            int lv = psini[(ll[0] + 8 * g +
                            lsini16[(y + ll[1] + 320 - 4 * g) & 8191]) & 16383]
                   + psini[(ll[2] + 2 * y + 320 - 4 * g +
                            lsini4[(y + ll[3] + 16 * g) & 8191]) & 16383];
            u8 odd = (u8)(y & 1);
            /* x=4g+p: (p ^ y) が奇数なら k、偶数なら l */
            u8 c0 = (u8)(odd ? kv : lv);
            u8 c1 = (u8)(odd ? lv : kv);
            int x = 4 * g - pomp_x;
            if ((unsigned)x < PLZ_W) dst[x] = c0;
            if ((unsigned)(x + 1) < PLZ_W) dst[x + 1] = c1;
            if ((unsigned)(x + 2) < PLZ_W) dst[x + 2] = c0;
            if ((unsigned)(x + 3) < PLZ_W) dst[x + 3] = c1;
        }
    }
    return 0;                        /* chunky 描画 → 呼び出し側が c2p */
}
