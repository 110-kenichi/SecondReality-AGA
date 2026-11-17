/*
 * Second Reality A1200: PLZPART plasmacube
 *
 * Original: reference/SecondReality/PLZPART/VECT.C + PLZFILL.C + PLZA.ASM
 *           reference/SecondReality/PLZPART/SPLINE.ASM + RATA.INC
 * Reference: XorJS/SecondRealityPlusPlus Parts/PLZPART/PLZ.cpp
 *
 *   - 8-point cube (+-125), 6 quad faces
 *   - scanline DDA textured polygons (PLZA.ASM style: fixed point, no
 *     per-pixel division)
 *   - camera/zoom/rotation driven by the original spline (getspl), so the
 *     cube slides in, grows, and finally shrinks like the original part
 *   - texture scroll amount follows the original: dseg = seg + dd*16
 *     (segment units) == dd rows/frame
 *   - all timing is derived from the PAL 50 Hz VBL counter, so a dropped
 *     frame does not slow the motion down
 */
#include "scenes.h"
#include "../framework/video.h"
#include "../framework/vblank.h"
#include "../framework/dis.h"
#include <string.h>

extern const u8 *g_cube_tex;
extern const u8 *g_cube_pal;
extern const u8 *g_cube_spline;
extern const u8 *g_cube_splcoef;
extern const u8 *g_cube_sin;

#define CW          320
#define CH          200
#define CUBE_TOP    ((SCREEN_H - CH) / 2)
#define TEX_W       256
#define TEX_STRIDE  32768
#define NSP         137         /* RATA.INC control points */

static int sin14[1024];
static u32 start_vbl;
static int started;
static int clear_full_frame;

typedef struct { int x, y, z; } V3;
static const V3 cube_pts[8] = {
    { 125,  125,  125}, { 125, -125,  125}, {-125, -125,  125}, {-125,  125,  125},
    { 125,  125, -125}, { 125, -125, -125}, {-125, -125, -125}, {-125,  125, -125},
};
static const int cube_face[6][4] = {
    {1, 2, 3, 0}, {7, 6, 5, 4}, {0, 4, 5, 1},
    {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0},
};
static const int cube_color[6] = {0, 0, 1, 2, 1, 2};
static const int tex_u[4] = {64, 190, 190, 64};
static const int tex_v[4] = {4, 4, 60, 60};

static int s16be(const u8 *p) {
    return (s16)((p[0] << 8) | p[1]);
}

/* ---- spline (SPLINE.ASM getspl) ---- */
static int sp[NSP + 8][8];
static int sc[1024];

static int ps_tx, ps_ty, ps_dis, ps_kx, ps_ky, ps_kz, ps_lskx, ps_lsky;

static void spl_eval(int pos) {
    int i = pos >> 8;
    int t = pos & 255;
    if (i > NSP - 4)
        i = NSP - 4;
    if (i < 0)
        i = 0;
    int o[8];
    for (int c = 0; c < 8; c++) {
        long s = (long)sp[i + 3][c] * sc[t]
               + (long)sp[i + 2][c] * sc[t + 256]
               + (long)sp[i + 1][c] * sc[t + 512]
               + (long)sp[i + 0][c] * sc[t + 768];
        if (s < 0)
            s += 32767;
        o[c] = (int)(s >> 15);
    }
    ps_tx = o[0];
    ps_ty = o[1];
    ps_dis = o[2];
    ps_kx = o[3];
    ps_ky = o[4];
    ps_kz = o[5];
    ps_lskx = o[6];         /* SPLINE.ASM:66-69: ls_kx, ls_ky (光源の向き) */
    ps_lsky = o[7];
}

/* 原作 VECT.C:143-145,207-241 + PLZA.ASM shadepal (264-286):
 *   ls_y = kosinit[ls_kx]>>8; ls_x = (sinit[ls_kx]>>8)*(sinit[ls_ky]>>8)>>7;
 *   ls_z = (sinit[ls_kx]>>8)*(kosinit[ls_ky]>>8)>>7;
 *   見える面ごとに s = (ls . n)/250000 + 32 を求め、その面の色の
 *   パレット 64 色を pal*s>>6 にする (fpal)。
 * 光源がスプラインで回るので、キューブの最後は面が暗くなっていく。
 * 以前はこの処理が無く、パレットが固定で暗くならなかった
 * (2026-11-14 ユーザー指摘: 最後にフェードアウトしない)。 */
static u8 fpal[768];

