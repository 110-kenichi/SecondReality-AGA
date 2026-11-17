/*
 * M4 シーン: DOTS (MiniVectorBalls) — オリジナル DOTS/MAIN.C + DOTS/ASM.ASM の移植
 *
 * - 512 個の玉 (ドット) が 5 つのフェーズで運動する:
 *     0-499    : 渦 (sin/cos で x/y/z を揺らす)
 *     500-899  : 上空から落下
 *     900-1699 : リング状に集まる
 *     1700-2359: ランダム散布 + 重力で落下 (床でバウンド)
 *     2360-2439: パレットを明るくする / 2440-: 白へフェード
 * - 3D は Y 軸回転 + 透視投影 (オリジナルの 16.16 固定小数点演算を C に移植)
 * - 描画は planar 直接 (3x3 の玉 + 足元の影)。c2p 不要。
 * - 背景は下 1/2 のグラデーション (行ごとに固定色) なので、玉の消去は
 *   「その行の背景色で塗る」だけで済む。
 */
#include "scenes.h"
#include "../framework/video.h"
#include "../framework/vblank.h"
#include "../music/music.h"
#include "../framework/dis.h"

#include <proto/exec.h>
#include <exec/memory.h>

#define NDOTS       512
#define PLANE_ROWBYTES 40
#define PLANE_SIZE  (PLANE_ROWBYTES * 256)

/* 2026-11-16 ユーザー指示: 原作の 320x200 をそのまま (縦の換算なし) 28 行目
 * から表示する。以前は縦を 1.28 倍して 256 行全体に描いていた
 * (投影の係数 82 = 64 x 1.28、床のランプを 128 行に引き伸ばし)。 */
#define WIN_Y0      28          /* 320x200 の窓の先頭行 (他パートと同じ) */
#define GROUND_Y    (WIN_Y0 + 100)  /* 原作 MAIN.C:152-155: 100 行目から床 */
#define HORIZON     (WIN_Y0 + 100)  /* 原作 ASM.ASM:103,141: + 100 */
#define DEPTH_BASE  9000
#define SHADOW_A    23          /* 影の色 = 原作 ASM.ASM:113 の 87 = 64+23 (床ランプ
                                 * a=23)。以前は a=12 相当で暗かった
                                 * (2026-11-16 ユーザー指示で原作に合わせた)。 */
/* 6 プレーン化 (2026-11-16 ユーザー指示): 色番号を付け替え、同じ RGB は
 * 同じ番号にまとめて 0..63 に収める。表示される色 (RGB) は以前と同じ。
 *   0      : 黒 (背景ランプで RGB が 0 の段もここ)
 *   1..31  : 背景ランプのグレー (原作の式で 6bit 0..15 に丸まるので最大 15 色)
 *   32..63 : 玉 (奥行き c=0..15、外周 32+2c / 中心 33+2c)
 * 以前は 8-71 = ランプ、128+4c+2/3 = 玉、1 = 白 (未使用のデバッグ用)。
 * プレーン 6, 7 は常に 0 なので、描画と転送は 6 プレーンだけにする。 */
#define DOT_PLANES  6
#define BALL_BASE   32
static u8 ramp_idx[100];        /* 床の行 a=0..99 (原作の色 64+a) -> 色番号 */

typedef struct {
    int x, y, z;
    int yadd;
    int px, py;                 /* 前フレームの画面位置 */
} Dot;

static Dot dots[NDOTS];
static u16 dot_order[NDOTS];
static int sin1024[1024];
static u8 *dots_page_planes[VIDEO_NBUF];
static s16 old_ball_x[VIDEO_NBUF][NDOTS], old_ball_y[VIDEO_NBUF][NDOTS];
static s16 old_shadow_x[VIDEO_NBUF][NDOTS], old_shadow_y[VIDEO_NBUF][NDOTS];
static u8 dots_page_initialized[VIDEO_NBUF];

