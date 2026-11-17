/*
 * GLENZ (透明ポリゴン) — オリジナル GLENZ/MAIN.C + VEC.ASM + MATH.ASM の移植。
 *
 * 元の仕組み:
 *  - 14 点 (立方体 8 + 軸方向のトゲ 6) を回転・スケールし、24 枚の三角形に
 *    分けて投影する。
 *  - 背景 (FC.UH の 320x200 画像) のパレットは 16 色しか使わない。
 *  - 各ポリゴンは「色ワード」に 16 の倍数 (0x10..0xe0) を持ち、塗るときは
 *    chunky の値へ XOR する。背景の 0..15 と XOR すると同じブロックの
 *    0..15 になり、そのブロックのパレットを「面の明るさ + 背景色/4」に
 *    しておくと、重なった面が透けて明るくなる (これが Glenz)。
 *  - 面の明るさは投影後の符号付き面積 (＝向き) から求める。裏面も塗る。
 *
 * Amiga 版: 原作と同じ 320x200 の chunky に描き、320x256 の中央
 * (28..227 行) にそのまま置いて c2p する (引き伸ばしはしない)。
 * 行列・投影・タイムラインはオリジナルの式をそのまま使う。
 */
#include "scenes.h"
#include "../framework/video.h"
#include "../framework/vblank.h"
#include "../music/music.h"
#include "../framework/dis.h"
#include "../framework/c2p.h"

/* 画面転送 (c2p) と 320x200 -> 320x256 の写しを、書いた範囲だけにする
 * (2026-11-15 ユーザー承認)。本編 (Glenz 物体) は背景を前フレームの
 * 描画矩形 (dirty_*) だけ戻し、物体を描いた矩形を記録しているので、
 * 「戻した矩形 + 描いた矩形」だけを写して変換する。zoomer2・背景ズーム・
 * 開始待ち・背景の全面コピー (ワイプ中など)・診断表示のフレームは全体。 */
static c2p_track gz_trk;
static int gz_full;                 /* このフレームは全体を写した */
static int gz_r[2][4];              /* 本編: 戻した矩形・描いた矩形 (200 行座標、含む) */

extern const u8 *g_glenz_bg;    /* 320*200 (index 0..15) */
extern const u8 *g_glenz_pal;   /* 256*3 (先頭 16 色だけ意味がある) */
extern const u8 *g_glenz_sin;   /* 3600 x s16 BE */
extern const u8 *g_image_data;  /* title.raw (前パートのタイトル画面) */
extern int g_glenz_start_frame;  /* デバッグ用の開始フレーム */
extern int g_glenz_diag;         /* デバッグ用の診断モード (0=通常) */

#define GW          320
#define GH          200
#define GY          0                   /* 画面いっぱい (縦 1.28 倍) */
#define SW          320
#define SH          200

#define ZPOS        7500
#define PROJ_XMUL   256
#define PROJ_YMUL   213
#define PROJ_XADD   160
/* 投影の縦中心。GLENZ/VID.ASM はモードごとに値が違う:
 *   init320x200: projyadd=130 (130/200 = 65%)
 *   init320x350: projyadd=175 (175/350 = 50%)
 * 参照映像のオブジェクトは画面中央あたりで跳ねるので、350 ラインモードの
 * 比率 (50%) を 200 行に写した 100 を使う。+28 の表示オフセットは GY。 */
/* 原作 (GLENZ/VID.ASM init320x200) の projyadd=130。背景ズームの地平線も
 * 同じ 130 なので、ここを下げるとオブジェクトだけ上に浮いて上端が切れる。 */
#define PROJ_YCENTER 130
#define PROJ_YADD   (GY + PROJ_YCENTER)
#define PROJ_MINZ   128

#define NPTS        14

/* ---- 面の定義 (オリジナルの epolys / epolysb) ----
 * 3, color, a, b, c の並び。0 で終端。color の bit1 が色味 (青系) の指定。 */
static const u8 poly_main[] = {
    3, 0x02, 0, 1,  8,   3, 0x04, 1, 2,  8,
    3, 0x06, 2, 3,  8,   3, 0x08, 3, 0,  8,
    3, 0x0a, 2, 1, 10,   3, 0x0c, 1, 5, 10,
    3, 0x0e, 5, 6, 10,   3, 0x10, 6, 2, 10,
    3, 0x12, 2, 6, 12,   3, 0x14, 6, 7, 12,
    3, 0x16, 7, 3, 12,   3, 0x18, 3, 2, 12,
    3, 0x1a, 0, 3, 11,   3, 0x1c, 3, 7, 11,
    3, 0x1e, 7, 4, 11,   3, 0x20, 4, 0, 11,
    3, 0x22, 5, 1, 13,   3, 0x24, 1, 0, 13,
    3, 0x26, 0, 4, 13,   3, 0x28, 4, 5, 13,
    3, 0x2a, 5, 4,  9,   3, 0x2c, 4, 7,  9,
    3, 0x2e, 7, 6,  9,   3, 0x30, 6, 5,  9,
    0
};

/* 2 個目 (frame > 800 で出る大きい方)。面の順序が逆回り。 */
static const u8 poly_big[] = {
    3, 0x04, 0, 1,  8,   3, 0x02, 1, 2,  8,
    3, 0x04, 2, 3,  8,   3, 0x02, 3, 0,  8,
    3, 0x04, 2, 1, 10,   3, 0x02, 1, 5, 10,
    3, 0x04, 5, 6, 10,   3, 0x02, 6, 2, 10,
    3, 0x04, 2, 6, 12,   3, 0x02, 6, 7, 12,
    3, 0x04, 7, 3, 12,   3, 0x02, 3, 2, 12,
    3, 0x04, 0, 3, 11,   3, 0x02, 3, 7, 11,
    3, 0x04, 7, 4, 11,   3, 0x02, 4, 0, 11,
    3, 0x04, 5, 1, 13,   3, 0x02, 1, 0, 13,
    3, 0x04, 0, 4, 13,   3, 0x02, 4, 5, 13,
    3, 0x04, 5, 4,  9,   3, 0x02, 4, 7,  9,
    3, 0x04, 7, 6,  9,   3, 0x02, 6, 5,  9,
    0
};

/* 形状 (ZZZ = 50 / QQQ = 99 はオリジナルと同じ) */
static const s32 pts_main[NPTS * 3] = {
    -5000, -5000, -5000,   5000, -5000, -5000,
     5000,  5000, -5000,  -5000,  5000, -5000,
    -5000, -5000,  5000,   5000, -5000,  5000,
     5000,  5000,  5000,  -5000,  5000,  5000,
        0,     0, -8500,      0,     0,  8500,
     8500,     0,     0,  -8500,     0,     0,
        0,  8500,     0,      0, -8500,     0
};
static const s32 pts_big[NPTS * 3] = {
    -5940, -5940, -5940,   5940, -5940, -5940,
     5940,  5940, -5940,  -5940,  5940, -5940,
    -5940, -5940,  5940,   5940, -5940,  5940,
     5940,  5940,  5940,  -5940,  5940,  5940,
        0,     0, -10395,      0,     0, 10395,
    10395,     0,      0, -10395,     0,     0,
        0, 10395,      0,      0, -10395,    0
};

/* ---- sin テーブル (3600 = 360 度, 0.1 度) ---- */
static s16 sin_tab[3600];

static int ang(int d) {
    d %= 3600;
    return d < 0 ? d + 3600 : d;
}

#define SIN(d) sin_tab[ang(d)]
#define COS(d) sin_tab[ang((d) + 900)]

/* ---- 状態 ---- */
static u8  bgpic[GW * GH];
static u8  gpal[256 * 3];          /* 出力パレット */
static u8  back6[16 * 3];          /* 背景パレット (6bit, 可変) */
static u8  pal6[16 * 3];           /* 現在の背景パレット (6bit) */
static u8  rolcol[64];             /* 色ワードの下位バイト -> スロット */
static u8  rolused[32];            /* スロット使用中 */
static u8  backpal[16 * 3];        /* FC の 16 色 (6bit 由来の 8bit) */
static int basepal[16 * 3];        /* フェード計算用 (6bit) */

