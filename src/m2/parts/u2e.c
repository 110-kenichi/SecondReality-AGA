/*
 * M4 シーン: U2A (Vector Part I - スペースバトル)
 *
 * オリジナル VISU/C/U2A.C (シーン再生) + VISU/VISU.C (vis_drawobject) +
 * ACALC.ASM (calc_rotate / calc_nrotate / calc_project / calc_applyrmatrix)
 * + ADRAW.ASM (draw_polylist / calclight) の移植。
 *
 * 背景 U2ABG.UH は 320x200。256 行の画面では中央 (28..227 行) に置き、
 * オブジェクトのビューポートはオリジナルの y=25..174 を +28 した
 * 53..202 行にクリップする。
 *
 * 投影: オリジナルは fov=34 固定。
 *   avistan[68] = 577 (256/tan(23.9°)) なので
 *   projmulx = 160*577/256 = 360、projaddx = 159、
 *   projaddy = (25+174)/2 + 28 = 127。
 *
 * オブジェクトの回転行列は UNIT=16384 の固定小数点。頂点は s32。
 * 描画はフラットシェーディング (法線と光源の内積) + スキャンライン塗り。
 * 近平面 (z < 512) は三角形をクリップし、画面端は 2D クリップする。
 */
#include "scenes.h"
#include "visu_clip.h"
#include <string.h>
#include "../framework/video.h"
#include "../framework/vblank.h"
#include "../framework/mul64.h"   /* smul64: __muldi3 を使わない 64 ビット積 */

typedef long long s64;
typedef unsigned long long u64;

extern const u8 *g_u2e_pal;
extern const u8 *g_u2e_obj;
extern const u8 *g_u2e_pal;
extern const u8 *g_u2e_script;
extern const u8 *g_u2e_inst;
extern int prof_on;

#define U2E_W        320
#define U2E_H        256
#define U2E_BG_TOP   0           /* 原作 320x200 を 1.28 倍して全 256 行に表示 */
#define U2E_VIEW_TOP 32          /* 原作 y=25 → 25*32/25 = 32 */
#define U2E_VIEW_BOT 222         /* 原作 y=174 → 174*32/25 = 222.7 */

#define PROJ_ADDX  159
#define PROJ_ADDY  127          /* 原作 (25+174)/2=99 → 99*32/25 = 126.7 */
#define PROJ_ZMIN  512          /* U2E.C: vid_window(0,319,25,174,512,9999999) */
#define PROJ_ZMAX  9999999

/* _vid_cameraangle() 用の視角テーブル
 * (reference/SecondReality/VISU/AVISTAN.INC, 256 words = 90 度) */
static const u16 avistan[256] = {
    32767, 32767, 20859, 13905, 10428,  8341,  6950,  5956,  5210,  4631,  4166,  3787,
     3470,  3202,  2972,  2773,  2599,  2445,  2308,  2185,  2075,  1975,  1884,  1801,
     1725,  1655,  1591,  1531,  1475,  1423,  1374,  1329,  1286,  1246,  1209,  1173,
     1140,  1108,  1077,  1049,  1022,   996,   971,   947,   925,   903,   882,   862,
      843,   825,   808,   791,   774,   759,   744,   729,   715,   701,   688,   675,
      663,   651,   640,   628,   618,   607,   597,   587,   577,   568,   558,   549,
      541,   532,   524,   516,   508,   500,   493,   486,   478,   471,   465,   458,
      451,   445,   439,   433,   427,   421,   415,   409,   404,   398,   393,   388,
      383,   378,   373,   368,   363,   358,   354,   349,   345,   340,   336,   332,
      328,   323,   319,   315,   311,   308,   304,   300,   296,   293,   289,   285,
      282,   278,   275,   272,   268,   265,   262,   259,   256,   252,   249,   246,
      243,   240,   237,   234,   232,   229,   226,   223,   220,   218,   215,   212,
      210,   207,   204,   202,   199,   197,   194,   192,   189,   187,   185,   182,
      180,   177,   175,   173,   171,   168,   166,   164,   162,   159,   157,   155,
      153,   151,   149,   147,   145,   142,   140,   138,   136,   134,   132,   130,
      128,   126,   124,   123,   121,   119,   117,   115,   113,   111,   109,   107,
      106,   104,   102,   100,    98,    96,    95,    93,    91,    89,    88,    86,
       84,    82,    81,    79,    77,    75,    74,    72,    70,    69,    67,    65,
       64,    62,    60,    59,    57,    55,    54,    52,    50,    49,    47,    46,
       44,    42,    41,    39,    37,    36,    34,    33,    31,    29,    28,    26,
       25,    23,    22,    20,    18,    17,    15,    14,    12,    11,     9,     7,
        6,     4,     3,     1,
};

/* vid_cameraangle(fov): projmulx = (clipx_max - projaddx) * avistan[i] >> 8
 * 画面は 320 幅、中心 159 → (319-159)=160。
 *
 * 原作 AVID.ASM:66-79:
 *   bx = fov >> 1 → clamp 512..16383 → bx = (bx >> 5) & ~1  (bx は
 *   avistan の「バイトオフセット」; コメントも "word index (0..255)*2")
 *   movzx ecx,word ptr ds:_avistan[bx]
 * したがってテーブルの word 添字は bx >> 1。
 * SecondRealityPlusPlus VISU.cpp:461-476 も `avistan[bx >> 1]` で一致。
 * U2E の fov は全 1801 フレーム一定 (28<<8=7168):
 *   bx = (7168>>1)>>5 & ~1 = 112 → word 添字 56 → avistan[56]=715
 *   projmulx = 160*715>>8 = 446
 *   projmuly = projmulx * projaspect(225)>>8 = 391
 *     (m11_init: AVIDM1.ASM:67 で projaspect=225)
 */
static int proj_mulx = 446;
static int proj_muly = 391;
static u32 u2e_fov = 7168;

static void camera_angle(u32 a) {
    u32 half = a >> 1;
    if (half < 8 * 64)
        half = 8 * 64;
    if (half >= 16384)
        half = 16383;
    int idx = (int)((half >> 5) & ~1u);    /* バイトオフセット */
    idx >>= 1;                             /* avistan の word 添字 */
    if (idx > 255)
        idx = 255;
    proj_mulx = (160 * (int)avistan[idx]) >> 8;
    /* projaspect=225 (mode 11) の 200 行基準の倍率に、320x200→320x256 の
     * 縦 1.28 倍 (32/25) を掛けて 256 行基準にする。
     * 原作の 320x200 を 4:3 で見たときと同じ縦横比になる (GLENZ と同じ扱い)。 */
    proj_muly = (((proj_mulx * 225) >> 8) * 32) / 25;
}

/* ---- 消える条件の切り分け (U2E_DEBUG_CYCLE / 一時ビルド専用) ----
 * U2E_STOP_FRAME で表示を固定したまま、2 秒ごとに「無効化する条件」を
 * 切り替える。上部の白いバーの本数 = モード番号 + 1。
 *
 *   1 本: 通常 (何も無効化しない)
 *   2 本: スクリプト OFF を無視
 *   3 本: vf_and のオブジェクトスキップを無効
 *   4 本: バックフェイスカリングを無効
 *   5 本: ポリゴン単位の anded スキップを無効
 *   6 本: 3D 近平面クリップを無効
 *   7 本: 2D 画面クリップを無効
 *   8 本: オブジェクトの距離ソートを無効 (インデックス順)
 *   9 本: オブジェクトの距離ソートを逆順
 *  10 本: 1〜6 をすべて無効
 */