static u8 palette[256 * 3];
static u8 pal_out[256 * 3];
static u32 rnd_state = 0x12345678;

static int frame, f, j, dropper, grav, gravd, rot, rots, rota;
static int rotcos, rotsin;
static u16 last_row = 0xFFFF;
static u32 dots_last_vbl;
static int dots_step_acc;

/* 原作 DOTS/MAIN.C:150-183 の開始フェード:
 * パレットを黒にし、2 VBL ごとに 64..0 の段階で pal へ近づける。
 * 70Hz の 2 VBL = 50Hz の 10/7 VBL。65 段階。 */
static int start_fade_step;
static int start_fade_acc;
static int start_fade_done;

static u32 rnd(void) {
    rnd_state = rnd_state * 1103515245 + 12345;
    return rnd_state >> 16;
}

static int isin(int deg) { return sin1024[deg & 1023]; }
static int icos(int deg) { return sin1024[(deg + 256) & 1023]; }

/* 背景色 (行ごとのグラデーション) */
static u8 bg_color(int y) {
    if (y < GROUND_Y || y >= WIN_Y0 + 200)
        return 0;
    return ramp_idx[y - GROUND_Y];  /* 原作 MAIN.C:154: 行 100+a は色 64+a */
}

/* 背景を全面塗る (上は黒、下はグレーグラデーション)。
 * 毎フレーム全体を塗り直すことで前フレームの玉も消える。
 * オリジナルは玉ごとに bgpic から旧位置を復元しているが、
 * ここでは全面再描画で同じ結果にする。 */
static void fill_bg(u8 *planes) {
    for (int y = 0; y < 256; y++) {
        u8 c = bg_color(y);
        u8 *p = planes + y * PLANE_ROWBYTES;
        for (int b = 0; b < 8; b++) {
            u32 v = (c & (1 << b)) ? 0xFFFFFFFFu : 0u;
            u32 *q = (u32 *)(p + b * PLANE_SIZE);
            for (int i = 0; i < PLANE_ROWBYTES / 4; i++)
                q[i] = v;
        }
    }
}

/* 同じ 1〜3 画素を 8 プレーンへまとめて更新し、同一バイトの
 * read-modify-write を 1 回にする。 */
static inline void plot_pixels(u8 *planes, int x, int y,
                               const u8 *colors, int count) {
    if ((unsigned)(y - WIN_Y0) >= 200u)    /* 窓 (原作の 0..199 行) の外は描かない */
        return;
    int byte0 = x < 0 ? 0 : x >> 3;
    u8 mask[2] = { 0, 0 };
    u8 bits[2][8] = { { 0 } };
    for (int i = 0; i < count; i++) {
        int px = x + i;
        if ((unsigned)px >= 320u)
            continue;
        int byte = (px >> 3) - byte0;
        if ((unsigned)byte >= 2u)
            continue;
        u8 bit = (u8)(0x80u >> (px & 7));
        mask[byte] |= bit;
        for (int p = 0; p < DOT_PLANES; p++)
            if (colors[i] & (1u << p))
                bits[byte][p] |= bit;
    }

    for (int byte = 0; byte < 2; byte++) {
        if (!mask[byte])
            continue;
        u8 *p = planes + y * PLANE_ROWBYTES + byte0 + byte;
        for (int plane = 0; plane < DOT_PLANES; plane++) {
            u8 *dst = p + plane * PLANE_SIZE;
            dst[0] = (u8)((dst[0] & (u8)~mask[byte]) | bits[byte][plane]);
        }
    }
}

/* 3x3 の玉 (center は明るく) を描く / 消す */
static void ball(u8 *planes, int x, int y, u8 color, u8 center) {
    const u8 edge[3] = { color, color, color };
    const u8 middle[3] = { color, center, color };
    plot_pixels(planes, x - 1, y - 1, edge, 3);
    plot_pixels(planes, x - 1, y, middle, 3);
    plot_pixels(planes, x - 1, y + 1, edge, 3);
}

