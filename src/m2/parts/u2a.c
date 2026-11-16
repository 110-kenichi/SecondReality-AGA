/*
 * M4 シーン: U2A (Vector Part I - スペースバトル)
 *
 * オリジナル VISU/C/U2A.C (シーン再生) + VISU/VISU.C (vis_drawobject) +
 * ACALC.ASM (calc_rotate / calc_nrotate / calc_project / calc_applyrmatrix)
 * + ADRAW.ASM (draw_polylist / calclight) の移植。
 *
 * 背景 U2ABG.UH は 320x200。256 行の画面では中央 (28..227 行) に
 * 1:1 で置き、上下は黒帯にする。オブジェクトのビューポートは
 * オリジナルの y=25..174 を +28 した 53..202 行にクリップする。
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
#include "../framework/c2p.h"

typedef long long s64;

extern const u8 *g_u2a_pal;
extern const u8 *g_u2a_bg;
extern const u8 *g_u2a_obj;
extern const u8 *g_u2a_anim;
extern const u8 *g_u2a_scene;

#define U2A_W        320
#define U2A_H        256
#define U2A_BG_TOP   28
#define U2A_VIEW_TOP 53
#define U2A_VIEW_BOT 202

#define PROJ_MUL   360
#define PROJ_ADDX  159
#define PROJ_ADDY  127
#define PROJ_ZMIN  512
#define PROJ_ZMAX  9999999

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
    const u8 *vdata;   /* vnum * 12 bytes (s32 x,y,z) */
    const u8 *ndata;   /* nnum * 6 bytes  (s16 x,y,z) */
    const u8 *tdata;   /* tnum * 10 bytes (u16 a,b,c,nrm, u8 color,flags) */
} u2a_obj;

typedef struct {
    const u2a_obj *obj;
    rmat r0, r;
    int on;
    s32 dist;
} u2a_inst;

static u2a_obj objs[3];
static u2a_inst co[6];              /* co[0] = カメラ (obj なし) */
static const u8 *anim_sp;
static u32 u2a_last_vbl;
static u32 u2a_frac;
static int u2a_done;

static s32 rv[MAXV][3];             /* 回転済み頂点 */
static s16 rn[MAXN][3];             /* 回転済み法線 */
static s32 px[MAXV], py[MAXV];      /* 投影座標 */
static u8  pvf[MAXV];
static u8 *g_dst;

/* 画面転送 (c2p) と背景の復元を、ポリゴンを塗った範囲だけにする
 * (2026-11-15 ユーザー承認)。chunky に書くのは背景のコピーと fill_tri の
 * memset だけ。fill_tri で塗った範囲の外枠 (fb_*) を記録し、
 *   - 背景は「前のフレームで塗った枠」だけ戻す (chunky は残っている)
 *   - c2p は裏画面ごとに「その裏画面の前回の枠」と「今回の枠」だけ
 * にする。WinUAE の集計では 1 フレームで変わるブロックは平均 4.7%。
 * 初めて使う裏画面は 256 行全体を変換する。 */
struct u2a_rect { s16 y0, y1, x0, x1; };      /* y0..y1-1, x0..x1-1 */
static struct u2a_rect fb;                     /* 今回塗った範囲 */
static struct u2a_rect ch_prev;                /* chunky に残っている前回の枠 */
static int u2a_bg_full;                        /* 次は背景を全部コピーする */
static u8 *u2a_buf[2];
static struct u2a_rect u2a_prev[2];

static void rect_none(struct u2a_rect *r) { r->y0 = r->x0 = 0; r->y1 = r->x1 = 0; }

static void u2a_conv(const u8 *chunky, u8 *planes, const struct u2a_rect *r) {
    if (r->y1 <= r->y0 || r->x1 <= r->x0)
        return;
    c2p8_cols(chunky + r->y0 * U2A_W, planes, r->y0, r->y1 - r->y0,
              r->x0 >> 5, (r->x1 + 31) >> 5);
}

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
    const u8 *p = g_u2a_obj;
    int n = ((u16)p[0] << 8) | p[1];
    p += 2;
    for (int i = 0; i < n && i < 3; i++) {
        u2a_obj *o = &objs[i];
        o->vnum   = ((u16)p[0] << 8) | p[1];
        o->nnum   = ((u16)p[2] << 8) | p[3];
        o->tnum   = ((u16)p[4] << 8) | p[5];
        o->center = ((u16)p[6] << 8) | p[7];
        p += 8;
        o->vdata = p; p += o->vnum * 12;
        o->ndata = p; p += o->nnum * 6;
        o->tdata = p; p += o->tnum * 10;
    }
}