#ifdef U2E_DEBUG_CYCLE
static int u2e_dbg_mode;
static u32 u2e_dbg_t0;
#define DBG_IS(n)   (u2e_dbg_mode == (n))
#define DBG_OFF(n)  (u2e_dbg_mode == (n) || u2e_dbg_mode == 9)
#endif

/* デバッグ計測 (main からも参照可能) */
volatile int u2e_dbg_objs;      /* 描画したオブジェクト数 */
volatile int u2e_dbg_skip;      /* vf_and でスキップした数 */
volatile int u2e_dbg_on;        /* on のオブジェクト数 */
volatile int u2e_dbg_polys;     /* 描画したポリゴン数 */
volatile int u2e_dbg_mul;       /* 現在の projmulx */
volatile int u2e_dbg_muly;      /* 現在の projmuly */

#define VF_UP    1
#define VF_DOWN  2
#define VF_LEFT  4
#define VF_RIGHT 8
#define VF_NEAR  16
#define VF_FAR   32

#define MAXV 320
#define MAXN 320

typedef struct { s32 m[9]; s32 x, y, z; } rmat;

typedef struct {
    u16 vnum, nnum, tnum, center;
    u16 kind;          /* 1=ground(name[1]==_ ) 2=s01 */
    const u8 *vdata;   /* vnum * 16 bytes (s32 x,y,z,normal) */
    const u8 *ndata;   /* nnum * 6 bytes  (s16 x,y,z) */
    const u8 *tdata;   /* polygons: u8 n, u8 flags, u16 color, u16 nrm, n*u16 */
    const u8 *ordata;  /* u16 count, then (u16 sortv, u16 n, n*u16 idx) */
    u16 ordnum;
} u2e_obj;

typedef struct {
    const u2e_obj *obj;
    rmat r0, r;
    int on;
    s32 dist;
} u2e_inst;

static u2e_obj objs[64];
static u2e_inst co[64];              /* co[0] = カメラ (obj なし) */
static const u8 *anim_sp;
static int u2e_done;
static int conum;
static int u2e_frame;
static s32 poly_z[600];
static int poly_order[600];
static const u8 *poly_pp[600];
static int clip_ax[64], clip_ay[64], clip_ac[64];
static int clip_bx[64], clip_by[64], clip_bc[64];
static int tri_order[600];
static s32 tri_z[600];
static u32 u2e_last_vbl;
static u32 u2e_frac;

/* ---- 開始時のホワイトアウト (原本 VISU/C/U2E.C:273-350) ----
 * 原本は前パート (JPLOGO) の VGA 画面と DAC パレットをそのまま使い、
 *   33 フレーム: 3..761 バイトを +2/63 で白へ (U2E.C:285-295)
 *   16 フレーム: 白を保持                  (U2E.C:297-300)
 *   fadeset()  : 白い枠を描く              (U2E.C:145-183,302-313)
 *   16 フレーム: 保持                      (U2E.C:315-318)
 *   33 フレーム: 色 1..252 を -2/63 で黒へ、
 *                253/254 を +2/63 で白へ  (U2E.C:320-335)
 *   vid_init(11) + シーンのパレット        (U2E.C:336-350)
 * の順に切り替える。70Hz の 1 フレームを 7/5 VBL で 50Hz に換算する。
 * 合計 99 フレーム = 70.7 VBL。 */
extern const u8 *g_prev_frame;
extern const u8 *g_prev_pal;

#define INTRO_Y        28      /* JPLOGO と同じ 320x200 の表示位置 */
#define INTRO_WHITEIN  33
#define INTRO_HOLD1    16
#define INTRO_HOLDSET  16
#define INTRO_FADEDOWN 33
#define INTRO_FADESET  (INTRO_WHITEIN + INTRO_HOLD1)
#define INTRO_FADEOUT  (INTRO_FADESET + INTRO_HOLDSET)
#define INTRO_TOTAL    (INTRO_FADEOUT + INTRO_FADEDOWN)

static int intro_done;
static int intro_f;
static u32 intro_last_vbl;
static u32 intro_acc;
static u8 intro_img[320 * 200];
static u8 intro_pal[768];

static s32 rv[MAXV][3];             /* 回転済み頂点 */
static s16 rn[MAXN][3];             /* 回転済み法線 */
static s32 px[MAXV], py[MAXV];      /* 投影座標 */
static u8  pvf[MAXV];
static u8 *g_dst;

/* ---- バイト列 (ビッグエンディアン) の読み出し ---- */
static inline s32 rd32(const u8 *p) {
    return (s32)(((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]);
}
static inline s16 rd16(const u8 *p) {
    return (s16)(((u16)p[0] << 8) | p[1]);
}

static void clear_rmat(rmat *r) {
    for (int i = 0; i < 9; i++)
        r->m[i] = 0;
    r->x = r->y = r->z = 0;
}

/* ---- 行列 1 行分の回転 (UNIT=16384) ---- */
static inline s32 rot_row(const rmat *r, int row, s32 x, s32 y, s32 z) {
    const s32 *m = &r->m[row * 3];
    s64 s = smul64(m[0], x) + smul64(m[1], y) + smul64(m[2], z);
    return (s32)(s >> 14);
}

static void parse_objects(void) {
    const u8 *p = g_u2e_obj;
    int n = ((u16)p[0] << 8) | p[1];
    p += 2;
    for (int i = 0; i < n && i < 64; i++) {
        u2e_obj *o = &objs[i];
        o->vnum   = ((u16)p[0] << 8) | p[1];
        o->nnum   = ((u16)p[2] << 8) | p[3];
        o->tnum   = ((u16)p[4] << 8) | p[5];
        o->center = ((u16)p[6] << 8) | p[7];
        o->kind = ((u16)p[8] << 8) | p[9];
        p += 10;
        o->vdata = p; p += o->vnum * 16;
        o->ndata = p; p += o->nnum * 6;
        o->tdata = p;
        {
            const u8 *q = p;
            for (int t = 0; t < o->tnum; t++)
                q += 6 + 2 * q[0];
            o->ordata = q;
            o->ordnum = ((u16)q[0] << 8) | q[1];
            q += 2;
            for (int k = 0; k < o->ordnum; k++) {
                u16 nn = ((u16)q[2] << 8) | q[3];
                q += 4 + 2 * nn;
            }
            p = (u8 *)q;
        }
    }
}

/* ---- カメラ行列の適用: r = cam * r0 ---- */
static void apply_camera(u2e_inst *ci) {
    const rmat *cam = &co[0].r0;
    const rmat *r0 = &ci->r0;
    rmat *r = &ci->r;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            s64 s = smul64(cam->m[i * 3 + 0], r0->m[0 * 3 + j])
                  + smul64(cam->m[i * 3 + 1], r0->m[1 * 3 + j])
                  + smul64(cam->m[i * 3 + 2], r0->m[2 * 3 + j]);
            r->m[i * 3 + j] = (s32)(s >> 14);
        }
    }
    r->x = rot_row(cam, 0, r0->x, r0->y, r0->z) + cam->x;
    r->y = rot_row(cam, 1, r0->x, r0->y, r0->z) + cam->y;
    r->z = rot_row(cam, 2, r0->x, r0->y, r0->z) + cam->z;
}