static int dots_page_index(u8 *planes) {
    for (int page = 0; page < VIDEO_NBUF; page++)
        if (dots_page_planes[page] == planes)
            return page;
    for (int page = 0; page < VIDEO_NBUF; page++) {
        if (!dots_page_planes[page]) {
            dots_page_planes[page] = planes;
            for (int i = 0; i < NDOTS; i++) {
                old_ball_x[page][i] = old_ball_y[page][i] = -1;
                old_shadow_x[page][i] = old_shadow_y[page][i] = -1;
            }
            return page;
        }
    }
    return 0;
}

/* 戻り値: 1 = この裏画面を初めて背景で塗った (8 プレーン全部を写す) */
static int dots_restore_page(u8 *planes, int page) {
    if (!dots_page_initialized[page]) {
        fill_bg(planes);
        dots_page_initialized[page] = 1;
        return 1;
    }

    for (int i = 0; i < NDOTS; i++) {
        int x = old_ball_x[page][i], y = old_ball_y[page][i];
        if (x >= 0) {
            u8 row[3];
            u8 color = bg_color(y - 1);
            row[0] = row[1] = row[2] = color;
            plot_pixels(planes, x - 1, y - 1, row, 3);
            color = bg_color(y);
            row[0] = row[1] = row[2] = color;
            plot_pixels(planes, x - 1, y, row, 3);
            color = bg_color(y + 1);
            row[0] = row[1] = row[2] = color;
            plot_pixels(planes, x - 1, y + 1, row, 3);
        }
        x = old_shadow_x[page][i];
        y = old_shadow_y[page][i];
        if (x >= 0) {
            u8 row[2] = { bg_color(y), bg_color(y) };
            plot_pixels(planes, x, y, row, 2);
        }
    }
    return 0;
}

/* パレット: 0=黒 / 1..=背景グレー / 32..63=玉 (上の DOT_PLANES の説明)。
 * 色の値はオリジナル MAIN.C の式のまま。 */
static void make_palette(void) {
    /* オリジナルの cols[]: b=0 黒, b=1..3 が青緑系 */
    static const u8 cols[4][3] = {
        { 0, 0, 0 },
        { 4, 25, 30 },
        { 8, 40, 45 },
        { 16, 55, 60 },
    };
    for (int i = 0; i < 256 * 3; i++)
        palette[i] = 0;
    /* 背景: オリジナル MAIN.C:140-147 のグレーランプ (palette index 64..163
     * の 100 段)。
     *   c = 64 - 256/(a+4); c = c*c/64; v6 = c/4 (0..15)
     * の無彩色 (最大でも 6bit 15 = 8bit 60)。同じ v6 の段は同じ色番号。 */
    {
        int next = 1;
        int idx_of_v6[16];
        for (int k = 0; k < 16; k++)
            idx_of_v6[k] = -1;
        for (int i = 0; i < 100; i++) {
            int a = i;
            int c = 64 - 256 / (a + 4);
            c = c * c / 64;
            int v6 = c / 4;
            u8 v8 = (u8)(v6 * 255 / 63);
            if (v8 == 0) {
                ramp_idx[i] = 0;            /* 黒 = 0 番と同じ RGB */
                continue;
            }
            if (idx_of_v6[v6] < 0) {
                idx_of_v6[v6] = next++;
                palette[idx_of_v6[v6] * 3 + 0] = v8;
                palette[idx_of_v6[v6] * 3 + 1] = v8;
                palette[idx_of_v6[v6] * 3 + 2] = v8;
            }
            ramp_idx[i] = (u8)idx_of_v6[v6];
        }
    }
    /* 玉: オリジナル MAIN.C の
     *   for(a=0;a<16;a++) for(b=0;b<4;b++) {
     *       c = 100 + a*9;
     *       RGB = cols[b] * (b==0 ? 1 : c/256)
     *   }
     * 玉は b=2 (外周) と b=3 (中心) だけを使う -> 32+2a / 33+2a。 */
    for (int a = 0; a < 16; a++) {
        int c = 100 + a * 9;
        for (int b = 2; b < 4; b++) {
            int ci = BALL_BASE + a * 2 + (b - 2);
            int r = cols[b][0];
            int g = cols[b][1] * c / 256;
            int bl = cols[b][2] * c / 256;
            palette[ci * 3 + 0] = (u8)(r * 255 / 63);
            palette[ci * 3 + 1] = (u8)(g * 255 / 63);
            palette[ci * 3 + 2] = (u8)(bl * 255 / 63);
        }
    }
    video_set_palette(palette);
}