/* ---- カメラ行列の適用: r = cam * r0 ---- */
static void apply_camera(u2a_inst *ci) {
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

static s32 center_z(const u2a_inst *ci) {
    const u8 *p = ci->obj->vdata + ci->obj->center * 12;
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
            if (a <= 0x7f)
                return;                 /* 1 フレーム終わり */
            if (a == 0xff) {
                anim_sp = 0;            /* ストリーム終端 */
                u2a_done = 1;           /* 原作 U2A.C の xit=1 相当 */
                return;
            }
        }
        if ((a & 0xc0) == 0xc0) {
            onum = (a & 0x3f) << 4;
            a = *anim_sp++;
        }
        onum = (onum & 0xff0) | (a & 0xf);
        if (onum > 5)
            return;
        rmat *r = &co[onum].r0;
        if ((a & 0xc0) == 0x80)
            co[onum].on = 1;
        else if ((a & 0xc0) == 0x40)
            co[onum].on = 0;

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
        r->x += lsget(pflag);
        r->y += lsget(pflag >> 2);
        r->z += lsget(pflag >> 4);
        if (pflag & 0x40) {
            for (int b = 0; b < 9; b++)
                if (pflag & (0x80u << b))
                    r->m[b] += lsget(2);
        } else {
            for (int b = 0; b < 9; b++)
                if (pflag & (0x80u << b))
                    r->m[b] += lsget(1);
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
            if (mode < 2) {
                out[m * 2] = bound;
                out[m * 2 + 1] = visu_clip_other(ax, bx, ay, by, bound);
            } else {
                out[m * 2] = visu_clip_other(ay, by, ax, bx, bound);
                out[m * 2 + 1] = bound;
            }
            m++;
        }
    }
    return m;
}

/* C++ VISU.cpp drawfill_nrm / ADRAW.ASM:121-138 と同じ画素中心の
 * 16.16 エッジ走査。右端は排他で、走査中の辺更新は加算のみ。 */
static void fill_tri(s32 x0, s32 y0, s32 x1, s32 y1, s32 x2, s32 y2, u8 col) {
    s32 t;
    if (y0 > y1) { t=x0;x0=x1;x1=t; t=y0;y0=y1;y1=t; }
    if (y1 > y2) { t=x1;x1=x2;x2=t; t=y1;y1=y2;y2=t; }
    if (y0 > y1) { t=x0;x0=x1;x1=t; t=y0;y0=y1;y1=t; }
    if (y2 <= y0)
        return;

    int ytop = (y0 < U2A_VIEW_TOP) ? U2A_VIEW_TOP : (int)y0;
    int ybot = (y2 > U2A_VIEW_BOT) ? U2A_VIEW_BOT : (int)y2;
    if (ytop >= ybot)
        return;

    s32 dx02 = ((x2 - x0) * 65536L) / (y2 - y0);
    s32 dx01 = (y1 > y0) ? ((x1 - x0) * 65536L) / (y1 - y0) : 0;
    s32 dx12 = (y2 > y1) ? ((x2 - x1) * 65536L) / (y2 - y1) : 0;
    s32 x02 = (x0 << 16) + 0x8000L + dx02 * (ytop - y0 + 1);
    s32 x01 = (x0 << 16) + 0x8000L + dx01 * (ytop - y0 + 1);
    int ystart12 = ytop > y1 ? ytop : (int)y1;
    s32 x12 = (x1 << 16) + 0x8000L + dx12 * (ystart12 - y1 + 1);

    for (int y = ytop; y < ybot; y++, x02 += dx02) {
        s32 xa;
        s32 xb;
        if (y < y1) {
            xa = x02;
            xb = x01;
            x01 += dx01;
        } else {
            xa = x02;
            xb = x12;
            x12 += dx12;
        }
        if (xa > xb) { t = xa; xa = xb; xb = t; }
        int l = (int)(xa >> 16);
        int r = (int)(xb >> 16);
        if (l < 0) l = 0;
        if (r > U2A_W) r = U2A_W;
        if (l >= r)
            continue;
        u8 *d = g_dst + (u32)y * U2A_W;
        memset(d + l, col, (unsigned long)(r - l));
        if (y < fb.y0) fb.y0 = (s16)y;
        if (y + 1 > fb.y1) fb.y1 = (s16)(y + 1);
        if (l < fb.x0) fb.x0 = (s16)l;
        if (r > fb.x1) fb.x1 = (s16)r;
    }
}