static s32 center_z(const u2e_inst *ci) {
    const u8 *p = ci->obj->vdata + ci->obj->center * 16;   /* vlist = 16 バイト */
    s32 x = rd32(p), y = rd32(p + 4), z = rd32(p + 8);
    return rot_row(&ci->r, 2, x, y, z) + ci->r.z;
}

/* ---- アニメーションストリーム ---- */
static s32 lsget(u32 f) {
    int n = f & 3;
    if (n == 0)
        return 0;
    /* 原本 U2E.C/U2A.C: case 3 は 4 バイト読む (最後のバイトを符号拡張) */
    if (n == 3)
        n = 4;
    s32 v = 0;
    for (int i = 0; i < n; i++) {
        u8 b = *anim_sp++;
        if (i == n - 1 && b >= 0x80)
            v |= (s32)(b - 256) << (8 * i);
        else
            v |= (s32)b << (8 * i);
    }
    return v;
}

static void anim_advance(void) {
    if (!anim_sp)
        return;
    int onum = 0;
    for (;;) {
        u8 a = *anim_sp++;
        if (a == 0xff) {
            a = *anim_sp++;
            if (a <= 0x7f) {
                u2e_fov = (u32)a << 8;  /* フレーム区切りが次フレームの fov を持つ */
                return;                 /* 1 フレーム終わり */
            }
            if (a == 0xff) {
                anim_sp = 0;            /* ストリーム終端 */
                u2e_done = 1;           /* 原作: resetscene() + xit=1 でパート終了 */
                return;
            }
        }
        if ((a & 0xc0) == 0xc0) {
            onum = (a & 0x3f) << 4;
            a = *anim_sp++;
        }
        onum = (onum & 0xff0) | (a & 0xf);
        /* 範囲外のオブジェクト番号でもレコードの残り (pflag/差分) を
         * 最後まで読む。ここで return するとストリーム位置がレコードの
         * 途中で止まり、以降のフレームがずれて ON/OFF を取りこぼす
         * (frame 892 の obj17 ON が効かずビルが消える)。 */
        rmat *r = (onum >= 0 && onum < conum) ? &co[onum].r0 : (rmat *)0;
        if (r) {
            if ((a & 0xc0) == 0x80)
                co[onum].on = 1;
            else if ((a & 0xc0) == 0x40)
                co[onum].on = 0;
        }

        u32 pflag = 0;
        int sel = (a & 0x30) >> 4;
        if (sel == 1) {
            pflag = *anim_sp++;
        } else if (sel == 2) {
            pflag = anim_sp[0] | ((u32)anim_sp[1] << 8);
            anim_sp += 2;
        } else if (sel == 3) {
            pflag = anim_sp[0] | ((u32)anim_sp[1] << 8) | ((u32)anim_sp[2] << 16);
            anim_sp += 3;
        }
        {
            s32 dx = lsget(pflag);
            s32 dy = lsget(pflag >> 2);
            s32 dz = lsget(pflag >> 4);
            if (r) { r->x += dx; r->y += dy; r->z += dz; }
        }
        if (pflag & 0x40) {
            for (int b = 0; b < 9; b++)
                if (pflag & (0x80u << b)) {
                    s32 d = lsget(2);
                    if (r) r->m[b] += d;
                }
        } else {
            for (int b = 0; b < 9; b++)
                if (pflag & (0x80u << b)) {
                    s32 d = lsget(1);
                    if (r) r->m[b] += d;
                }
        }
    }
}

/* ---- 2D ポリゴンクリップ (Sutherland-Hodgman) ---- */
/* mode: 0=x>=bound, 1=x<=bound, 2=y>=bound, 3=y<=bound */
static int clip2d(const s32 *in, int n, s32 *out, int mode, s32 bound) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        s32 ax = in[i * 2], ay = in[i * 2 + 1];
        s32 bx = in[((i + 1) % n) * 2], by = in[((i + 1) % n) * 2 + 1];
        int ain, bin;
        if (mode == 0)      { ain = ax >= bound; bin = bx >= bound; }
        else if (mode == 1) { ain = ax <= bound; bin = bx <= bound; }
        else if (mode == 2) { ain = ay >= bound; bin = by >= bound; }
        else                { ain = ay <= bound; bin = by <= bound; }

        if (ain) {
            out[m * 2] = ax; out[m * 2 + 1] = ay; m++;
        }
        if (ain != bin) {
            s32 tn, td;
            if (mode < 2) { tn = bound - ax; td = bx - ax; }
            else          { tn = bound - ay; td = by - ay; }
            if (td != 0) {
                s32 ix = ax + ((bx - ax) * tn) / td;
                s32 iy = ay + ((by - ay) * tn) / td;
                if (mode == 0 || mode == 1) ix = bound; else iy = bound;
                out[m * 2] = ix; out[m * 2 + 1] = iy; m++;
            }
        }
    }
    return m;
}

/* ---- スキャンライン三角形塗り (座標は画面内) ---- */
/* ---- n 角形の走査線塗り (ADRAW.ASM drawfill_grd 相当) ---- */
#define PGMAX 64
static int pg_x[PGMAX], pg_y[PGMAX], pg_c[PGMAX], pg_vi[PGMAX];
/* 3D 近平面クリップ (原作 newclip / clip_near_reproject) の出力 */
static int nc_x[PGMAX + 2], nc_y[PGMAX + 2], nc_c[PGMAX + 2];
static int pg_n;
static int edge_y0[PGMAX], edge_y1[PGMAX];
static s32 edge_x[PGMAX], edge_dx[PGMAX];
static s32 edge_c[PGMAX], edge_dc[PGMAX];

static int clip_pg(int *ix, int *iy, int *ic, int n,
                   int *ox, int *oy, int *oc, int mode, int bound)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1 == n) ? 0 : (i + 1);
        int ai, aj;
        switch (mode) {
        case 0: ai = ix[i] >= bound; aj = ix[j] >= bound; break;
        case 1: ai = ix[i] <= bound; aj = ix[j] <= bound; break;
        case 2: ai = iy[i] >= bound; aj = iy[j] >= bound; break;
        default: ai = iy[i] <= bound; aj = iy[j] <= bound; break;
        }
        if (ai) { ox[m] = ix[i]; oy[m] = iy[i]; oc[m] = ic[i]; m++; }
        if (ai != aj && m < PGMAX - 1) {
            int di = (mode <= 1) ? ix[i] : iy[i];
            int dj = (mode <= 1) ? ix[j] : iy[j];
            int den = dj - di;
            if (den == 0) den = 1;
            int num = bound - di;
            if (mode <= 1) {
                ox[m] = bound;
                oy[m] = visu_clip_other(di, dj, iy[i], iy[j], bound);
            } else {
                ox[m] = visu_clip_other(di, dj, ix[i], ix[j], bound);
                oy[m] = bound;
            }
            oc[m] = ic[i] + (int)(((long)(ic[j] - ic[i]) * num) / den);
            m++;
        }
    }
    return m;
}