static int frame, rx, ry, rz;

/* 原作 MAIN.C:291 dis_setmframe(0) からの音楽フレーム。
 * 353: while(dis_getmframe()<300);  400: while(dis_getmframe()<333); */
static u16 mf0;
static int mf_phase;        /* 0=300 待ち / 1=333 待ち / 2=本体 */
static int g_started;       /* MAIN.C:290 の musplus 待ちを抜けた */
static int g_finished;      /* MAIN.C:409-411 で本体ループを抜けた */

/* ---- zoomer2 (GLENZ/ZOOMER.C:18-81) ----
 * 前のパート (BEGLOGO) のタイトル画面を表示し、黒い帯を上下から閉じる。
 * 同時にパレット 0..127 をグレー 30/63 へフェードし、閉じたら画面を
 * 黒にして MAIN.C の背景ズーム (床板の落下) に入る。
 * 原作の vram は 320x400、こちらは 320x256 なので 256/400 で縮尺する。
 */
static u8 zoom2_title[320 * 256];  /* タイトル画面 (chunky) */
static u8 zoom2_pal[768];
static u8 zoom2_work[768];
static int zoom2_zy, zoom2_zya, zoom2_zy2;
static int zoom2_frame, zoom2_done;
static int zoom2_track_top, zoom2_track_bottom, zoom2_track_valid;
static int zyy, zya, zoom_done;     /* 背景ズーム (MAIN.C 320-351) の状態 */
static u32 out200[320 * 200 / 4];   /* 320x200 描画バッファ */
static u32 tl_acc;                  /* 70Hz 相当で進めるための累算器 */
static u32 tl_last_vbl;
static int g_tl_steps;
static int ypos, yposa, jello, jelloa;
static int xscale, yscale, zscale, bscale;
static int oxp, oyp, ozp, oxb, oyb, ozb;
static int boingm, boingd;
static int lightshift;
static int fade16 = 64;             /* 終了フェード (0..64)。小のパレット用 */
static u32 last_vbl;                /* デバッグ: 描画間の VBL 計測 */
static int vbl_acc, vbl_n;

static s16 mat[9];                 /* 回転行列 (2^15 スケール) */
static s32 p_rot[NPTS * 3];
static s32 p_world[NPTS * 3];
static s16 p_sx[NPTS], p_sy[NPTS], p_vis[NPTS];
static u8  have_bg;

/* 320x200 描画バッファ (原作と同じ解像度で描いてから 256 行へ引き伸ばす) */

/* ---- 小さな正弦 (オリジナルの sin1024) ----
 * SIN1024.INC の実データは sin(2*pi*i/1024) * 256 (振幅 256)。
 * 3600 ステップの表へは i * 3600 / 1024 = i * 225 / 64 で写す。
 * (以前は振幅 1024 / 周期 1/10 で、小さいオブジェクトが大きく飛んでいた) */
static int s1024(int a) {
    return SIN((a & 1023) * 225 / 64) * 256 / 32767;
}

#define MUL15(a, b) ((s16)(((s32)(a) * (s32)(b)) >> 15))

/* ---- 回転行列 rY * rX * rZ (MATH.ASM calcmatrix の写し) ---- */
static void build_matrix(int ax, int ay, int az) {
    s32 xsin = SIN(ax), xcos = COS(ax);
    s32 ysin = SIN(ay), ycos = COS(ay);
    s32 zsin = SIN(az), zcos = COS(az);
    s32 m0, t, q;

    mat[7] = MUL15(ysin, zsin);
    m0 = MUL15(ycos, zcos);
    mat[0] = (s16)m0;
    mat[7] = (s16)(mat[7] - MUL15(m0, xsin));

    t = MUL15(xsin, ysin);
    mat[0] = (s16)(mat[0] - MUL15(zsin, t));
    mat[1] = (s16)MUL15(zcos, t);
    q = MUL15(ycos, zsin);
    mat[1] = (s16)(mat[1] + q);
    mat[6] = (s16)(MUL15(xsin, q) + MUL15(ysin, zcos));

    mat[3] = (s16)(-MUL15(xcos, zsin));
    mat[4] = (s16)MUL15(xcos, zcos);
    mat[2] = (s16)(-MUL15(xcos, ysin));
    mat[8] = (s16)MUL15(xcos, ycos);
    mat[5] = (s16)xsin;
}

/* 行列で点列を変換する (rotlist)。tx/ty/tz を足す。
 * 原作 VEC.ASM:123-160 の rotlist は、行列 row1 (m3..m5) の結果を
 * 出力 z に、row2 (m6..m8) の結果を出力 y に入れる。ここを row1→y,
 * row2→z にすると、GLENZ の jello の縦縮み (matrix[8]=zscale) が
 * 奥行きに掛かってしまう。 */
static void rot_list(s32 *dst, const s32 *src, int tx, int ty, int tz) {
    for (int i = 0; i < NPTS; i++) {
        s32 x = src[i * 3 + 0], y = src[i * 3 + 1], z = src[i * 3 + 2];
        dst[i * 3 + 0] = ((mat[0] * x + mat[1] * y + mat[2] * z) >> 15) + tx;
        dst[i * 3 + 1] = ((mat[6] * x + mat[7] * y + mat[8] * z) >> 15) + ty;
        dst[i * 3 + 2] = ((mat[3] * x + mat[4] * y + mat[5] * z) >> 15) + tz;
    }
}

/* 投影 (projlist) */
static void proj_list(void) {
    for (int i = 0; i < NPTS; i++) {
        s32 x = p_world[i * 3 + 0], y = p_world[i * 3 + 1];
        s32 z = p_world[i * 3 + 2];
        if (z < PROJ_MINZ)
            z = PROJ_MINZ;
        s32 sx = (x * PROJ_XMUL) / z + PROJ_XADD;
        s32 sy = (y * PROJ_YMUL) / z + PROJ_YADD;
        if (sx < -32000) sx = -32000;
        if (sx >  32000) sx =  32000;
        if (sy < -32000) sy = -32000;
        if (sy >  32000) sy =  32000;
        p_sx[i] = (s16)sx;
        p_sy[i] = (s16)sy;
    }
}

/* ---- 0 埋め (bgpic が全 0 のときの背景復元用) ---- */
static void fill_zero(u8 *dst, int n) {
    int i = 0;
    while (i < n && ((u32)(dst + i) & 3))
        dst[i++] = 0;
    if (i + 4 <= n) {
        u32 *p = (u32 *)(dst + i);
        while (i + 3 < n) {
            *p++ = 0;
            i += 4;
        }
    }
    while (i < n)
        dst[i++] = 0;
}

/* ---- スパン書き込み ----
 * 0 = XOR / 1 = 上書き / 2 = color | bgpic (通常の Glenz) */
static u8 *g_buf;
/* 前フレームで描いた矩形 (背景を戻す範囲) */
static int dirty_x0, dirty_x1, dirty_y0, dirty_y1;
static u8  bg_full;
static u8  bg_changed;   /* bgpic を書き換えた (全画面コピーし直す) */
static u8  bg_zero;      /* bgpic が全 0 (ワイプ後)。コピーの代わりに 0 で埋める */