static void draw_projected_poly(const s32 sp[][2], int n, u8 col) {
    s32 screen[4][2];
    for (int i = 0; i < n; i++) {
        s32 sx = sp[i][0], sy = sp[i][1];
        if (sx < -16000) sx = -16000; else if (sx > 16000) sx = 16000;
        if (sy < -16000) sy = -16000; else if (sy > 16000) sy = 16000;
        screen[i][0] = sx;
        screen[i][1] = sy;
    }

    /* 画面内ならクリップ不要 */
    int inside = 1;
    for (int i = 0; i < n; i++) {
        if (screen[i][0] < 0 || screen[i][0] > 319 ||
            screen[i][1] < U2A_VIEW_TOP || screen[i][1] > U2A_VIEW_BOT) {
            inside = 0;
            break;
        }
    }
    if (inside) {
        for (int i = 1; i < n - 1; i++)
            fill_tri(screen[0][0], screen[0][1], screen[i][0], screen[i][1],
                     screen[i + 1][0], screen[i + 1][1], col);
        return;
    }

    /* 2D クリップ (最大 8 頂点) */
    s32 a[8][2], b[8][2];
    for (int i = 0; i < n; i++) {
        a[i][0] = screen[i][0];
        a[i][1] = screen[i][1];
    }
    int m = clip2d(&a[0][0], n, &b[0][0], 0, 0);
    if (m < 3) return;
    m = clip2d(&b[0][0], m, &a[0][0], 1, 319);
    if (m < 3) return;
    m = clip2d(&a[0][0], m, &b[0][0], 2, U2A_VIEW_TOP);
    if (m < 3) return;
    m = clip2d(&b[0][0], m, &a[0][0], 3, U2A_VIEW_BOT);
    if (m < 3) return;

    for (int i = 1; i < m - 1; i++)
        fill_tri(a[0][0], a[0][1], a[i][0], a[i][1],
                 a[i + 1][0], a[i + 1][1], col);
}

/* ---- 三角形を近平面でクリップして描く ---- */
static void draw_tri(const s32 *v0, const s32 *v1, const s32 *v2, u8 col) {
    s32 in[3][3] = {
        { v0[0], v0[1], v0[2] },
        { v1[0], v1[1], v1[2] },
        { v2[0], v2[1], v2[2] }
    };
    s32 poly[4][3];
    int n = 0;

    for (int i = 0; i < 3; i++) {
        const s32 *a = in[i];
        const s32 *b = in[(i + 1) % 3];
        int ain = a[2] >= PROJ_ZMIN;
        int bin = b[2] >= PROJ_ZMIN;
        if (ain) {
            poly[n][0] = a[0]; poly[n][1] = a[1]; poly[n][2] = a[2]; n++;
        }
        if (ain != bin) {
            /* ADRAWCLP.ASM ZCLIPCLIP: always interpolate from the farther
             * endpoint. The largest product in the U2A host replay was
             * 1102927364, within signed 32-bit range. */
            const s32 *far = a[2] > b[2] ? a : b;
            const s32 *near = a[2] > b[2] ? b : a;
            s32 dz = far[2] - near[2];
            s32 num = far[2] - PROJ_ZMIN;
            poly[n][0] = far[0] + ((near[0] - far[0]) * num) / dz;
            poly[n][1] = far[1] + ((near[1] - far[1]) * num) / dz;
            poly[n][2] = PROJ_ZMIN;
            n++;
        }
    }
    if (n < 3)
        return;

    /* 投影 */
    s32 sp[4][2];
    for (int i = 0; i < n; i++) {
        s32 z = poly[i][2];
        sp[i][0] = (poly[i][0] * PROJ_MUL) / z + PROJ_ADDX;
        sp[i][1] = (poly[i][1] * PROJ_MUL) / z + PROJ_ADDY;
    }

    draw_projected_poly(sp, n, col);
}

/* ---- バックフェイスカリング: dot(法線, 頂点) < 0 で可視 ---- */
static int face_visible(const s16 *n, const s32 *v) {
    s64 d = smul64(n[0], v[0]) + smul64(n[1], v[1]) + smul64(n[2], v[2]);
    return d < 0;
}