/* C++ VISU.cpp drawfill_nrm / drawfill_grd, edge_setup_nrm と同じ画素中心
 * (16.16 X / 8.8 color) の辺走査。原作 ADRAW.ASM:121-138 と同様、
 * 辺は行ごとに加算し、水平スパンは右端排他で塗る。 */
static void fill_poly(void)
{
    if (pg_n < 3)
        return;
    int ymin = pg_y[0], ymax = pg_y[0], flat = 1;
    for (int i = 1; i < pg_n; i++) {
        if (pg_y[i] < ymin) ymin = pg_y[i];
        if (pg_y[i] > ymax) ymax = pg_y[i];
        if (pg_c[i] != pg_c[0]) flat = 0;
    }
    if (ymin < U2E_VIEW_TOP) ymin = U2E_VIEW_TOP;
    if (ymax > U2E_VIEW_BOT) ymax = U2E_VIEW_BOT;
    if (ymin >= ymax)
        return;

    for (int i = 0; i < pg_n; i++) {
        int j = (i + 1 == pg_n) ? 0 : (i + 1);
        int lo = i, hi = j;
        if (pg_y[lo] > pg_y[hi]) {
            lo = j;
            hi = i;
        }
        int dy = pg_y[hi] - pg_y[lo];
        if (dy == 0) {
            edge_y0[i] = edge_y1[i] = 0;
            continue;
        }
        int first = pg_y[lo] > ymin ? pg_y[lo] : ymin;
        int end = pg_y[hi] < ymax ? pg_y[hi] : ymax;
        edge_y0[i] = first;
        edge_y1[i] = end;
        if (first >= end)
            continue;

        edge_dx[i] = ((pg_x[hi] - pg_x[lo]) * 65536L) / dy;
        edge_x[i] = (pg_x[lo] * 65536L) + 0x8000L +
                    edge_dx[i] * (first - pg_y[lo] + 1);
        if (!flat) {
            edge_dc[i] = ((pg_c[hi] - pg_c[lo]) * 256L) / dy;
            edge_c[i] = pg_c[lo] * 256L +
                        edge_dc[i] * (first - pg_y[lo] + 1);
        }
    }

    /* 有効な辺だけを追う (2026-11-16 ユーザー承認の最適化)。以前は 1 行ごとに
     * 全部の辺を調べていた (PC で U2E を再生して 1 フレーム平均 9588 回)。
     * 開始行の順に並べた辺を、その行で有効になったら act[] (辺の番号順) に
     * 入れ、終わったら外す。左右の端と色は、以前と同じく番号順に見て
     * 最小・最大 (同じ値なら先の番号) を取るので、結果は同じ。 */
    int ord[PGMAX], nord = 0;
    for (int i = 0; i < pg_n; i++) {
        if (edge_y0[i] >= edge_y1[i])
            continue;
        int k = nord++;
        while (k > 0 && edge_y0[ord[k - 1]] > edge_y0[i]) {
            ord[k] = ord[k - 1];
            k--;
        }
        ord[k] = i;
    }
    int act[PGMAX], na = 0, next = 0;

    for (int y = ymin; y < ymax; y++) {
        while (next < nord && edge_y0[ord[next]] == y) {
            int e = ord[next++], k = na++;
            while (k > 0 && act[k - 1] > e) {
                act[k] = act[k - 1];
                k--;
            }
            act[k] = e;
        }
        s32 xl = 0x7fffffff, xr = -0x7fffffff;
        int cl = 0, cr = 0;
        if (flat) {
            for (int a = 0; a < na; a++) {
                int i = act[a];
                s32 x = edge_x[i];
                if (x < xl) xl = x;
                if (x > xr) xr = x;
                edge_x[i] += edge_dx[i];
            }
            cl = cr = pg_c[0];
        } else {
            for (int a = 0; a < na; a++) {
                int i = act[a];
                s32 x = edge_x[i];
                int c = (int)(edge_c[i] >> 8);
                if (x < xl) { xl = x; cl = c; }
                if (x > xr) { xr = x; cr = c; }
                edge_x[i] += edge_dx[i];
                edge_c[i] += edge_dc[i];
            }
        }
        /* この行で終わる辺を外す (次の行では y >= edge_y1) */
        {
            int w = 0;
            for (int a = 0; a < na; a++)
                if (y + 1 < edge_y1[act[a]])
                    act[w++] = act[a];
            na = w;
        }
        int x0 = (int)(xl >> 16);
        int x1 = (int)(xr >> 16);
        if (x0 >= x1)
            continue;
        int l = x0 < 0 ? 0 : x0;
        int r = x1 > U2E_W ? U2E_W : x1;
        if (l >= r)
            continue;
        u8 *d = g_dst + (u32)y * U2E_W;
        if (cl == cr) {
            int v = cl < 0 ? 0 : (cl > 255 ? 255 : cl);
            /* memset の呼び出しをやめ、その場で 32 ビットずつ書く */
            u8 *q = d + l;
            int n = r - l;
            while (n && ((u32)q & 3)) {
                *q++ = (u8)v;
                n--;
            }
            {
                u32 w = (u32)v * 0x01010101u;
                u32 *q4 = (u32 *)q;
                while (n >= 16) {
                    q4[0] = w; q4[1] = w; q4[2] = w; q4[3] = w;
                    q4 += 4;
                    n -= 16;
                }
                while (n >= 4) {
                    *q4++ = w;
                    n -= 4;
                }
                q = (u8 *)q4;
            }
            while (n--)
                *q++ = (u8)v;
            continue;
        }
        int span = x1 - x0;
        int offset = l - x0;
        int diff = cr - cl;
        /* 割り算は 1 行 1 回にする (以前は / と % を別々に、最大 4 回)。
         * C の % は a - (a/b)*b なので、商から余りを掛け算で求めても同じ値。
         * 左端が切れていない行 (offset == 0) は 2 回目の割り算が要らない。
         * 実機 68060 の bench で割り算 1 回 約 0.8us (2026-11-16)。 */
        int step = diff / span;
        int rem = diff - step * span;
        int sign = rem >= 0 ? 1 : -1;
        int abs_rem = rem >= 0 ? rem : -rem;
        int acc = 0, c = cl;
        if (offset) {
            int t = abs_rem * offset;
            int tq = t / span;
            acc = t - tq * span;
            c = cl + step * offset + sign * tq;
        }
        if ((unsigned)cl <= 255u && (unsigned)cr <= 255u) {
            /* 両端が 0..255 なら途中の色も両端の間に入るので、1 ドットごとの
             * 範囲チェックは要らない (同じ値になる) */
            for (int x = l; x < r; x++) {
                d[x] = (u8)c;
                acc += abs_rem;
                c += step;
                if (acc >= span) {
                    c += sign;
                    acc -= span;
                }
            }
            continue;
        }
        for (int x = l; x < r; x++) {
            int v = c < 0 ? 0 : (c > 255 ? 255 : c);
            d[x] = (u8)v;
            acc += abs_rem;
            c += step;
            if (acc >= span) {
                c += sign;
                acc -= span;
            }
        }
    }
}

static void fill_poly_profiled(u32 *fill_time) {
    if (prof_on) {
        u32 start = prof_now();
        fill_poly();
        *fill_time += prof_now() - start;
    } else {
        fill_poly();
    }
}