static void span_write(u8 *row, int xa, int xb, u8 val, int mode) {
    int x = xa;

    if (xa > xb)
        return;                     /* 空のスパンで dirty 矩形を壊さない */
    if (xa < dirty_x0) dirty_x0 = xa;
    if (xb > dirty_x1) dirty_x1 = xb;

    while (x <= xb && ((u32)(row + x) & 3)) {
        if (mode == 0)      row[x] ^= val;
        else if (mode == 1) row[x] = val;
        else                row[x] = (u8)(val | (row[x] & 0x0f));
        x++;
    }
    if (x + 4 <= xb + 1) {
        u32 *p32 = (u32 *)(row + x);
        u32 v32 = (u32)val * 0x01010101u;
        if (mode == 0) {
            while (x + 3 <= xb) { *p32++ ^= v32; x += 4; }
        } else if (mode == 1) {
            while (x + 3 <= xb) { *p32++ = v32; x += 4; }
        } else {
            while (x + 3 <= xb) {
                u32 b = *p32 & 0x0f0f0f0fu;
                *p32++ = v32 | b;
                x += 4;
            }
        }
    }
    while (x <= xb) {
        if (mode == 0)      row[x] ^= val;
        else if (mode == 1) row[x] = val;
        else                row[x] = (u8)(val | (row[x] & 0x0f));
        x++;
    }
}

/* ---- 辺の 16.16 傾き ----
 * x1-x0 はクランプ (±32000) の結果 64000 まであり得る。s32 で <<16 すると
 * 符号付きであふれるので、絶対値を u32 で扱う (64000<<16 は u32 に収まる)。 */
static s32 edge_dx(int x0, int x1, int dy) {
    s32 d = x1 - x0;
    u32 a = (d < 0) ? (u32)(-d) : (u32)d;
    u32 q = (a << 16) / (u32)dy;
    if (q > 0x3fffffffu)          /* ほぼ水平な辺。画面外なので飽和でよい */
        q = 0x3fffffffu;
    return (d < 0) ? -(s32)q : (s32)q;
}

/* 飽和付きの積・和 (座標が画面外へ大きく出ても破綻しないように) */
static s32 mul_sat(s32 a, s32 b) {
    u32 aa = (a < 0) ? (u32)(-a) : (u32)a;
    u32 bb = (b < 0) ? (u32)(-b) : (u32)b;
    u32 p;
    if (aa && bb > 0x7fffffffu / aa)
        p = 0x7fffffffu;
    else
        p = aa * bb;
    return ((a < 0) != (b < 0)) ? -(s32)p : (s32)p;
}

static s32 add_sat(s32 a, s32 b) {
    if (b > 0) {
        if (a > 0x7fffffff - b)
            return 0x7fffffff;
    } else if (b < 0) {
        if (a < (s32)(0x80000000u + (u32)(-b)))
            return (s32)0x80000000u;
    }
    return a + b;
}

/* ---- ポリゴン 1 枚を塗る (診断用。通常はエッジ表で塗る) ---- */
static void fill_poly(const s16 *sx, const s16 *sy, int n, u8 val, int mode) {
    const int w = SW;
    int i;

    int emin[8], emax[8];
    s32 ex[8], edx[8];
    int ne = 0;
    int ytop = 0x7fff, ybot = -0x7fff;

    for (i = 0; i < n; i++) {
        int j = (i + 1 == n) ? 0 : i + 1;
        int y0 = sy[i], y1 = sy[j];
        int x0 = sx[i], x1 = sx[j];
        if (y0 == y1)
            continue;
        if (y0 > y1) {
            int t = y0; y0 = y1; y1 = t;
            t = x0; x0 = x1; x1 = t;
        }
        emin[ne] = y0;
        emax[ne] = y1;
        ex[ne] = (s32)((u32)x0 << 16);
        edx[ne] = edge_dx(x0, x1, y1 - y0);
        ne++;
        if (y0 < ytop) ytop = y0;
        if (y1 > ybot) ybot = y1;
    }
    if (!ne)
        return;

    /* オリジナルは 320x200 の画面なので、その範囲外には描かない */
    if (ytop < GY) ytop = GY;
    if (ybot > GY + GH - 1) ybot = GY + GH - 1;
    if (ytop > ybot)
        return;

    /* 画面の上端より上で始まる辺は、上端での x まで進めておく */
    for (i = 0; i < ne; i++) {
        if (emin[i] < ytop) {
            ex[i] = add_sat(ex[i], mul_sat(edx[i], ytop - emin[i]));
            emin[i] = ytop;
        }
    }

    for (int y = ytop; y <= ybot; y++) {
        int xs[8], nx = 0;
        for (i = 0; i < ne; i++) {
            if (y < emin[i] || y >= emax[i])     /* 半開区間 */
                continue;
            xs[nx++] = (int)ex[i];               /* 16.16 のまま入れる */
            ex[i] += edx[i];                     /* 乗算ではなく加算で進める */
        }
        if (nx < 2)
            continue;
        for (i = 1; i < nx; i++) {
            int v = xs[i], k = i - 1;
            while (k >= 0 && xs[k] > v) {
                xs[k + 1] = xs[k];
                k--;
            }
            xs[k + 1] = v;
        }
        for (i = 0; i + 1 < nx; i += 2) {
            /* ピクセル中心 (x+0.5) が [xl, xr) に入る列だけ塗る。 */
            s32 xl = (s32)xs[i], xr = (s32)xs[i + 1];
            int xa = (int)((xl - 0x8000 + 0xFFFF) >> 16);
            int xb = (int)(((xr - 0x8000 + 0xFFFF) >> 16) - 1);
            if (xa < 0) xa = 0;
            if (xb > w - 1) xb = w - 1;
            if (xb < xa)
                continue;
            span_write(g_buf + (s32)y * w, xa, xb, val, mode);
        }
        if (y < dirty_y0) dirty_y0 = y;
        if (y > dirty_y1) dirty_y1 = y;
    }
}

/* ---- エッジ表 (NEW.ASM __newgroup 相当) ----
 * 各面の辺を {Y1,Y2,X,DX,COLOR} として登録する。同じ辺 (Y2/X/DX が一致)
 * が既にあれば色を XOR して新しい辺は捨てる (共有エッジの打ち消し)。 */
typedef struct {
    s32 x, dx;
    s16 y1, y2;
    u16 color;
    u8 used;
} Edge;

#define MAXEDGE 512
static Edge edges[MAXEDGE];
static int nedge;
/* 共有辺の打ち消しを座標比較 (O(n^2)) ではなく頂点ペアで引く。
 * 14 点しかないので 14x14 の表で O(1)。 */
static s16 edge_index[NPTS][NPTS];

static void group_begin_obj(void) {
    for (int i = 0; i < NPTS; i++)
        for (int j = 0; j < NPTS; j++)
            edge_index[i][j] = -1;
}

static void group_init(void) {
    nedge = 0;
    group_begin_obj();
}

static void group_add_edge(int x0, int y0, int x1, int y1, u16 color,
                           int i0, int i1) {
    if (y0 == y1)
        return;
    if (y0 > y1) {
        int t = y0; y0 = y1; y1 = t;
        t = x0; x0 = x1; x1 = t;
    }
    s32 ex = (s32)((u32)x0 << 16);
    s32 edx = edge_dx(x0, x1, y1 - y0);

    /* 画面の上端より上で始まる辺は、上端での x まで進めておく。
     * 進めないと、以降のすべての行で x がずれる。 */
    if (y0 < GY) {
        ex = add_sat(ex, mul_sat(edx, GY - y0));
        y0 = GY;
    }
    if (y1 <= GY)
        return;                       /* 画面より上で終わる辺 */

    /* 頂点ペアで共有辺を探す (同じ辺は 2 枚の面から 1 回ずつ) */
    int a = i0, b = i1;
    if (a > b) {
        int t = a; a = b; b = t;
    }
    int ei = edge_index[a][b];
    if (ei >= 0) {
        edges[ei].color ^= color;     /* 共有エッジ → 打ち消し */
        return;
    }

    if (nedge >= MAXEDGE)
        return;
    edges[nedge].x = ex;
    edges[nedge].dx = edx;
    edges[nedge].y1 = (s16)y0;
    edges[nedge].y2 = (s16)y1;
    edges[nedge].color = color;
    edges[nedge].used = 1;
    edge_index[a][b] = (s16)nedge;
    nedge++;
}