/* ---- フラットシェーディング (ADRAW.ASM の calclight) ---- */
static u8 tri_color(u16 nrm, u8 flags, u8 base) {
    int sb = flags & 0x0c;
    if (sb == 0)
        return (u8)(base + 1);
    const s16 *n = rn[nrm];
    s32 dot = (s32)n[0] * 12118 + (s32)n[1] * 10603 + (s32)n[2] * 3030;
    int b = 128 + (dot >> 21);
    if (b < 0) b = 0; else if (b > 255) b = 255;
    int shade = b >> (6 - (sb >> 2));
    if (shade < 1) shade = 1; else if (shade > 30) shade = 30;
    return (u8)(base + shade);
}

static void draw_object(u2a_inst *ci) {
    const u2a_obj *o = ci->obj;
    const rmat *r = &ci->r;

    for (int i = 0; i < o->vnum; i++) {
        const u8 *p = o->vdata + i * 12;
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
        s32 sx = (x * PROJ_MUL) / z + PROJ_ADDX;
        s32 sy = (y * PROJ_MUL) / z + PROJ_ADDY;
        if (sx < 0) vf |= VF_LEFT; else if (sx > 319) vf |= VF_RIGHT;
        if (sy < U2A_VIEW_TOP) vf |= VF_UP; else if (sy > U2A_VIEW_BOT) vf |= VF_DOWN;
        px[i] = sx; py[i] = sy; pvf[i] = vf;
        vf_and &= vf;
    }
    if (vf_and)
        return;

    for (int t = 0; t < o->tnum; t++) {
        const u8 *tp = o->tdata + t * 10;
        u16 ia = rd16(tp), ib = rd16(tp + 2), ic = rd16(tp + 4);
        u16 nrm = rd16(tp + 6);
        u8 color = tp[8], flags = tp[9];
        if (!face_visible(rn[nrm], rv[ia]))
            continue;
        u8 col = tri_color(nrm, flags, color);
        /* 近平面を越えない頂点は、オブジェクト投影済み座標を再利用する。 */
        if (rv[ia][2] >= PROJ_ZMIN && rv[ib][2] >= PROJ_ZMIN &&
            rv[ic][2] >= PROJ_ZMIN) {
            s32 sp[3][2] = {
                { px[ia], py[ia] },
                { px[ib], py[ib] },
                { px[ic], py[ic] }
            };
            draw_projected_poly(sp, 3, col);
        } else {
            draw_tri(rv[ia], rv[ib], rv[ic], col);
        }
    }
}

void u2a_init(void) {
    parse_objects();

    const u8 *p = g_u2a_scene;
    int n = ((u16)p[0] << 8) | p[1];
    p += 2;
    for (int i = 0; i < 6; i++) {
        co[i].obj = 0;
        clear_rmat(&co[i].r0);
        clear_rmat(&co[i].r);
        co[i].on = 0;
        co[i].dist = 0;
    }
    for (int i = 0; i < n && i < 5; i++) {
        int idx = ((u16)p[i * 2] << 8) | p[i * 2 + 1];
        if (idx >= 1 && idx <= 3)
            co[i + 1].obj = &objs[idx - 1];
    }

    /* パレットは main が最初のフレームを描いてから u2a_apply_palette()
     * で設定する。原作 U2A.C:226-247 は音楽を待ってから vid_init と
     * パレット設定をするので、それまで ALKU の最後の画面が残る。
     * init で先にパレットを変えると ALKU の画面が一瞬違う色になり、
     * 画面を消すと黒が一瞬見える (2026-11-14 ユーザー指摘のチラつき)。 */

    anim_sp = g_u2a_anim;
    anim_advance();                 /* カメラ行列の初期化 */
    u2a_last_vbl = vblank_frames();
    u2a_frac = 0;
    u2a_done = 0;
    u2a_bg_full = 1;
    u2a_buf[0] = u2a_buf[1] = 0;
    rect_none(&ch_prev);
}

void u2a_apply_palette(void) {
    video_set_palette(g_u2a_pal);
}

int u2a_finished(void) {
    return u2a_done;
}