static int dots_armed;
void dots_init(void) {
    dots_armed = 0;
    for (int i = 0; i < VIDEO_NBUF; i++) {
        dots_page_planes[i] = 0;
        dots_page_initialized[i] = 0;
    }
    for (int i = 0; i < 1024; i++) {
        /* sin(i*2pi/1024) * 256 を整数で近似 (テーブルは 1 回だけ生成) */
        int deg = i & 1023;
        int sign = 1;
        if (deg >= 512) { deg -= 512; sign = -1; }
        /* 0..511 を 0..256 の二次近似 (sin の半周期) */
        int t = deg;                        /* 0..511 */
        /* Bhaskara の近似: sin(pi*t/512) = 16u/(5-4u), u = t(512-t)/512^2 */
        int u = t * (512 - t);              /* 0 .. 65536 */
        int num = 16 * u;
        int den = 5 * 262144 - 4 * u;
        sin1024[i] = sign * (num * 256 / den);
    }

    rnd_state = 0x12345678;
    for (int i = 0; i < NDOTS; i++)
        dot_order[i] = (u16)i;
    for (int i = 0; i < 500; i++) {
        int a = (int)(rnd() % NDOTS);
        int b = (int)(rnd() % NDOTS);
        u16 t = dot_order[a];
        dot_order[a] = dot_order[b];
        dot_order[b] = t;
    }

    dropper = 22000;
    for (int i = 0; i < NDOTS; i++) {
        dots[i].x = 0;
        dots[i].y = 2560 - dropper;
        dots[i].z = 0;
        dots[i].yadd = 0;
        dots[i].px = dots[i].py = -1;
    }
    for (int i = 0; i < 500; i++) {
        int a = (int)(rnd() % NDOTS);
        int b = (int)(rnd() % NDOTS);
        int t;
        t = dots[a].x; dots[a].x = dots[b].x; dots[b].x = t;
        t = dots[a].y; dots[a].y = dots[b].y; dots[b].y = t;
        t = dots[a].z; dots[a].z = dots[b].z; dots[b].z = t;
    }

    frame = 0; f = 0; j = 0;
    grav = 3; gravd = 13;
    rot = 0; rots = 0; rota = -64;
    last_row = 0xFFFF;
    dots_last_vbl = vblank_frames();
    dots_step_acc = 0;
    start_fade_step = 0;
    start_fade_acc = 0;
    start_fade_done = 0;
    make_palette();
    /* 原作 MAIN.C:150-151: 開始時は全色 0 (黒) */
    for (int i = 0; i < 768; i++)
        pal_out[i] = 0;
    video_set_palette(pal_out);
}

/* ---- original 70 Hz timing on PAL 50 Hz ----
 * MAIN.C advances one simulation frame per dis_waitb() return, and the PC
 * version runs at 70 Hz.  On PAL we must run 70/50 = 1.4 steps per VBL,
 * otherwise the balls move at 71% speed. */
/* 原作 DOTS/MAIN.C:185-195: while(frame<2450) { ...
 *   if(dis_indemo()) { a=dis_musplus(); if(a>-4 && a<0) break; } }
 * 以前は 2450 フレームだけで終えていたため、音楽マーカー (std 74:60)
 * より約 2.4 秒遅れ、次の RAYSCRL の開始待ち (musplus>=0) を逃して
 * 真っ黒のままになっていた (2026-11-14 実測)。 */