static int plzcub_armed;
void plzcub_init(void) {
    plzcub_armed = 0;
    clear_full_frame = 1;
    for (int i = 0; i < 1024; i++)
        sin14[i] = s16be(g_cube_sin + i * 2);

    if (g_cube_spline) {
        for (int i = 0; i < NSP; i++)
            for (int c = 0; c < 8; c++)
                sp[i][c] = s16be(g_cube_spline + i * 16 + c * 2);
    }
    if (g_cube_splcoef) {
        for (int i = 0; i < 1024; i++)
            sc[i] = s16be(g_cube_splcoef + i * 2);
    }

    start_vbl = vblank_frames();
    started = 1;
    video_set_palette(g_cube_pal);
}

/* ---- scanline DDA textured triangle ---- */
static int svx[3], svy[3], su[3], sv[3];
static const u8 *stex;
static int svadd;
static int shade_tab[128];      /* dist1[] equivalent: 64-row light bands */

static void __attribute__((noinline)) fill_tri(u8 *chunky)
{
    int i0 = 0, i1 = 1, i2 = 2, t;

    if (svy[i0] > svy[i1]) { t = i0; i0 = i1; i1 = t; }
    if (svy[i1] > svy[i2]) { t = i1; i1 = i2; i2 = t; }
    if (svy[i0] > svy[i1]) { t = i0; i0 = i1; i1 = t; }

    int ya = svy[i0], yb = svy[i1], yc = svy[i2];
    int dyL = yc - ya;
    if (dyL <= 0)
        return;

    int ystart = ya < CUBE_TOP ? CUBE_TOP : ya;
    int yend = yc > CUBE_TOP + CH ? CUBE_TOP + CH : yc;
    if (ystart >= yend)
        return;

    /* long edge: i0 -> i2 */
    long lx = (long)svx[i0] << 16;
    long lu = (long)su[i0] << 16;
    long lv = (long)sv[i0] << 16;
    long ldx = ((long)(svx[i2] - svx[i0]) << 16) / dyL;
    long ldu = ((long)(su[i2] - su[i0]) << 16) / dyL;
    long ldv = ((long)(sv[i2] - sv[i0]) << 16) / dyL;
    lx += ldx * (ystart - ya);
    lu += ldu * (ystart - ya);
    lv += ldv * (ystart - ya);

    /* short edge: i0 -> i1 (upper), then i1 -> i2 (lower) */
    int dyS1 = yb - ya;
    int dyS2 = yc - yb;
    long sx, suu, svv, sdx, sdu, sdv;

    if (dyS1 > 0) {
        sx = (long)svx[i0] << 16;
        suu = (long)su[i0] << 16;
        svv = (long)sv[i0] << 16;
        sdx = ((long)(svx[i1] - svx[i0]) << 16) / dyS1;
        sdu = ((long)(su[i1] - su[i0]) << 16) / dyS1;
        sdv = ((long)(sv[i1] - sv[i0]) << 16) / dyS1;
        sx += sdx * (ystart - ya);
        suu += sdu * (ystart - ya);
        svv += sdv * (ystart - ya);
    } else {
        if (dyS2 <= 0)
            return;
        sx = (long)svx[i1] << 16;
        suu = (long)su[i1] << 16;
        svv = (long)sv[i1] << 16;
        sdx = ((long)(svx[i2] - svx[i1]) << 16) / dyS2;
        sdu = ((long)(su[i2] - su[i1]) << 16) / dyS2;
        sdv = ((long)(sv[i2] - sv[i1]) << 16) / dyS2;
        sx += sdx * (ystart - yb);
        suu += sdu * (ystart - yb);
        svv += sdv * (ystart - yb);
    }

    for (int y = ystart; y < yend; y++) {
        if (dyS1 > 0 && y >= yb) {
            dyS1 = 0;
            sx = (long)svx[i1] << 16;
            suu = (long)su[i1] << 16;
            svv = (long)sv[i1] << 16;
            sdx = ((long)(svx[i2] - svx[i1]) << 16) / dyS2;
            sdu = ((long)(su[i2] - su[i1]) << 16) / dyS2;
            sdv = ((long)(sv[i2] - sv[i1]) << 16) / dyS2;
            sx += sdx * (y - yb);
            suu += sdu * (y - yb);
            svv += sdv * (y - yb);
        }

        long xl = lx, xr = sx;
        long ul = lu, ur = suu;
        long vl = lv, vr = svv;
        if (xl > xr) {
            long tmp;
            tmp = xl; xl = xr; xr = tmp;
            tmp = ul; ul = ur; ur = tmp;
            tmp = vl; vl = vr; vr = tmp;
        }

        int px0 = (int)(xl >> 16);
        int px1 = (int)(xr >> 16);
        int span = px1 - px0;
        if (span <= 0) {
            lx += ldx; lu += ldu; lv += ldv;
            sx += sdx; suu += sdu; svv += sdv;
            continue;
        }

        long du = (ur - ul) / span;
        long dv = (vr - vl) / span;

        int x0c = px0 < 0 ? 0 : px0;
        int x1c = px1 > CW ? CW : px1;
        if (x0c >= x1c) {
            lx += ldx; lu += ldu; lv += ldv;
            sx += sdx; suu += sdu; svv += sdv;
            continue;
        }

        if (x0c != px0) {
            long d = x0c - px0;
            ul += du * d;
            vl += dv * d;
        }

        u8 *row = chunky + y * CW + x0c;
        int n = x1c - x0c;
        long u = ul, v = vl;
        for (int i = 0; i < n; i++) {
            int uu = (int)(u >> 16);
            int vv = (int)(v >> 16) & 127;
            /* PLZA.ASM: al = [ds:bx+si]  (bx = v*256+u, si = dist1 value)
             * -> the texture read is displaced horizontally per texel row. */
            int sh = shade_tab[vv];
            int o = (int)stex[vv * TEX_W + ((uu + sh) & (TEX_W - 1))];
            row[i] = (u8)o;
            u += du;
            v += dv;
        }

        lx += ldx; lu += ldu; lv += ldv;
        sx += sdx; suu += sdu; svv += sdv;
    }
}