/* ---- 走査線スパン塗り (ng_pass2 + ng_pass3 相当) ----
 * 各走査線で有効な辺を x でソートし、辺をまたぐたびに色を XOR して
 * いく。描画は pixel = color | bgpic。 */
static void group_draw(void) {
    /* アクティブ辺表: 辺を y1 でバケットに入れ、行ごとに追加/削除する。
     * 毎行 nedge (最大 144) 本を走査するのを避ける (2 物体時の低下対策)。 */
    static s16 head[GH];
    static s16 enext[MAXEDGE];
    int list[MAXEDGE];
    int nl = 0;

    for (int i = 0; i < GH; i++)
        head[i] = -1;
    for (int e = 0; e < nedge; e++) {
        if (!edges[e].used)
            continue;
        int yy = edges[e].y1 - GY;
        if (yy < 0 || yy >= GH)
            continue;
        enext[e] = head[yy];
        head[yy] = (s16)e;
    }

    for (int yy = 0; yy < GH; yy++) {
        int y = GY + yy;

        /* この行から有効になる辺を追加 */
        for (int e = head[yy]; e >= 0; e = enext[e])
            list[nl++] = e;

        /* 下端に達した辺を削除 (y >= y2) */
        {
            int w = 0;
            for (int i = 0; i < nl; i++) {
                int e = list[i];
                if (y < edges[e].y2)
                    list[w++] = e;
            }
            nl = w;
        }
        if (nl < 2) {
            /* 辺が 2 本未満でも x は進める (進めないと以降の行がずれる) */
            for (int a = 0; a < nl; a++)
                edges[list[a]].x += edges[list[a]].dx;
            continue;
        }

        /* x でソート (辺のインデックスを挿入ソート)
         * 前の行の順序がほぼ残るので、ほぼ整列済みの挿入ソートで速い。 */
        for (int a = 1; a < nl; a++) {
            int v = list[a], k = a - 1;
            while (k >= 0 && edges[list[k]].x > edges[v].x) {
                list[k + 1] = list[k];
                k--;
            }
            list[k + 1] = v;
        }

        u8 *row = g_buf + (s32)y * SW;
        u16 color = 0;
        int prevx = 0;
        int touched = 0;
        for (int a = 0; a < nl; a++) {
            int x = (int)(edges[list[a]].x >> 16);
            if (x < 0) x = 0;
            if (x > SW) x = SW;
            if (x == prevx) {
                /* 同じ x の辺は色を XOR して 1 本にまとめる */
                color ^= edges[list[a]].color;
                continue;
            }
            if (color) {
                span_write(row, prevx, x - 1, (u8)color, 2);
                touched = 1;
            }
            prevx = x;
            color ^= edges[list[a]].color;
        }
        if (color) {
            span_write(row, prevx, SW - 1, (u8)color, 2);
            touched = 1;
        }
        /* 次の行へ */
        for (int a = 0; a < nl; a++)
            edges[list[a]].x += edges[list[a]].dx;
        if (touched) {
            if (y < dirty_y0) dirty_y0 = y;
            if (y > dirty_y1) dirty_y1 = y;
        }
    }
}

/* ---- 背景ズーム (オリジナル MAIN.C 320-351) ----
 * fc 画像の 0..99 行を y1..y2 に縦引き伸ばし、その下に 100..107 行の帯を
 * 8 行だけ置く。yy が加速して 48*16 を超えると跳ね返り、|ya|<4 で終了。
 * 最終状態は y1=153 / y2=200 で、チェック柄の床が画面最下部に収まる。 */

/* 320x200 -> 320x256 (縦 25/32 倍)。GLENZ は原作で 4:3 全面なので、
 * 1:1 ではなく画面いっぱいに広げる。 */
/* 320x200 を 256 行の画面の中央 (28..227 行) にそのまま置く。
 * 以前は縦 1.28 倍に引き伸ばしていたが、行の複製でポリゴンの斜めの辺が
 * 段になるため、いったん引き伸ばしをやめた (2026-11-15 ユーザー指示)。 */
static void stretch200(const u8 *src, u8 *dst) {
    u32 *d = (u32 *)dst;
    const u32 *s = (const u32 *)src;
    for (int i = 0; i < 28 * 320 / 4; i++)
        d[i] = 0;
    for (int i = 0; i < 200 * 320 / 4; i++)
        d[28 * 320 / 4 + i] = s[i];
    for (int i = 0; i < 28 * 320 / 4; i++)
        d[228 * 320 / 4 + i] = 0;
}

static int zoom_step_frame(void) {
    zya++;
    zyy += zya;
    if (zyy > 48 * 16) {
        zyy -= zya;
        zya = -zya * 2 / 3;
        if (zya > -4 && zya < 4)
            return 0;
    }
    return 1;
}

static void zoom_draw(u8 *base, int stride) {
    int ry, c, b;
    int y = zyy / 16;
    int y1 = 130 + y / 2;
    int y2 = 130 + y * 3 / 2;

    if (y1 > GH) y1 = GH;
    if (y2 > GH) y2 = GH;
    /* 原作 MAIN.C:332 は y2==y1 のとき b を更新せず、床板の先頭 16 行
     * (fcrow2) だけは最初のフレームから描く。C++ 版 GLENZ_MAIN.cpp:1253
     * も同じ。ここでは y2==y1 でも下の 16 行を描く。 */
    b = (y2 > y1) ? 25600 / (y2 - y1) : 0;
    c = 0;
    for (ry = y1; ry < y2; ry++, c += b) {
        const u8 *src = g_glenz_bg + (s32)(c >> 8) * GW;
        u8 *d = base + (s32)ry * stride;
        for (int x = 0; x < GW; x++)
            d[x] = src[x];
    }
    for (c = 0; c < 16 && ry < GH; c++, ry++) {
        u8 *d = base + (s32)ry * stride;
        if (c > 7) {
            for (int x = 0; x < GW; x++)
                d[x] = 0;
        } else {
            const u8 *src = g_glenz_bg + (s32)(100 + c) * GW;
            for (int x = 0; x < GW; x++)
                d[x] = src[x];
        }
    }
}

/* ---- 符号付き 2D 外積 ----
 * 投影座標は ±32000 まで残すので、差 (最大 64000) の積は s32 を超える。
 * 符号と大きさだけ分かればよい (大きさは 63 で飽和する) ので、
 * 積を飽和させてから引く。 */
static s32 cross3(int x0, int y0, int x1, int y1, int x2, int y2) {
    s32 p = mul_sat(x0 - x1, y0 - y2);
    s32 q = mul_sat(y0 - y1, x0 - x2);
    if (q > 0 && p < (s32)(0x80000000u + (u32)q))
        return (s32)0x80000000u;
    if (q < 0 && p > (s32)(0x7fffffffu + (u32)q))
        return 0x7fffffff;
    return p - q;
}