static int dots_armed;
int dots_finished(void) {
    if (start_fade_done) {
        /* 描画が遅い実機で -3..-1 の 3 行を見逃しても止まらないように
         * dis_mus_reached を使う (2026-11-15)。 */
        if (dis_mus_reached(&dots_armed, -4))
            return 1;
    }
    return frame >= 2450;
}

static void dots_physics(void)
{
    for (int i = 0; i < NDOTS; i++) {
        Dot *d = &dots[i];

        /* gravity + bounce (original ASM.ASM @@4) */
        d->yadd += grav;
        int y = d->y + d->yadd;
        if (y >= 8105) {
            int v = -d->yadd;
            v = v * gravd / 16;
            d->yadd = v;
            y = y + d->yadd;
        }
        d->y = y;
    }
}

static void dots_step(void)
{
    frame++;
    if (frame == 500)
        f = 0;
    {
        int i = dot_order[j];
        j++;
        if (j >= NDOTS)
            j = 0;
        Dot *d = &dots[i];
        if (frame < 500) {
            d->x = isin(f * 11) * 40;
            d->y = icos(f * 13) * 10 - dropper;
            d->z = isin(f * 17) * 40;
            d->yadd = 0;
        } else if (frame < 900) {
            d->x = icos(f * 15) * 55;
            d->y = dropper;
            d->z = isin(f * 15) * 55;
            d->yadd = -260;
        } else if (frame < 1700) {
            int a = sin1024[frame & 1023] / 8;
            d->x = icos(f * 66) * a;
            d->y = 8000;
            d->z = isin(f * 66) * a;
            d->yadd = -300;
        } else if (frame < 2360) {
            d->x = (int)(rnd() & 0x7FFF) - 16384;
            d->y = 8000 - (int)(rnd() / 2);
            d->z = (int)(rnd() & 0x7FFF) - 16384;
            d->yadd = 0;
            if (frame > 1900 && !(frame & 31) && grav > 0)
                grav--;
        } else if (frame < 2400) {
            /* MAIN.C:242-257: pal2 = pal + a*3 (R,G) / a*4 (B), 上限 63。
             * 原作は 6bit (0..63) の VGA DAC 値で、毎回 **元の pal** から
             * 作る。こちらの palette は 8bit なので 6bit に戻して計算し、
             * 8bit へ変換して pal_out に出す (palette 自体は変更しない)。
             * 以前は 8bit 値に 6bit の上限 63 を掛け、さらに palette を
             * 累積で書き換えていたため、白ではなく暗い灰色で止まっていた。 */
            int a = frame - 2360;
            for (int b = 0; b < 768; b += 3) {
                for (int k = 0; k < 3; k++) {
                    int c = (palette[b + k] * 63 + 127) / 255 +
                            a * (k == 2 ? 4 : 3);
                    if (c > 63) c = 63;
                    pal_out[b + k] = (u8)(c * 255 / 63);
                }
            }
            video_set_palette(pal_out);
        } else if (frame < 2440) {
            /* MAIN.C:258-269: 全色 63-a*2 (6bit) で白から黒へ。 */
            int a = frame - 2400;
            int c = 63 - a * 2;
            if (c < 0) c = 0;
            for (int b = 0; b < 768; b++)
                pal_out[b] = (u8)(c * 255 / 63);
            video_set_palette(pal_out);
        }
        if (dropper > 4000)
            dropper -= 100;
        rotcos = icos(rot) * 64;
        rotsin = isin(rot) * 64;
        rots += 2;
        if (frame > 1900) {
            rot += rota / 64;
            rota--;
        } else {
            rot = isin(rots);
        }
        f++;
    }

    dots_physics();
}