/* 原作 VECT.C:117-120: while(!dis_exit()) { a=dis_musplus();
 * if(a>=-4 && a<0) break; ... } -- キューブは音楽マーカーの 4 行前で終わる。
 * 以前はスプライン点 36 (SPL_END_POS) で終えていたが、MUSIC1 の曲データ
 * から計算すると原作の終了 (std 64:60) は点 35.2 で、残り約 0.7 秒の間
 * キューブが正面を向いて止まって見えた (2026-11-14 ユーザー指摘)。 */
static int plzcub_armed;
int plzcub_finished(void) {
    if (!started)
        return 0;
    /* 原作は -4..-1 の 4 行の窓。遅い実機で見逃しても止まらないように
     * dis_mus_reached を使う (2026-11-15)。 */
    return dis_mus_reached(&plzcub_armed, -5);
}

int plzcub_render(u8 *chunky) {
    if (clear_full_frame) {
        memset(chunky, 0, CW * SCREEN_H);
        clear_full_frame = 0;
    } else {
        memset(chunky + CUBE_TOP * CW, 0, CW * CH);
    }

    if (!g_cube_tex || !g_cube_pal || !g_cube_sin) {
        for (int i = 0; i < CW * SCREEN_H; i++)
            chunky[i] = 63;
        return 0;
    }

    /* PAL 50 Hz: real time, so a missed frame does not slow the motion.
     * Original: getspl(4*256 + frames*4) with frames at 70 Hz.
     * 4 units/frame * 70 = 280 per second -> (t * 28) / 5 per VBL. */
    u32 t = video_frame_vbl() - start_vbl;
    int pos = 1024 + (int)((t * 28u) / 5u);
    spl_eval(pos);

    int tx = ps_tx, ty = ps_ty;
    int dist = ps_dis;
    int kx = ps_kx & 1023;
    int ky = ps_ky & 1023;
    int kz = ps_kz & 1023;

    int CX = sin14[(kx + 256) & 1023], SX = sin14[kx];
    int CY = sin14[(ky + 256) & 1023], SY = sin14[ky];
    int CZ = sin14[(kz + 256) & 1023], SZ = sin14[kz];

    int cxx = (CY * CZ) >> 14;
    int cxy = (CY * SZ) >> 14;
    int cxz = -SY;
    int cyx = (((SX * CZ) >> 14) * SY >> 14) - ((CX * SZ) >> 14);
    int cyy = (((SX * SY) >> 14) * SZ >> 14) + ((CX * CZ) >> 14);
    int cyz = (CY * SX) >> 14;
    int czx = (((CX * CZ) >> 14) * SY >> 14) + ((SX * SZ) >> 14);
    int czy = (((CX * SY) >> 14) * SZ >> 14) - ((SX * CZ) >> 14);
    int czz = (CY * CX) >> 14;

    int px[8], py[8], wz[8];
    int wx3[8], wy3[8], wz3[8];
    for (int i = 0; i < 8; i++) {
        int x = cube_pts[i].x, y = cube_pts[i].y, z = cube_pts[i].z;
        int rx = (x * cxx + y * cxy + z * cxz) >> 14;
        int ry = (x * cyx + y * cyy + z * cyz) >> 14;
        int rz = (x * czx + y * czy + z * czz) >> 14;
        wx3[i] = rx + tx;
        wy3[i] = ry + ty;
        wz3[i] = rz + dist;
        int zz = rz + dist;
        /* VECT.C:198-199 projects x with 256 in a 640-pixel viewport;
         * halve it for 320 pixels. Keep its y focal length 142. */
        px[i] = ((rx + tx) * 128) / zz + CW / 2;
        /* Main's standard 200-line area is y=28..227 (main.c:702). */
        py[i] = ((ry + ty) * 142) / zz + CUBE_TOP + CH / 2;
        wz[i] = zz;
        if (px[i] < -3000) px[i] = -3000;
        if (px[i] > 3000) px[i] = 3000;
        if (py[i] < -3000) py[i] = -3000;
        if (py[i] > 3000) py[i] = 3000;
    }

    /* 光源シェーディング (VECT.C:207-241)。sin14 は 16384 振幅なので
     * 原作の sinit>>8 (128 振幅) は sin14>>7。 */
    {
        int lkx = ps_lskx & 1023, lky = ps_lsky & 1023;
        long ls_y = sin14[(lkx + 256) & 1023] >> 7;
        long ls_x = (long)(sin14[lkx] >> 7) * (sin14[lky] >> 7) >> 7;
        long ls_z = (long)(sin14[lkx] >> 7) * (sin14[(lky + 256) & 1023] >> 7) >> 7;
        for (int i = 0; i < 768; i++)
            fpal[i] = g_cube_pal[i];
        for (int f = 0; f < 6; f++) {
            int p1 = cube_face[f][0], p2 = cube_face[f][1], p3 = cube_face[f][2];
            long x = wx3[p1], y = wy3[p1], z = wz3[p1];
            long ax = wx3[p2] - x, ay = wy3[p2] - y, az = wz3[p2] - z;
            long bx = wx3[p3] - x, by = wy3[p3] - y, bz = wz3[p3] - z;
            long nx = ay * bz - az * by;
            long ny = az * bx - ax * bz;
            long nz = ax * by - ay * bx;
            if (-x * nx - y * ny - z * nz > 0)
                continue;                       /* 裏面 */
            long sh = (ls_x * nx + ls_y * ny + ls_z * nz) / 250000 + 32;
            int c = cube_color[f];
            for (int i = 0; i < 64 * 3; i++)
                fpal[c * 64 * 3 + i] = (u8)((((
                    (g_cube_pal[c * 64 * 3 + i] >> 2) * (sh & 0xff)) >> 6)
                    & 63) << 2);        /* 6bit DAC で計算 (mul dl; shr 6) */
        }
        video_set_palette(fpal);
    }

    int order[6] = {0, 1, 2, 3, 4, 5};
    int zavg[6];
    for (int f = 0; f < 6; f++)
        zavg[f] = (wz[cube_face[f][0]] + wz[cube_face[f][1]] +
                   wz[cube_face[f][2]] + wz[cube_face[f][3]]) / 4;
    for (int i = 0; i < 6; i++) {
        int best = i;
        for (int j = i + 1; j < 6; j++)
            if (zavg[order[j]] > zavg[order[best]])
                best = j;
        if (best != i) {
            int t2 = order[i]; order[i] = order[best]; order[best] = t2;
        }
    }

    /* dist1[(ty+dd)] = sini[(ty+dd)*8]/3 : dd advances 1 row per 70Hz
     * frame -> (t*7)/5 rows per PAL VBL.  Range +-42 texels (of 256). */
    svadd = (int)((t * 7u) / 5u) & 63;
    for (int i = 0; i < 128; i++)
        shade_tab[i] = sin14[((i + svadd) & 63) * 16] * 42 / 16384;

    for (int k = 0; k < 6; k++) {
        int f = order[k];
        int i0 = cube_face[f][0], i1 = cube_face[f][1];
        int i2 = cube_face[f][2], i3 = cube_face[f][3];
        int color = cube_color[f];
        stex = g_cube_tex + color * TEX_STRIDE;

        svx[0] = px[i0]; svy[0] = py[i0]; su[0] = tex_u[0]; sv[0] = tex_v[0];
        svx[1] = px[i1]; svy[1] = py[i1]; su[1] = tex_u[1]; sv[1] = tex_v[1];
        svx[2] = px[i2]; svy[2] = py[i2]; su[2] = tex_u[2]; sv[2] = tex_v[2];
        fill_tri(chunky);

        svx[0] = px[i0]; svy[0] = py[i0]; su[0] = tex_u[0]; sv[0] = tex_v[0];
        svx[1] = px[i2]; svy[1] = py[i2]; su[1] = tex_u[2]; sv[1] = tex_v[2];
        svx[2] = px[i3]; svy[2] = py[i3]; su[2] = tex_u[3]; sv[2] = tex_v[3];
        fill_tri(chunky);
    }

    return 0;
}