/* ---- 1 オブジェクト描画 ---- */
static void draw_object(const s32 *pts, const u8 *polys,
                        int ax, int ay, int az,
                        int scx, int scy, int scz,
                        int tx, int ty, int tz, int mode) {
    /* 1) 回転 */
    build_matrix(ax, ay, az);
    rot_list(p_rot, pts, 0, 0, 0);
    /* 2) スケール + 平行移動 */
    mat[0] = (s16)(scx * 64); mat[1] = 0; mat[2] = 0;
    mat[3] = 0; mat[4] = (s16)(scy * 64); mat[5] = 0;
    mat[6] = 0; mat[7] = 0; mat[8] = (s16)(scz * 64);
    rot_list(p_world, p_rot, tx, ty, tz);
    /* 2b) 床で切る。原作 MAIN.C:611 "if(frame<800) ccliplist(points2);"
     * (小オブジェクトだけ)。VEC.ASM:234-253 cliplist は各点の y
     * ([si+4]) が 1500 以上なら 1500 にする。y=1500 が床板の面で、
     * 床に当たった瞬間に下側の頂点が床に押し付けられて潰れる。
     * 以前はこれが無く、オブジェクトが床の下まで沈み (跳ねる位置が
     * 低く見える)、潰れも jello のスケールだけで小さかった
     * (2026-11-15 ユーザー指摘)。 */
    if (mode == 0 && frame < 800) {
        for (int i = 0; i < NPTS; i++)
            if (p_world[i * 3 + 1] >= 1500)
                p_world[i * 3 + 1] = 1500;
    }
    /* 3) 投影 */
    proj_list();

    /* 4) 塗る */
    group_begin_obj();          /* 共有辺の表はオブジェクトごとにリセット */
    const u8 *p = polys;
    while (*p) {
        int n = *p++;
        int col = *p++;
        int idx[4];
        for (int i = 0; i < n; i++)
            idx[i] = p[i];
        p += n;

        s16 sx[4], sy[4];
        for (int i = 0; i < n; i++) {
            sx[i] = p_sx[idx[i]];
            sy[i] = p_sy[idx[i]];
        }

        /* 符号付き面積 (checkhiddenbx と同じ)。cross < 0 が裏面。
         * checkhiddenbx (VEC.ASM):
         *   (x0-x1)*(y0-y2) - (y0-y1)*(x0-x2)
         * 2 項目の y は y0-y1。y0-y2 にしていると裏表を取り違える。 */
        s32 cross = cross3(sx[0], sy[0], sx[1], sy[1], sx[2], sy[2]);
        int hidden = cross < 0;
        s32 a = (cross < 0) ? (s32)(0u - (u32)cross) : cross;
        if (a < 0)
            a = 0x7fffffff;              /* INT32_MIN の絶対値 */
        int shade = a >> (lightshift == 9 ? 7 : 8);
        if (lightshift != 9)
            shade += shade >> 1;
        if (shade < 0)
            shade = 0;
        if (shade > 63)
            shade = 63;

        int fillv;
        int base = col & 0xff;

        if (mode == 1) {
            /* demo_glz2: 表示面は色ワードの bit1 (0/1)、裏面は 2/4 を
             * そのまま塗る。パレットは背景 (赤) を使う。 */
            fillv = hidden ? base : ((base >> 1) & 1);
        } else {
            /* demo_glz */
            if (hidden) {
                int sl = rolcol[base & 63];
                rolcol[base & 63] = 0;
                if (sl < 32)
                    rolused[sl] = 0;
                /* オリジナルどおり ((base>>1)&1)<<2 = 0 か 4。
                 * 0 は XOR で何も足さない (透明) という意味。 */
                fillv = (base & 2) ? 4 : 0;
            } else {
                int slot = rolcol[base & 63];
                if (slot == 0) {
                    for (slot = 2; slot < 32; slot += 2)
                        if (!rolused[slot])
                            break;
                    if (slot >= 32)
                        slot = 0x1e;
                    rolcol[base & 63] = (u8)slot;
                    rolused[slot] = 1;
                }
                int al = slot << 3;
                int cr, cg, cb;
                if (base & 2) {
                    /* オリジナルは cl を設定しないので R は直前の値
                     * (_lightshift 由来の 7 か 10) のまま。これで青になる。 */
                    cr = (lightshift == 9) ? 7 : 10;
                    cg = shade >> 1;
                    cb = shade;
                } else {
                    cr = cg = cb = shade;
                }
                for (int i = 0; i < 16; i++) {
                    int r = cr + back6[i * 3 + 0] / 4;
                    int g = cg + back6[i * 3 + 1] / 4;
                    int bb = cb + back6[i * 3 + 2] / 4;
                    if (r > 63) r = 63;
                    if (g > 63) g = 63;
                    if (bb > 63) bb = 63;
                    /* 終了間際は大 (0..15) だけでなく小 (slot<<3) も
                     * 同じ係数でフェードさせる。 */
                    r = r * fade16 / 64;
                    g = g * fade16 / 64;
                    bb = bb * fade16 / 64;
                    gpal[(al + i) * 3 + 0] = (u8)((r << 2) | (r >> 4));
                    gpal[(al + i) * 3 + 1] = (u8)((g << 2) | (g >> 4));
                    gpal[(al + i) * 3 + 2] = (u8)((bb << 2) | (bb >> 4));
                }
                fillv = al;
            }
        }

        if (g_glenz_diag & 1) {
            /* 診断: 全面を同じ色 (15=緑) で上書きし、形だけを見る。
             * XOR を使わないので、重なりで色が 0 になる影響を受けない。 */
            fill_poly(sx, sy, n, 15, 1);
        } else {
            /* 面の各辺をエッジ表へ (塗りは最後にまとめて行う) */
            for (int e = 0; e < n; e++) {
                int f = (e + 1 == n) ? 0 : e + 1;
                group_add_edge(sx[e], sy[e], sx[f], sy[f], (u16)fillv,
                               idx[e], idx[f]);
            }
        }
    }
}

static void palette_step(void);

/* 1 フレームぶんのタイムライン (MAIN.C の写し)。
 * 開始フレームを指定したときは、描画せずにこの関数だけを回して
 * 状態を進める。 */
static void timeline_step(void) {
    int a, b;


frame++;
rx = (rx + 32) % 10800;
ry = (ry + 7) % 10800;

if (frame > 900) {
    a = frame - 900;
    b = frame - 900;
    if (b > 50) b = 50;
    oxp = s1024(a * 3) * b / 10;
    oyp = s1024(a * 5) * b / 10;
    ozp = (s1024(a * 4) / 2 + 128) * b / 16;
    if (frame > 1800) {
        a = frame - 1800 + 64;
        if (a > 1024) a = 1024;
        oxb = -(s32)s1024(a * 6) * a / 40;
        oyb = -(s32)s1024(a * 7) * a / 40;
        ozb = (s32)(s1024(a * 8) + 128) * a / 40;
    } else {
        oxb = -s1024(a * 6);
        oyb = -s1024(a * 7);
        ozb = s1024(a * 8) + 128;
    }
    b = 1800 - frame;
    if (b < 0) {
        if (b < -99) b = -99;
        oyp -= b * b / 2;
    }
}

if (frame > 800) {
    if (frame > 1220 + 789) {
        if (xscale > 0) xscale--;
        if (yscale > 0) yscale--;
        if (zscale > 0) zscale--;
        if (bscale > 0) bscale--;
    } else if (frame > 1400 + 789) {
        if (bscale > 0) bscale -= 8;
        if (bscale < 0) bscale = 0;
    } else {
        if (bscale < 180) bscale += 2;
        else bscale = 180;
    }
    if (bscale > xscale)
        lightshift = 10;
} else {
    if (frame < 640 + 70) {
        yposa += 31;
        ypos += yposa / 40;
        if (ypos > -300) {
            ypos -= yposa / 40;
            yposa = -yposa * boingm / boingd;
            boingm += 2;
            boingd++;
        }
        if (ypos > -900 && yposa > 0) {
            jello = (ypos + 900) * 5 / 3;
            jelloa = 0;
        }
    } else {
        if (ypos > -2800) ypos -= 16;
        else if (ypos < -2800) ypos += 16;
    }
    /* 原作 MAIN.C / C++ 版 (GLENZ_MAIN.cpp 1445 行) と同じ式・同じ係数 */
    yscale = xscale = 120 + jello / 30;
    zscale = 120 - jello / 30;
    a = jello;
    jello += jelloa;
    if ((a < 0 && jello > 0) || (a > 0 && jello < 0))
        jelloa = jelloa * 5 / 6;
    a = jello / 20;
    jelloa -= a;
}

/* パレットは frame>800 でも進める (2069 以降のフェードアウト)。
 * オリジナルでもこのブロックは if(frame>800) の外にある。 */
palette_step();     /* パレットのフェード/ワイプ/赤切り替え */

}

/* ---- 背景パレットのタイムライン (MAIN.C 538-590) ----
 * 700-764: フェードアウト / 765-789: ワイプ (bgpic を 2 行ずつ消す) と
 * 赤パレットの生成 / 790-794: 赤パレット表示 / 2069 以降: フェードアウト。 */