/* ---- 64/32 の符号付き乗除算 (原作 ZCLIPCLIP の imul/idiv 相当) ----
 * (a*b)/c を 64 ビット中間値で計算する。__divdi3 をリンクしていないため
 * シフト減算で実装する (近平面クリップの交点数回だけ呼ばれる)。 */
static s32 muldiv32(s32 a, s32 b, s32 c) {
    int neg = 0;
    u32 ua, ub, uc;
    if (a < 0) { neg ^= 1; ua = (u32)(-a); } else ua = (u32)a;
    if (b < 0) { neg ^= 1; ub = (u32)(-b); } else ub = (u32)b;
    if (c < 0) { neg ^= 1; uc = (u32)(-c); } else uc = (u32)c;
    if (uc == 0)
        return 0;
    u64 p = (u64)ua * ub;
    u64 q = 0, r = 0, bit = (u64)1 << 63;
    for (int i = 0; i < 64; i++) {
        r = (r << 1) | ((p & bit) ? 1 : 0);
        if (r >= uc) {
            r -= uc;
            q |= bit;
        }
        bit >>= 1;
    }
    return neg ? -(s32)q : (s32)q;
}

/* ---- バックフェイスカリング: dot(法線, 頂点) < 0 で可視 ---- */
static int face_visible(const s16 *n, const s32 *v) {
    s64 d = smul64(n[0], v[0]) + smul64(n[1], v[1]) + smul64(n[2], v[2]);
    return d < 0;
}

/* ---- フラットシェーディング (ADRAW.ASM の calclight) ---- */
/* ADRAW.ASM calclight: 1 つの法線から 1..30 の階調 */
static int calc_shade(const s16 *n, int sb) {
    s64 dot = smul64(n[0], 12118) + smul64(n[1], 10603) + smul64(n[2], 3030);
    int b = 128 + (int)(dot >> 21);
    if (b < 0) b = 0; else if (b > 255) b = 255;
    int shade = b >> (6 - (sb >> 2));
    if (shade < 1) shade = 1; else if (shade > 30) shade = 30;
    return shade;
}

static u8 tri_color(u16 nrm, u8 flags, u8 base, const u16 *vi3,
                    const u8 *vdata, u16 nnum) {
    int sb = flags & 0x0c;
    if (sb == 0)
        return (u8)(base + 1);
    int shade;
    if (flags & 0x10) {
        /* F_GOURAUD: 頂点ごとの法線の階調を平均して面の明るさにする
         * (完全な補間はこのビルドで通らないため近似) */
        int sum = 0;
        for (int k = 0; k < 3; k++) {
            int vn = (int)rd32(vdata + vi3[k] * 16 + 12);
            if ((unsigned)vn >= (unsigned)nnum)
                vn = nrm;
            sum += calc_shade(rn[vn], sb);
        }
        shade = sum / 3;
        if (shade < 1) shade = 1;
    } else {
        shade = calc_shade(rn[nrm], sb);
    }
    return (u8)(base + shade);
}