static void dots_draw(u8 *planes, int page)
{
    for (int i = 0; i < NDOTS; i++) {
        Dot *d = &dots[i];
        old_ball_x[page][i] = old_ball_y[page][i] = -1;
        old_shadow_x[page][i] = old_shadow_y[page][i] = -1;

        int depth = ((d->z * rotcos - d->x * rotsin) >> 16) + DEPTH_BASE;
        if (depth < 64)
            depth = 64;
        int v = d->x * rotcos + d->z * rotsin;
        int sx = 160 + ((v >> 8) + (v >> 11)) / depth;
        int sy = HORIZON + (d->y * 64) / depth;     /* 原作 ASM.ASM:137-141 */

        if ((unsigned)sx < 320u && (unsigned)(sy - WIN_Y0) < 200u) {
            int shy = HORIZON + (8 * 65536) / depth;  /* 原作 ASM.ASM:100-104 */
            if ((unsigned)(shy - WIN_Y0) < 200u) {
                const u8 shadow[2] = { ramp_idx[SHADOW_A], ramp_idx[SHADOW_A] };
                plot_pixels(planes, sx, shy, shadow, 2);
                old_shadow_x[page][i] = (s16)sx;
                old_shadow_y[page][i] = (s16)shy;
            }

            int a = depth >> 8;
            if (a < 0)
                a = 0;
            else if (a > 127)
                a = 127;
            int c = a - 31;
            c = c * 3 / 4;
            c += 8;
            if (c < 0)
                c = 0;
            else if (c > 15)
                c = 15;
            c = 15 - c;
            u8 col = (u8)(BALL_BASE + 2 * c);        /* 以前の 128+2+4c */
            u8 colc = (u8)(BALL_BASE + 1 + 2 * c);   /* 以前の 128+3+4c */
            ball(planes, sx, sy, col, colc);
            old_ball_x[page][i] = (s16)sx;
            old_ball_y[page][i] = (s16)sy;
            d->px = sx;
            d->py = sy;
        } else {
            d->px = -1;
            d->py = -1;
        }
    }
}

int dots_render(u8 *chunky, u8 *planes, int width, int height) {
    (void)chunky;
    (void)width;
    (void)height;

    int page = dots_page_index(planes);
    /* 初めての裏画面は fill_bg がプレーン 6, 7 も 0 にしたので 8 枚とも
     * チップ RAM へ写す (前のパートの絵を消す)。以後プレーン 6, 7 は
     * 変わらないので 6 枚だけ写す。 */
    if (dots_restore_page(planes, page))
        video_set_commit_area(8, 0, 256);
    else
        video_set_commit_area(DOT_PLANES, WIN_Y0, 200);

    /* run as many 70 Hz simulation ticks as the PAL VBLs that elapsed */
    u32 now = video_frame_vbl();
    u32 el = now - dots_last_vbl;
    if (el > 6)
        el = 6;
    dots_last_vbl = now;

    /* 開始フェード (MAIN.C:150-183)。この間は背景だけを出し、
     * 玉のシミュレーションは始めない。 */
    if (!start_fade_done) {
        start_fade_acc += (int)el * 7;
        while (start_fade_acc >= 10) {
            start_fade_acc -= 10;
            if (start_fade_step < 64) {
                start_fade_step++;
            } else {
                start_fade_done = 1;
                break;
            }
        }
        {
            int b = 64 - start_fade_step;
            int sub = b * 255 / 63;
            for (int i = 0; i < 768; i++) {
                int v = palette[i] - sub;
                pal_out[i] = (u8)(v > 0 ? v : 0);
            }
            video_set_palette(pal_out);
        }
        if (!start_fade_done)
            return 1;
        video_set_palette(palette);
        dots_step_acc = 0;
        el = 1;                 /* フェード中は玉を進めていない */
    }

    dots_step_acc += (int)el * 7;
    int steps = dots_step_acc / 5;
    dots_step_acc -= steps * 5;
    if (steps > 8)
        steps = 8;
    for (int s = 0; s < steps; s++)
        dots_step();

    dots_draw(planes, page);
    return 1;
}