int u2a_render(u8 *chunky, u8 *planes, int width, int height) {
    (void)width;
    (void)height;

    /* アニメーションを実時間で進める。
     * 参照映像に合わせて 1 VBL = 1 ステップ (50 コマ/秒) にしている。 */
    u32 now = vblank_frames();
    u32 el = now - u2a_last_vbl;
    if (el > 4) el = 4;
    u2a_last_vbl = now;
    /* 原作 U2A.C は 35fps、C++ 版 U2A.cpp:242 は 70fps と食い違う。
     * 以前の参照映像との比較で 50fps (1 VBL = 1 ステップ) が合っていた
     * ため、その値を使う (2026-11-13 ユーザー確認: 35fps は遅すぎる)。 */
    while (el--) {
        anim_advance();
    }
    u2a_frac = 0;

    /* 背景 (320x200 を 28 行目から 1:1 で表示)。最初のフレームは全部、
     * 以降は前のフレームでポリゴンを塗った枠 (ch_prev) だけを戻す。
     * ポリゴンはビューポート 53..202 行にクリップされるので、枠は
     * 常に背景の範囲 (28..227 行) の中にある。 */
    if (u2a_bg_full) {
        u2a_bg_full = 0;
        for (int y = 0; y < 200; y++) {
            const u8 *src = g_u2a_bg + (u32)y * 320;
            u8 *dst = chunky + (u32)(y + U2A_BG_TOP) * U2A_W;
            for (int x = 0; x < 320; x++)
                dst[x] = src[x];
        }
        for (int y = 0; y < U2A_BG_TOP; y++) {
            u8 *dst = chunky + (u32)y * U2A_W;
            for (int x = 0; x < 320; x++)
                dst[x] = 0;
        }
        for (int y = U2A_BG_TOP + 200; y < U2A_H; y++) {
            u8 *dst = chunky + (u32)y * U2A_W;
            for (int x = 0; x < 320; x++)
                dst[x] = 0;
        }
    } else {
        for (int y = ch_prev.y0; y < ch_prev.y1; y++) {
            const u8 *src = g_u2a_bg + (u32)(y - U2A_BG_TOP) * 320;
            u8 *dst = chunky + (u32)y * U2A_W;
            memcpy(dst + ch_prev.x0, src + ch_prev.x0,
                   (unsigned long)(ch_prev.x1 - ch_prev.x0));
        }
    }
    fb.y0 = U2A_H; fb.y1 = 0; fb.x0 = U2A_W; fb.x1 = 0;

    g_dst = chunky;

    /* 有効なオブジェクトの行列と距離 */
    int order[5], onum = 0;
    for (int a = 1; a < 6; a++) {
        if (!co[a].on || !co[a].obj)
            continue;
        apply_camera(&co[a]);
        co[a].dist = center_z(&co[a]);
        order[onum++] = a;
    }

    /* 遠い順に挿入ソート (ペインターズアルゴリズム) */
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

    for (int i = 0; i < onum; i++)
        draw_object(&co[order[i]]);

    if (fb.y1 <= fb.y0 || fb.x1 <= fb.x0)
        rect_none(&fb);
    ch_prev = fb;

    /* c2p: 初めての裏画面は全体、以降はその裏画面の前回の枠と今回の枠 */
    int slot = (u2a_buf[0] == planes) ? 0 : (u2a_buf[1] == planes) ? 1 : -1;
    if (slot < 0) {
        slot = u2a_buf[0] ? 1 : 0;
        u2a_buf[slot] = planes;
        c2p8_fast(chunky, planes, U2A_W, U2A_H);
    } else {
        /* 前回の枠と今回の枠は大きく重なることが多いので、まとめた枠の
         * ほうが変換量 (行 x 32 ドット列) が増えないなら 1 つにする。 */
        const struct u2a_rect *a = &u2a_prev[slot], *b = &fb;
        if (a->y1 <= a->y0 || b->y1 <= b->y0) {
            u2a_conv(chunky, planes, a);
            u2a_conv(chunky, planes, b);
        } else {
            struct u2a_rect m;
            m.y0 = a->y0 < b->y0 ? a->y0 : b->y0;
            m.y1 = a->y1 > b->y1 ? a->y1 : b->y1;
            m.x0 = a->x0 < b->x0 ? a->x0 : b->x0;
            m.x1 = a->x1 > b->x1 ? a->x1 : b->x1;
#define U2A_AREA(r) (((r)->y1 - (r)->y0) * ((((r)->x1 + 31) >> 5) - ((r)->x0 >> 5)))
            if (U2A_AREA(&m) <= U2A_AREA(a) + U2A_AREA(b))
                u2a_conv(chunky, planes, &m);
            else {
                u2a_conv(chunky, planes, a);
                u2a_conv(chunky, planes, b);
            }
#undef U2A_AREA
        }
    }
    u2a_prev[slot] = fb;
    return 1;
}