static void palette_step(void) {
    int i, b;

    fade16 = 64;
    if (frame > 1280 + 789) {
        b = 1280 + 789 + 64 - frame;
        if (b < 0) b = 0;
        fade16 = b;                 /* 小のパレットも同じ係数でフェード */
        for (i = 0; i < 48; i++)
            pal6[i] = (u8)(back6[i] * b / 64);
    } else if (frame > 700) {
        if (frame < 765) {
            b = 764 - frame;
            if (b < 0) b = 0;
            for (i = 0; i < 48; i++)
                pal6[i] = (u8)(back6[i] * b / 64);
        } else if (frame < 790) {
            int y = 150 + (frame - 765) * 2;
            if (y >= 0 && y + 1 < GH) {
                for (i = 0; i < GW; i++) {
                    bgpic[y * GW + i] = 0;
                    bgpic[(y + 1) * GW + i] = 0;
                }
                /* 原作 MAIN.C:555-556 は bgpic と同時に VRAM の同じ 2 行も
                 * 0 にする (memset(vram+y*320,0,640))。以前は bgpic だけを
                 * 消し、画面は次の描画の全面コピーまで残っていた。1 描画で
                 * 2 歩進むと、消えていないロゴの行が 790 の赤パレットで
                 * 一瞬赤く見えた (2026-11-16 ユーザー報告)。 */
                {
                    u8 *v = (u8 *)out200 + (s32)(GY + y) * SW;
                    for (i = 0; i < 2 * SW; i++)
                        v[i] = 0;
                }
                gz_full = 1;            /* この描画は全体を写して変換する */
                bg_changed = 1;
            }
            if (frame > 785) {
                for (int k = 0; k < 16; k++) {
                    int r = 0, g = 0, bb = 0;
                    if (k & 1) r += 10;
                    if (k & 2) r += 30;
                    if (k & 4) r += 20;
                    if (k & 8) { r += 16; g += 16; bb += 16; }
                    if (r > 63) r = 63;
                    if (g > 63) g = 63;
                    if (bb > 63) bb = 63;
                    back6[k * 3 + 0] = (u8)r;
                    back6[k * 3 + 1] = (u8)g;
                    back6[k * 3 + 2] = (u8)bb;
                }
                /* 参照画像に合わせた赤ランプ (1..7 番)。
                 * オリジナルの式では大きい赤が使う 2/4 番が 121/81 と
                 * 暗く、参照の R 90..210 に合わない。このフェーズで
                 * 1..7 番を使うのは大きい赤の面だけなので置き換える
                 * (背景の 0 番と 8..15 番は元の式のまま)。 */
                {
                    static const u8 redramp[7 * 3] = {
                        15,  3,  3,   /* 1: 輪郭 (色 0 の単独被覆) */
                        27,  4,  4,   /* 2: visible0 + hidden2 */
                        37,  5,  5,   /* 3: visible1 + hidden2 */
                        22,  4,  4,   /* 4: visible0 + hidden4 */
                        49,  6,  6,   /* 5: visible1 + hidden4 */
                        54,  6,  6,   /* 6: 4 面の重なり */
                        59,  7,  7,   /* 7 */
                    };
                    for (int k = 1; k < 8; k++) {
                        back6[k * 3 + 0] = redramp[(k - 1) * 3 + 0];
                        back6[k * 3 + 1] = redramp[(k - 1) * 3 + 1];
                        back6[k * 3 + 2] = redramp[(k - 1) * 3 + 2];
                    }
                }
            }
        } else {
            for (i = 0; i < 48; i++)
                pal6[i] = back6[i];
            bg_zero = 1;        /* ワイプ完了: bgpic は全 0 */
        }
    } else {
        for (i = 0; i < 48; i++)
            pal6[i] = back6[i];
    }
}

/* ---- 初期化 ---- */
void glenz_init(void) {
    const u8 *s = g_glenz_sin;
    int i;

    for (i = 0; i < 3600; i++)
        sin_tab[i] = (s16)((s[i * 2] << 8) | s[i * 2 + 1]);

    for (i = 0; i < 16 * 3; i++) {
        int v = g_glenz_pal ? ((int)g_glenz_pal[i] * 63) / 255 : 0;
        backpal[i] = g_glenz_pal ? g_glenz_pal[i] : 0;
        basepal[i] = v;
        back6[i] = (u8)v;
        pal6[i] = (u8)v;
    }
    for (i = 0; i < 64; i++)
        rolcol[i] = 0;
    for (i = 0; i < 32; i++)
        rolused[i] = 0;

    /* zoomer2: 前パート (BEGLOGO) のタイトル画面を chunky に変換する。
     * 形式は image.c と同じ (ヘッダ 0x310、パレット 0x10)。 */
    {
        const u8 *img = g_image_data;
        int ok = 0;
        if (img) {
            u16 w = (u16)((img[4] << 8) | img[5]);
            u16 h = (u16)((img[6] << 8) | img[7]);
            if (w == 320 && h == 256) {
                const u8 *src = img + 0x310;
                for (int y = 0; y < 256; y++) {
                    for (int x = 0; x < 320; x++) {
                        int b = x >> 3;
                        int bit = 7 - (x & 7);
                        u8 v = 0;
                        for (int pl = 0; pl < 8; pl++)
                            if ((src[pl * (40 * 256) + y * 40 + b] >> bit) & 1)
                                v |= (u8)(1 << pl);
                        zoom2_title[y * 320 + x] = v;
                    }
                }
                for (int i2 = 0; i2 < 768; i2++)
                    zoom2_pal[i2] = img[0x10 + i2];
                ok = 1;
            }
        }
        if (!ok) {
            for (int i2 = 0; i2 < 320 * 256; i2++)
                zoom2_title[i2] = 0;
            for (int i2 = 0; i2 < 768; i2++)
                zoom2_pal[i2] = 0;
        }
        /* 原作 BEG.C:25-34: パレット 0..254 をタイトルにし、255 は黒。
         * ZOOMER.C:48,57 の帯は memset(v,255,...) なので、帯は黒。 */
        zoom2_pal[255 * 3 + 0] = 0;
        zoom2_pal[255 * 3 + 1] = 0;
        zoom2_pal[255 * 3 + 2] = 0;
    }

    zoom2_zy = zoom2_zya = zoom2_zy2 = 0;
    zoom2_frame = 0;
    zoom2_done = 0;
    zoom2_track_top = 0;
    zoom2_track_bottom = 256;
    zoom2_track_valid = 0;

    mf0 = np_zframe;            /* 原作 dis_setmframe(0) */
    mf_phase = 0;
    g_started = 0;
    g_finished = 0;
    frame = 0;
    rx = ry = rz = 0;
    ypos = -9000;
    yposa = 0;
    jello = jelloa = 0;
    xscale = yscale = zscale = 120;
    bscale = 0;
    oxp = oyp = ozp = oxb = oyb = ozb = 0;
    boingm = 6;
    boingd = 7;
    lightshift = 9;
    fade16 = 64;
    have_bg = 0;
    bg_full = 0;
    bg_changed = 0;
    bg_zero = 0;
    zyy = 0;
    zya = 0;
    zoom_done = 0;
    tl_acc = 0;
    g_tl_steps = 1;
    dirty_x0 = dirty_y0 = 0x7fff;
    dirty_x1 = dirty_y1 = -1;
    c2p_track_reset(&gz_trk);

    /* デバッグ用: 開始フレームまでタイムラインだけ進める
     * (DH0:glenz.cfg。make-hdf.ps1 が $env:M2_GLENZ_FRAME を書く) */
    if (g_glenz_start_frame > 0) {
        while (zoom_step_frame())
            ;                       /* ズームは最後まで進める */
        for (i = 0; i < GW * GH; i++)
            bgpic[i] = 0;
        zoom_draw(bgpic, GW);
        zoom_done = 1;
        have_bg = 1;
        for (i = 0; i < g_glenz_start_frame; i++)
            timeline_step();
    }
}