static void draw_object(u2e_inst *ci) {
    const u2e_obj *o = ci->obj;
    const rmat *r = &ci->r;
    u32 st_a = prof_now();

    for (int i = 0; i < o->vnum; i++) {
        const u8 *p = o->vdata + i * 16;
        s32 x = rd32(p), y = rd32(p + 4), z = rd32(p + 8);
        rv[i][0] = rot_row(r, 0, x, y, z) + r->x;
        rv[i][1] = rot_row(r, 1, x, y, z) + r->y;
        rv[i][2] = rot_row(r, 2, x, y, z) + r->z;
    }
    for (int i = 0; i < o->nnum; i++) {
        const u8 *p = o->ndata + i * 6;
        s32 x = rd16(p), y = rd16(p + 2), z = rd16(p + 4);
        rn[i][0] = (s16)rot_row(r, 0, x, y, z);
        rn[i][1] = (s16)rot_row(r, 1, x, y, z);
        rn[i][2] = (s16)rot_row(r, 2, x, y, z);
    }

    /* 投影 (全頂点が画面外ならオブジェクトごとスキップ) */
    u8 vf_and = 0xff;
    for (int i = 0; i < o->vnum; i++) {
        s32 x = rv[i][0], y = rv[i][1], z = rv[i][2];
        u8 vf = 0;
        if (z < PROJ_ZMIN) { z = PROJ_ZMIN; vf |= VF_NEAR; }
        else if (z > PROJ_ZMAX) vf |= VF_FAR;
        s32 sx = (x * proj_mulx) / z + PROJ_ADDX;
        s32 sy = (y * proj_muly) / z + PROJ_ADDY;
        if (sx < 0) vf |= VF_LEFT; else if (sx > 319) vf |= VF_RIGHT;
        if (sy < U2E_VIEW_TOP) vf |= VF_UP; else if (sy > U2E_VIEW_BOT) vf |= VF_DOWN;
        px[i] = sx; py[i] = sy; pvf[i] = vf;
        vf_and &= vf;
    }
    {
        u32 t = prof_now();
        prof_stage_add(1, t - st_a);    /* 頂点・法線の回転と投影 */
        st_a = t;
    }
    if (vf_and) {
        u2e_dbg_skip++;
#ifdef U2E_DEBUG_CYCLE
        if (!DBG_OFF(2))
#endif
            return;
    }
    u2e_dbg_objs++;

    /* ポリゴンを奥行きでソートして描く (原本は ORD リスト) */
    int np = o->tnum;
    if (np > 600)
        np = 600;

    {
        const u8 *q = o->tdata;
        for (int i = 0; i < np; i++) { poly_pp[i] = q; q += 6 + 2 * q[0]; }
    }
    for (int i = 0; i < np; i++) {
        int first = ((u16)poly_pp[i][6] << 8) | poly_pp[i][7];
        poly_z[i] = rv[first][2];
        poly_order[i] = i;
    }
    for (int i = 1; i < np; i++) {
        int v = poly_order[i];
        s32 z = poly_z[v];
        int j = i - 1;
        while (j >= 0 && poly_z[poly_order[j]] < z) {
            poly_order[j + 1] = poly_order[j];
            j--;
        }
        poly_order[j + 1] = v;
    }

    /* 原本 drawobject(): ORD リストのうち「ソート頂点の z が最小」のものを選ぶ */
    const u8 *ordsel = 0;
    int ordn = 0;
    if (o->ordata && o->ordnum) {
        const u8 *q = o->ordata + 2;
        s32 best = 0x7fffffff;
        for (int k = 0; k < o->ordnum; k++) {
            u16 sortv = ((u16)q[0] << 8) | q[1];
            u16 nn = ((u16)q[2] << 8) | q[3];
            if (sortv < o->vnum && rv[sortv][2] < best) {
                best = rv[sortv][2];
                ordsel = q + 4;
                ordn = nn;
            }
            q += 4 + 2 * nn;
        }
    }

    {
        u32 t = prof_now();
        prof_stage_add(2, t - st_a);    /* ポリゴンの並べ替え・ORD 選択 */
        st_a = t;
    }
    u32 fill_time = 0, clip_time = 0;
    int drawcount = ordsel ? ordn : np;
    for (int pi = 0; pi < drawcount; pi++) {
        int polyi;
        if (ordsel)
            polyi = ordn > pi ? (((u16)ordsel[pi * 2] << 8) | ordsel[pi * 2 + 1]) : -1;
        else
            polyi = poly_order[pi];
        if (polyi < 0 || polyi >= np)
            continue;
        const u8 *q = poly_pp[polyi];
        int n = q[0];
        u8 flags = q[1];
        int color = ((int)q[2] << 8) | q[3];
        /* 原作 ADRAW.ASM:475-476: color==0xFFFF の面は描かない */
        if (color == 0xFFFF)
            continue;
        u16 nrm = ((u16)q[4] << 8) | q[5];
        if (n < 3 || n > PGMAX / 2)
            continue;
        if (nrm >= o->nnum)
            nrm = 0;

        int first = ((u16)q[6] << 8) | q[7];
        /* 原作 ADRAW.ASM:484-486 / VISU.cpp:1511: F_2SIDE (flags bit1) は
         * バックフェイスカリングしない。flags は 16bit の上位バイトなので
         * raw byte の 0x02 が F_2SIDE=0x0200 に対応する。 */
        int twoside = (flags & 0x02) != 0;
#ifdef U2E_DEBUG_CYCLE
        if (!twoside && !face_visible(rn[nrm], rv[first]) && !DBG_OFF(3))
#else
        if (!twoside && !face_visible(rn[nrm], rv[first]))
#endif
        {
            continue;
        }

        int go = (flags & 0x10) != 0;
        int q0 = calc_shade(rn[nrm], flags & 0x0c);
        int ok = 1;
        int anded = 0xff, orred = 0;
        pg_n = n;
        for (int k = 0; k < n; k++) {
            int vi = ((u16)q[6 + 2 * k] << 8) | q[7 + 2 * k];
            if (vi >= o->vnum) { ok = 0; break; }
            int c = color + q0;
            if (go) {
                int vn = (int)rd32(o->vdata + vi * 16 + 12);
                if ((unsigned)vn >= (unsigned)o->nnum)
                    vn = nrm;
                c = color + calc_shade(rn[vn], flags & 0x0c);
            }
            pg_c[k] = c & 0xFF;
            pg_x[k] = px[vi];
            pg_y[k] = py[vi];
            pg_vi[k] = vi;
            anded &= pvf[vi];
            orred |= pvf[vi];
        }
        if (!ok)
            continue;

        /* 原作 draw_polylist: 全頂点が同じ側で画面外なら描かない */
#ifdef U2E_DEBUG_CYCLE
        if (anded && !DBG_OFF(4))
#else
        if (anded)
#endif
        {
            continue;
        }

        /* 原作 newclip: 近平面を 3D でクリップして再投影する
         * (C++ VISU.cpp:1192-1256, 1393-1407)。2D だけでクリップすると
         * z<512 の頂点が z=512 に張り付いて巨大な多角形になる。 */
        const int *sx = pg_x, *sy = pg_y, *sc = pg_c;
#ifdef U2E_DEBUG_CYCLE
        if ((orred & VF_NEAR) && !DBG_OFF(5)) {
#else
        if (orred & VF_NEAR) {
#endif
            u32 clip_start = prof_on ? prof_now() : 0;
            int nm = 0;
            for (int k = 0; k < pg_n; k++) {
                int j = (k + 1 == pg_n) ? 0 : (k + 1);
                int vi = pg_vi[k], vj = pg_vi[j];
                int ii = rv[vi][2] >= PROJ_ZMIN;
                int ij = rv[vj][2] >= PROJ_ZMIN;
                if (ii) {
                    nc_x[nm] = pg_x[k]; nc_y[nm] = pg_y[k]; nc_c[nm] = pg_c[k];
                    nm++;
                }
                if (ii != ij) {
                    int far = (rv[vi][2] >= rv[vj][2]) ? vi : vj;
                    int near = (rv[vi][2] >= rv[vj][2]) ? vj : vi;
                    s32 dz = rv[far][2] - rv[near][2];
                    if (dz == 0) dz = 1;
                    s32 mm = rv[far][2] - PROJ_ZMIN;
                    s32 xi = rv[far][0] + muldiv32(rv[near][0] - rv[far][0], mm, dz);
                    s32 yi = rv[far][1] + muldiv32(rv[near][1] - rv[far][1], mm, dz);
                    nc_x[nm] = muldiv32(xi, proj_mulx, PROJ_ZMIN) + PROJ_ADDX;
                    nc_y[nm] = muldiv32(yi, proj_muly, PROJ_ZMIN) + PROJ_ADDY;
                    /* 色は辺の両端で補間 (原作と同じ式) */
                    s32 dz2 = rv[vi][2] - rv[vj][2];
                    if (dz2 == 0) dz2 = 1;
                    s32 m2 = rv[vi][2] - PROJ_ZMIN;
                    nc_c[nm] = pg_c[k] + (int)(((s32)(pg_c[j] - pg_c[k]) * m2) / dz2);
                    nm++;
                }
            }
            if (prof_on)
                clip_time += prof_now() - clip_start;
            if (nm < 3)
                continue;
            sx = nc_x; sy = nc_y; sc = nc_c;
            pg_n = nm;
        }

        int inside = 1;
        for (int k = 0; k < pg_n; k++) {
            if (sx[k] < 0 || sx[k] > 319 ||
                sy[k] < U2E_VIEW_TOP || sy[k] > U2E_VIEW_BOT) {
                inside = 0;
                break;
            }
        }
        if (inside) {
            for (int k = 0; k < pg_n; k++) {
                pg_x[k] = sx[k];
                pg_y[k] = sy[k];
                pg_c[k] = sc[k];
            }
            fill_poly_profiled(&fill_time);
            u2e_dbg_polys++;
            continue;
        }

        /* 画面内クリップ (モード 6 では無効化。
         * fill_poly は x=0..319 / y=32..222 に自前でクランプする) */
        int m = pg_n;
        for (int k = 0; k < m; k++) {
            clip_ax[k] = sx[k]; clip_ay[k] = sy[k]; clip_ac[k] = sc[k];
        }
#ifdef U2E_DEBUG_CYCLE
        if (!DBG_OFF(6)) {
#endif
        u32 clip_start = prof_on ? prof_now() : 0;
        m = clip_pg(clip_ax, clip_ay, clip_ac, m, clip_bx, clip_by, clip_bc, 0, 0);
        if (m >= 3) m = clip_pg(clip_bx, clip_by, clip_bc, m, clip_ax, clip_ay, clip_ac, 1, 319);
        if (m >= 3) m = clip_pg(clip_ax, clip_ay, clip_ac, m, clip_bx, clip_by, clip_bc, 2, U2E_VIEW_TOP);
        if (m >= 3) m = clip_pg(clip_bx, clip_by, clip_bc, m, clip_ax, clip_ay, clip_ac, 3, U2E_VIEW_BOT);
        if (prof_on)
            clip_time += prof_now() - clip_start;
#ifdef U2E_DEBUG_CYCLE
        }
#endif
        if (m < 3) continue;
        pg_n = m;
        for (int k = 0; k < m; k++) {
            pg_x[k] = clip_ax[k]; pg_y[k] = clip_ay[k]; pg_c[k] = clip_ac[k];
        }
        fill_poly_profiled(&fill_time);
        u2e_dbg_polys++;
    }
    prof_stage_add(3, prof_now() - st_a);   /* 裏面判定・クリップ・塗り */
    prof_stage_add(6, fill_time);
    prof_stage_add(7, clip_time);
}

/* ---- デバッグ計測バー (上部の黒帯 y=0..31 に表示) ----
 * 3D ビューポートは y=32..222 なので、バーはその上に収める。 */
static void dbg_bar(int x, int w, int h) {
    if (h <= 0)
        return;
    if (h > 30)
        h = 30;
    for (int y = 32 - h; y < 32; y++) {
        u8 *d = g_dst + (u32)y * U2E_W + x;
        for (int i = 0; i < w; i++)
            d[i] = 254;
    }
}

/* 原本 U2E.C:336-350: vid_init(11) + シーンのパレット (252/255=黒, 253/254=白) */
static void u2e_scene_start(void) {
    static u8 pal[768];
    for (int i = 0; i < 768; i++)
        pal[i] = g_u2e_pal[i];
    pal[252 * 3 + 0] = 0;   pal[252 * 3 + 1] = 0;   pal[252 * 3 + 2] = 0;
    pal[253 * 3 + 0] = 255; pal[253 * 3 + 1] = 255; pal[253 * 3 + 2] = 255;
    pal[254 * 3 + 0] = 255; pal[254 * 3 + 1] = 255; pal[254 * 3 + 2] = 255;
    pal[255 * 3 + 0] = 0;   pal[255 * 3 + 1] = 0;   pal[255 * 3 + 2] = 0;
    video_set_palette(pal);

    anim_sp = g_u2e_script;
    anim_advance();                 /* カメラ行列の初期化 (原本も最初に 1 フレーム解析) */
    u2e_frame = 1;                  /* currframe=2 相当 */
    u2e_last_vbl = vblank_frames();
    u2e_frac = 0;
#ifdef U2E_DEBUG_CYCLE
    u2e_dbg_t0 = vblank_frames();
    u2e_dbg_mode = 0;
#endif
    intro_done = 1;
}

/* 原本 U2E.C:285-335 のパレット変化。
 * 6bit DAC の +2 は 8bit の +8 に相当する。 */
static void intro_palette(int f) {
    u8 pal[768];
    for (int i = 0; i < 768; i++)
        pal[i] = intro_pal[i];
    if (f < INTRO_WHITEIN) {
        int add = f * 8;
        for (int i = 3; i < 762; i++) {
            int v = pal[i] + add;
            pal[i] = (u8)(v > 255 ? 255 : v);
        }
    } else if (f < INTRO_FADEOUT) {
        /* 白を保持 (fadeset 中も同じ) */
        for (int i = 3; i < 762; i++)
            pal[i] = 255;
    } else {
        int d = (f - INTRO_FADEOUT) * 8;
        for (int i = 3; i < 759; i++) {
            int v = 255 - d;
            pal[i] = (u8)(v < 0 ? 0 : v);
        }
        for (int i = 759; i < 762; i++)
            pal[i] = 255;                  /* 色 253 は白のまま */
        for (int i = 762; i < 765; i++) {
            int v = intro_pal[i] + d;      /* 色 254 は prev から白へ */
            pal[i] = (u8)(v > 255 ? 255 : v);
        }
    }
    video_set_palette(pal);
}

/* 原本 U2E.C:145-183 fadeset() の上書き部分。
 * 直前の画面 (intro_img) を残したまま、色 0/252/253/254 を置く。
 * fadeset は Mode X (1 行 80 バイト、マップマスク 15 = 1 バイトで 4 ドット)
 * に書くので、オフセットはバイト単位 (x4 ドット)。マスク 2+4+8 の 63 バイト目は
 * 252..255 ドットのうち 253..255 だけ。1 段目の memset(vram+y*80+252,0,16) は
 * 行をはみ出して y+3 行目の 12..27 バイト (48..111 ドット) を 0 にする
 * (原本どおり。25..27 行目は 2 段目で上書きされる)。
 * 以前はバイトをドットとして扱っていたため、帯の幅が 1/4 になり
 * 映像が乱れて見えた (2026-11-15 ユーザー指摘)。 */
static void fill_px(u8 *row, int x0, int n, u8 c) {
    for (int x = x0; x < x0 + n && x < 320; x++)
        row[x] = c;
}

static void intro_draw(u8 *chunky, int f) {
    for (int i = 0; i < 320 * 256; i++)
        chunky[i] = 0;
    memcpy(chunky + INTRO_Y * 320, intro_img, 320 * 200);
    if (f < INTRO_FADESET)
        return;
#define IROW(y) (chunky + (u32)(INTRO_Y + (y)) * 320)
    for (int y = 0; y < 25; y++) {
        u8 *row = IROW(y);
        fill_px(row, 0, 17 * 4, 0);
        fill_px(row, 17 * 4, 47 * 4, 252);
        fill_px(row, 63 * 4 + 1, 3, 0);
        fill_px(IROW(y + 3), 12 * 4, 16 * 4, 0);    /* +252 バイト */
    }
    for (int y = 25; y < 175; y++) {
        u8 *row = IROW(y);
        fill_px(row, 0, 17 * 4, 254);
        fill_px(row, 17 * 4, 47 * 4, 253);
        fill_px(row, 63 * 4 + 1, 3, 254);
        fill_px(row, 64 * 4, 16 * 4, 254);
    }
    for (int y = 175; y < 200; y++) {
        u8 *row = IROW(y);
        fill_px(row, 0, 17 * 4, 0);
        fill_px(row, 17 * 4, 47 * 4, 252);
        fill_px(row, 63 * 4 + 1, 3, 0);
        fill_px(row, 64 * 4, 16 * 4, 0);
    }
#undef IROW
}

void u2e_init(void) {
    parse_objects();

    /* instance table: co[0] はカメラ、co[1..] がオブジェクト実体 */
    const u8 *p = g_u2e_inst;
    conum = (((u16)p[0] << 8) | p[1]) + 1;
    if (conum > 64)
        conum = 64;
    p += 2;
    for (int i = 0; i < 64; i++) {
        co[i].obj = 0;
        clear_rmat(&co[i].r0);
        clear_rmat(&co[i].r);
        co[i].on = 0;
        co[i].dist = 0;
    }
    for (int i = 0; i + 1 < conum; i++) {
        int idx = ((u16)p[i * 2] << 8) | p[i * 2 + 1];
        if (idx < 64)
            co[i + 1].obj = &objs[idx];
    }

    u2e_done = 0;

    /* 前パートが無い場合 (固定シーンテスト) はイントロを飛ばす。 */
    intro_done = (g_prev_frame == 0 || g_prev_pal == 0);
#ifdef TEST_FIXED_SCENE
    intro_done = 1;
#endif

    if (intro_done) {
        u2e_scene_start();
    } else {
#ifdef U2E_TEST_INTRO
        /* テスト専用: 前パートが無い固定シーンでもイントロを確認する。
         * 縞模様 + 既知のパレットを前フレームの代わりに使う。 */
        if (g_prev_frame == 0 || g_prev_pal == 0) {
            for (int i = 0; i < 320 * 200; i++)
                intro_img[i] = (u8)((i >> 3) & 15);
            for (int i = 0; i < 768; i++)
                intro_pal[i] = (u8)((i % 48) * 5);
        } else
#endif
        {
            for (int i = 0; i < 200; i++)
                memcpy(intro_img + i * 320,
                       g_prev_frame + (u32)(INTRO_Y + i) * 320, 320);
            memcpy(intro_pal, g_prev_pal, 768);
        }
        intro_f = 0;
        intro_acc = 0;
        intro_last_vbl = vblank_frames();
        video_set_palette(intro_pal);
    }
}

int u2e_finished(void) {
    /* 原本 U2E.C はシーンスクリプトの 0xFF 0xFF で終了する */
    return u2e_done;
}

int u2e_render(u8 *chunky, u8 *planes, int width, int height) {
    (void)planes;
    (void)width;
    (void)height;

    if (!g_u2e_obj || !g_u2e_inst || !g_u2e_script || !g_u2e_pal)
        return 0;

    /* 開始時のホワイトアウト。99 フレーム (70Hz) = 70.7 VBL。 */
    if (!intro_done) {
        u32 now = video_frame_vbl();
        u32 el = now - intro_last_vbl;
        if (el > 4)
            el = 4;
        intro_last_vbl = now;
        intro_acc += el * 7;
        {
            int steps = (int)(intro_acc / 5);
            intro_acc -= (u32)steps * 5;
            intro_f += steps;
        }
        if (intro_f < INTRO_TOTAL) {
            intro_palette(intro_f);
            intro_draw(chunky, intro_f);
            return 0;
        }
        /* イントロ終了: シーンのパレットへ切り替えて通常描画へ */
        u2e_scene_start();
    }

    /* 原本 U2E.C と同じ順序にするため、アニメーションの前進は描画の後で行う
     * (原本は「描画 → その後に現フレームを解析」)。ここではまだ進めない。 */

    /* 段階別の時間 (prof.cfg があるときだけ、prof.txt の stage 行) */
    u32 st_t0 = prof_now();
    /* 背景は黒 (原本 U2E.C は vid_init で全ページをクリアする) */
    for (int y = 0; y < 200; y++) {
        u8 *dst = chunky + (u32)(y + U2E_BG_TOP) * U2E_W;
        for (int x = 0; x < 320; x++)
            dst[x] = 0;
    }
    for (int y = 0; y < U2E_BG_TOP; y++) {
        u8 *dst = chunky + (u32)y * U2E_W;
        for (int x = 0; x < 320; x++)
            dst[x] = 0;
    }
    for (int y = U2E_BG_TOP + 200; y < U2E_H; y++) {
        u8 *dst = chunky + (u32)y * U2E_W;
        for (int x = 0; x < 320; x++)
            dst[x] = 0;
    }

    g_dst = chunky;
    {
        u32 t = prof_now();
        prof_stage_add(0, t - st_t0);
        st_t0 = t;
    }
#ifdef U2E_DEBUG_CYCLE
    /* 2 秒 (100 VBL) ごとにモードを進める */
    u2e_dbg_mode = (int)(((vblank_frames() - u2e_dbg_t0) / 100u) % 10u);
#endif
    camera_angle(u2e_fov);              /* vid_cameraangle(fov) */
    u2e_dbg_objs = 0;
    u2e_dbg_skip = 0;
    u2e_dbg_polys = 0;
    u2e_dbg_mul = proj_mulx;
    u2e_dbg_muly = proj_muly;

    /* 有効なオブジェクトの行列と距離 */
    int order[64], onum = 0;
    for (int a = 1; a < conum; a++) {
        if (!co[a].obj)
            continue;
        if (!co[a].on) {
#ifdef U2E_DEBUG_CYCLE
            if (!DBG_OFF(1))
#endif
                continue;
        }
        apply_camera(&co[a]);
        if (co[a].obj->kind & 1)
            co[a].dist = 1000000000L;          /* 原本: 名前[1]=='_' は最初に描く */
        else
            co[a].dist = center_z(&co[a]);
        if ((co[a].obj->kind & 2) && u2e_frame > 900 && u2e_frame < 1100)
            co[a].dist = 1;                    /* 原本: フレーム 900..1099 の s01 */
        order[onum++] = a;
    }

    /* 遠い順に挿入ソート (ペインターズアルゴリズム) */
#ifdef U2E_DEBUG_CYCLE
    if (!DBG_IS(7))
#endif
    {
        for (int i = 1; i < onum; i++) {
            int c = order[i];
            s32 d = co[c].dist;
            int j = i - 1;
            while (j >= 0 && d > co[order[j]].dist) {
                order[j + 1] = order[j];
                j--;
            }
            order[j + 1] = c;
        }
#ifdef U2E_DEBUG_CYCLE
        if (DBG_IS(8)) {
            for (int i = 0; i < onum / 2; i++) {
                int t = order[i];
                order[i] = order[onum - 1 - i];
                order[onum - 1 - i] = t;
            }
        }
#endif
    }

    {
        u32 t = prof_now();
        prof_stage_add(5, t - st_t0);   /* カメラ行列・オブジェクトの並べ替え */
    }
    for (int i = 0; i < onum; i++)
        draw_object(&co[order[i]]);
    st_t0 = prof_now();

    u2e_dbg_on = onum;

#ifdef U2E_DEBUG_CYCLE
    /* モード番号を白いバーの本数で表示 (mode + 1 本) */
    for (int i = 0; i <= u2e_dbg_mode; i++)
        dbg_bar(2 + i * 32, 28, 28);
#elif defined(U2E_DEBUG_BARS)
    /* デバッグ計測 (上部 32 行): objs / skip / polys / on / mulx / muly / frame。
     * 通常ビルドでは表示しない (2026-11-15 ユーザー指示)。 */
    dbg_bar(0, 20, u2e_dbg_objs);
    dbg_bar(24, 20, u2e_dbg_skip * 6);
    dbg_bar(48, 60, u2e_dbg_polys / 20);
    dbg_bar(112, 20, u2e_dbg_on);
    dbg_bar(136, 20, u2e_dbg_mul / 16);     /* 446/16 = 27 */
    dbg_bar(160, 20, u2e_dbg_muly / 20);    /* 500/20 = 25 */
    dbg_bar(184, 12, u2e_frame / 64);
#endif

    /* 描画後にアニメーションを実時間で進める (原本と同じ順序)。
     * 原本 U2E.C: copper2 が syncframe++ (1/VBL)、描画ループは
     * a=syncframe-currframe が 0/-1 を交互になるよう currframe+=2 で進むため、
     * アニメは 1 フレーム/2 VBL = 35 フレーム/秒。
     * PAL 50Hz では 35/50 = 0.7 (7/10) フレーム/VBL。 */
    {
        u32 now = video_frame_vbl();
        u32 el = now - u2e_last_vbl;
        if (el > 8) el = 8;
        u2e_last_vbl = now;
        u2e_frac += el * 7;
        while (u2e_frac >= 10) {
            u2e_frac -= 10;
#ifdef U2E_STOP_FRAME
            if (u2e_frame >= U2E_STOP_FRAME)
                continue;           /* テスト: 指定フレームで停止して表示を固定 */
#endif
            anim_advance();
            u2e_frame++;
        }
    }
    prof_stage_add(4, prof_now() - st_t0); /* アニメーションの前進 */
    prof_stage_frame();

    return 0;
}