int glenz_finished(void) {
    return g_finished;
}

static int glenz_draw(u8 *chunky) {
    int i, a, b;
    u8 *dst256 = chunky;            /* 最終出力 (320x256) */

    chunky = (u8 *)out200;          /* 描画は原作どおり 320x200 */
    g_buf = chunky;

    /* オリジナルは 70Hz で 1 フレームずつ進む。こちらは 50fps なので
     * 1 描画あたり 70/50 = 1.4 ステップ進めて速度を合わせる。 */
    /* 経過 VBL から進める (描画が 25fps に落ちても 70 ステップ/秒を保つ)。
     * 実測: 描画 1 回ごとに 1.4 を足す旧実装では 0.71 ステップ/VBL
     * (35/秒) しか進んでいなかった。 */
    /* 原作 GLENZ/MAIN.C:290-291:
     *   while(!dis_exit() && dis_musplus()<-19) ;  dis_setmframe(0);
     * 待っている間は zoomer2 を進めない (BEGLOGO のタイトルのまま)。 */
    if (!g_started) {
        if (dis_musplus() < -19) {
            tl_last_vbl = video_frame_vbl();
        } else {
            g_started = 1;
            mf0 = np_zframe;
        }
    }
    {
        u32 now = video_frame_vbl();
        u32 el = now - tl_last_vbl;
        int steps = 0;
        if (tl_last_vbl == 0)
            el = 1;
        tl_last_vbl = now;
        if (el > 10)
            el = 10;
        tl_acc += el * (70u * 65536u / 50u);
        while (tl_acc >= 65536u) {
            tl_acc -= 65536u;
            steps++;
        }
        g_tl_steps = g_started ? steps : 0;
    }
    /* 原作 MAIN.C:409-411: while(frame<7000) { a=dis_musplus();
     * if(a<0 && a>-16) break; ... } */
    if (mf_phase == 2 && !g_finished) {
        int mp = dis_musplus();
        if ((mp < 0 && mp > -16) || frame >= 7000)
            g_finished = 1;
    }

    /* ---- zoomer2: タイトルをグレーアウトしながら黒い帯で閉じる
     * (GLENZ/ZOOMER.C:18-81、BEG.C:25-34 でパレット 255 = 黒) ---- */
    if (!zoom2_done) {
        for (i = 0; i < g_tl_steps && !zoom2_done; i++) {
            if (zoom2_zy == 260) {
                zoom2_done = 1;
                break;
            }
            zoom2_zya++;
            zoom2_zy += zoom2_zya / 4;
            if (zoom2_zy > 260)
                zoom2_zy = 260;
            zoom2_zy2 = 125 * zoom2_zy / 260;
            zoom2_frame++;
        }

        /* 前パートのタイトル画面を出し、黒い帯 (index 255) を重ねる */
        for (i = 0; i < 320 * 256; i++)
            dst256[i] = zoom2_title[i];
        {
            /* 原作の 400 ライン座標 -> 256 ライン */
            /* 原作は前フレームの帯が VRAM に残る。こちらは毎フレーム
             * 描き直すので、閉じた全範囲 (0..zy / 399-zy2..399) を塗る。 */
            int y0 = 0;
            int y1 = zoom2_zy * 256 / 400;
            int b0 = (399 - zoom2_zy2) * 256 / 400;
            int b1 = 255;
            int top_end = y1 + 1;
            for (int y = y0; y <= y1 && y < 256; y++) {
                if (y < 0) continue;
                u8 *d = dst256 + (s32)y * 320;
                for (int x = 0; x < 320; x++)
                    d[x] = 255;
            }
            for (int y = b0; y <= b1 && y < 256; y++) {
                if (y < 0) continue;
                u8 *d = dst256 + (s32)y * 320;
                for (int x = 0; x < 320; x++)
                    d[x] = 255;
            }
            /* ZOOMER.C の黒帯は毎フレーム外側へ広がる。初回の各裏画面は
             * c2p_track_flush が全体変換し、それ以後は新たに覆った行だけ変換。 */
            if (!zoom2_done) {
                if (zoom2_track_valid) {
                    if (top_end > zoom2_track_top)
                        c2p_track_add(&gz_trk, zoom2_track_top, top_end, 0, 320);
                    if (b0 < zoom2_track_bottom)
                        c2p_track_add(&gz_trk, b0, zoom2_track_bottom, 0, 320);
                }
                zoom2_track_top = top_end;
                zoom2_track_bottom = b0;
                zoom2_track_valid = 1;
                gz_full = 0;
            }
        }

        /* パレット 0..127 をグレー 30/63 へ (ZOOMER.C:59-68)。
         * 255 は黒のままなので、閉じる帯は黒。 */
        {
            int c = zoom2_frame;
            if (c > 32) c = 32;
            int b = 32 - c;
            for (i = 0; i < 128 * 3; i++)
                zoom2_work[i] = (u8)((zoom2_pal[i] * b + 121 * c) / 32);
            for (i = 128 * 3; i < 768; i++)
                zoom2_work[i] = zoom2_pal[i];
            video_set_palette(zoom2_work);
        }
        if (zoom2_done) {
            /* 原作 ZOOMER.C:70-80: まずパレット 0 を黒にしてから
             * 画面を 0 でクリアする。パレット 0 が灰色のままだと
             * クリアした画面が灰色に光る。 */
            zoom2_work[0] = 0;
            zoom2_work[1] = 0;
            zoom2_work[2] = 0;
            video_set_palette(zoom2_work);
            for (i = 0; i < 320 * 256; i++)
                dst256[i] = 0;
        }
        return 0;
    }

    /* ---- 背景ズーム (本編前の約 188 フレーム) ----
     * オリジナルは本編の前に fc 画像を縦に引き伸ばして最下部へ落とす。 */
    if (!zoom_done) {
        for (i = 0; i < g_tl_steps && !zoom_done; i++) {
            if (!zoom_step_frame()) {
                zoom_done = 1;          /* 収束 */
                break;
            }
        }
        for (int ry = 0; ry < SH; ry++) {
            u8 *d = chunky + (s32)ry * SW;
            for (int x = 0; x < SW; x++)
                d[x] = 0;
        }
        zoom_draw(chunky + (s32)GY * SW, SW);
        {
            const u8 *src = chunky + (s32)GY * SW;
            for (i = 0; i < GW * GH; i++)
                bgpic[i] = src[i];
        }
        for (i = 0; i < 16 * 3; i++)
            gpal[i] = (u8)((pal6[i] << 2) | (pal6[i] >> 4));
        for (i = 16 * 3; i < 256 * 3; i++)
            gpal[i] = 0;
        if (zoom_done) {
            have_bg = 1;
            bg_full = 0;
        }
        video_set_palette(gpal);
        stretch200((const u8 *)out200, dst256);
        return 0;
    }

    /* 原作 MAIN.C:353,400: FC ロゴを出したまま音楽フレーム 300 を待ち、
     * 333 で GLENZ 本体を開始する。待たずに始めると 14 秒早く終わり、
     * トンネルまで黒画面になる (2026-11-14 のユーザー報告)。 */
    if (mf_phase < 2) {
        int mf = (int)(u16)(np_zframe - mf0);
        if (mf >= 333) {
            mf_phase = 2;
        } else {
            if (mf >= 300)
                mf_phase = 1;
            for (i = 0; i < GW * GH; i++)
                chunky[i] = bgpic[i];
            for (i = 0; i < 16 * 3; i++)
                gpal[i] = (u8)((pal6[i] << 2) | (pal6[i] >> 4));
            for (i = 16 * 3; i < 256 * 3; i++)
                gpal[i] = 0;
            video_set_palette(gpal);
            stretch200((const u8 *)out200, dst256);
            return 0;
        }
    }

    /* デバッグ: 1 描画あたりの VBL 数 (bit64 の表示用) */
    {
        u32 now = vblank_frames();
        if (last_vbl) {
            int dv = (int)(now - last_vbl);
            if (dv > 0 && dv < 10) {
                vbl_acc += dv;
                vbl_n++;
            }
        }
        last_vbl = now;
    }

    /* 背景: 最初の 1 回だけ全体、以降は前フレームで描いた矩形だけ戻す。
     * ワイプで bgpic が変わったフレームは全体をコピーし直す。 */
    if (bg_changed) {
        bg_changed = 0;
        bg_full = 0;
    }
    gz_full = !bg_full || g_glenz_diag;
    gz_r[0][0] = dirty_x0; gz_r[0][1] = dirty_x1;   /* 戻す矩形 (前フレームの描画) */
    gz_r[0][2] = dirty_y0; gz_r[0][3] = dirty_y1;
    if (!bg_full) {
        if (bg_zero) {
            fill_zero(chunky, SH * SW);
        } else {
            u8 *d = chunky;
            int n0 = GY * SW;
            for (i = 0; i < n0; i++)
                *d++ = 0;
            for (i = 0; i < GW * GH; i++)
                *d++ = bgpic[i];
            for (i = 0; i < (SH - GY - GH) * SW; i++)
                *d++ = 0;
        }
        bg_full = 1;
    } else if (dirty_y1 >= dirty_y0 && dirty_x1 >= dirty_x0) {
        int y0 = dirty_y0, y1 = dirty_y1;
        int x0 = dirty_x0, x1 = dirty_x1;
        if (y0 < GY) y0 = GY;
        if (y1 > GY + GH - 1) y1 = GY + GH - 1;
        if (x0 < 0) x0 = 0;
        if (x1 > SW - 1) x1 = SW - 1;
        for (int y = y0; y <= y1; y++) {
            u8 *dst = chunky + (s32)y * SW;
            if (bg_zero) {
                fill_zero(dst + x0, x1 - x0 + 1);
                continue;
            }
            const u8 *src = bgpic + (s32)(y - GY) * GW;
            int x = x0;
            while (x <= x1 && ((u32)(dst + x) & 3)) {
                dst[x] = src[x];
                x++;
            }
            if (x + 4 <= x1 + 1) {
                u32 *d32 = (u32 *)(dst + x);
                const u32 *s32p = (const u32 *)(src + x);
                while (x + 3 <= x1) {
                    *d32++ = *s32p++;
                    x += 4;
                }
            }
            while (x <= x1) {
                dst[x] = src[x];
                x++;
            }
        }
    }
    dirty_x0 = 0x7fff;
    dirty_y0 = 0x7fff;
    dirty_x1 = -1;
    dirty_y1 = -1;
    /* パレット: 0..15 = 背景色。タイムラインでフェード/赤へ切り替わる */
    for (i = 0; i < 16 * 3; i++)
        gpal[i] = (u8)((pal6[i] << 2) | (pal6[i] >> 4));

    for (i = 0; i < g_tl_steps; i++)
        if (!(g_glenz_diag & 4))     /* 診断時はタイムラインを止める */
            timeline_step();


    /* ---- 描画 ---- */
    group_init();
    if (xscale > 4 && !(g_glenz_diag & 8)) {       /* bit3: 小を描かない */
        draw_object(pts_main, poly_main, rx, ry, rz,
                    xscale, yscale, zscale,
                    oxp, ypos + 1500 + oyp, ZPOS + ozp, 0);
    }
    if (frame > 800 && bscale > 4 && !(g_glenz_diag & 16)) {  /* bit4: 大を描かない */
        draw_object(pts_big, poly_big,
                    ang(3600 - rx / 3), ang(3600 - ry / 3), ang(3600 - rz / 3),
                    bscale, bscale, bscale,
                    oxb, ypos + 1500 + oyb, ZPOS + ozb, 1);
    }
    if (!(g_glenz_diag & 1))
        group_draw();

    /* デバッグ: bit6 (64) のときだけ、左上に diag 値と frame を色で出す
     * (14 番 = diag*4, 13 番 = (frame & 63)*4 の赤成分) */
    if (g_glenz_diag & 64) {
        u8 *r0 = chunky + (s32)GY * SW;
        u8 *r1 = chunky + (s32)(GY + 2) * SW;
        u8 *r2 = chunky + (s32)(GY + 4) * SW;
        for (int x = 0; x < 24; x++) {
            r0[x] = 0;
            r1[x] = 0;
            r2[x] = 0;
        }
        r0[4] = 14;
        r1[4] = 13;
        gpal[14 * 3 + 0] = (u8)(g_glenz_diag * 4);
        gpal[14 * 3 + 1] = 0;
        gpal[14 * 3 + 2] = 0;
        gpal[13 * 3 + 0] = (u8)((frame & 63) * 4);
        gpal[13 * 3 + 1] = 0;
        gpal[13 * 3 + 2] = 0;
        /* 1 描画あたりの VBL 数×4 (4=50fps, 8=25fps) を緑の点で出す */
        if (vbl_n > 0) {
            int avg4 = (vbl_acc * 4 + vbl_n / 2) / vbl_n;
            if (avg4 > 16) avg4 = 16;
            for (int x = 0; x < avg4; x++)
                r2[4 + x] = 15;
            if (vbl_n >= 64) {
                vbl_acc = 0;
                vbl_n = 0;
            }
        }
    }

    /* 診断色: 15 番を緑にする (15 は背景画像では未使用) */
    if (g_glenz_diag & 32) {
        /* デバッグ: 1..14 をビットごとの色にして、使われた index を見る */
        for (i = 1; i < 15; i++) {
            gpal[i * 3 + 0] = (u8)((i & 1) ? 255 : 0);
            gpal[i * 3 + 1] = (u8)((i & 2) ? 255 : 0);
            gpal[i * 3 + 2] = (u8)((i & 4) ? 255 : 0);
        }
    }
    if (g_glenz_diag)
        gpal[15 * 3 + 1] = 255, gpal[15 * 3 + 0] = 0, gpal[15 * 3 + 2] = 0;

    video_set_palette(gpal);
    gz_r[1][0] = dirty_x0; gz_r[1][1] = dirty_x1;   /* 今回描いた矩形 */
    gz_r[1][2] = dirty_y0; gz_r[1][3] = dirty_y1;
    if (gz_full) {
        stretch200((const u8 *)out200, dst256);
    } else {
        /* 変わったのは戻した矩形と描いた矩形の中だけ。そこだけ写して
         * 変換対象に加える (out200 の行 y は dst256 の行 y+28)。 */
        for (int k = 0; k < 2; k++) {
            int x0 = gz_r[k][0], x1 = gz_r[k][1], y0 = gz_r[k][2], y1 = gz_r[k][3];
            if (x1 < x0 || y1 < y0)
                continue;
            if (x0 < 0) x0 = 0;
            if (x1 > SW - 1) x1 = SW - 1;
            if (y0 < 0) y0 = 0;
            if (y1 > SH - 1) y1 = SH - 1;
            if (x1 < x0 || y1 < y0)
                continue;
            for (int y = y0; y <= y1; y++) {
                const u8 *src = (const u8 *)out200 + y * SW;
                u8 *d = dst256 + (y + 28) * 320;
                for (int x = x0; x <= x1; x++)
                    d[x] = src[x];
            }
            c2p_track_add(&gz_trk, y0 + 28, y1 + 29, x0, x1 + 1);
        }
    }
    return 0;
}

int glenz_render(u8 *chunky, u8 *planes, int width, int height) {
    (void)width;
    (void)height;
    gz_full = 1;                    /* 本編以外の段階は全体 */
    glenz_draw(chunky);
    if (gz_full)
        c2p_track_add(&gz_trk, 0, 256, 0, 320);
    c2p_track_flush(&gz_trk, chunky, planes);
    return 1;
}
