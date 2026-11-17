#include "c2p.h"
#include "vblank.h"
#include <string.h>

/*
 * 8 プレーン c2p
 *
 * c2p8      : 乗算トリック版 (可読性重視・基準実装)
 * c2p8_fast : 4x4 ビット転置版 (高速)
 *
 * どちらも出力は完全に同一。
 */

/* ---- 基準実装: 乗算トリック ---- */
#define MAGIC 0x01020408u

static inline u32 gather4(u32 v, int plane) {
    u32 t = (v >> plane) & 0x01010101u;
    return (t * MAGIC) >> 24;
}

void c2p8(const u8 *src, u8 *planes, int width, int height) {
    int rowbytes = width >> 3;
    int plane_size = rowbytes * height;

    for (int y = 0; y < height; y++) {
        const u32 *s = (const u32 *)(src + (u32)y * width);
        u8 *d0 = planes + 0 * plane_size + y * rowbytes;
        u8 *d1 = planes + 1 * plane_size + y * rowbytes;
        u8 *d2 = planes + 2 * plane_size + y * rowbytes;
        u8 *d3 = planes + 3 * plane_size + y * rowbytes;
        u8 *d4 = planes + 4 * plane_size + y * rowbytes;
        u8 *d5 = planes + 5 * plane_size + y * rowbytes;
        u8 *d6 = planes + 6 * plane_size + y * rowbytes;
        u8 *d7 = planes + 7 * plane_size + y * rowbytes;

        for (int x = 0; x < width; x += 8) {
            u32 v0 = *s++;
            u32 v1 = *s++;
            *d0++ = (u8)((gather4(v0, 0) << 4) | gather4(v1, 0));
            *d1++ = (u8)((gather4(v0, 1) << 4) | gather4(v1, 1));
            *d2++ = (u8)((gather4(v0, 2) << 4) | gather4(v1, 2));
            *d3++ = (u8)((gather4(v0, 3) << 4) | gather4(v1, 3));
            *d4++ = (u8)((gather4(v0, 4) << 4) | gather4(v1, 4));
            *d5++ = (u8)((gather4(v0, 5) << 4) | gather4(v1, 5));
            *d6++ = (u8)((gather4(v0, 6) << 4) | gather4(v1, 6));
            *d7++ = (u8)((gather4(v0, 7) << 4) | gather4(v1, 7));
        }
    }
}

/* ---- 高速実装: 4x4 ビット転置 ----
 *
 * 4 ピクセル (u32) の各バイト = 1 ピクセル (bit p = プレーン p)。
 * 低ニブル (プレーン 0-3) と高ニブル (プレーン 4-7) に分け、
 * それぞれ 4x4 ビット転置して「プレーンごとの 4bit (ピクセル順)」を得る。
 * 転置はバタフライ 3 段 (距離 7/14/21) で行う。
 * 検証: tools/c2p_transpose_check.py (参照実装と完全一致)
 */

static inline u32 transpose4(u32 x) {
    u32 t;
    t = (x ^ (x >> 7)) & 0x00080402u;
    x ^= t ^ (t << 7);
    t = (x ^ (x >> 14)) & 0x00000804u;
    x ^= t ^ (t << 14);
    t = (x ^ (x >> 21)) & 0x00000008u;
    x ^= t ^ (t << 21);
    return x;
}

/* a のバイト i のニブルを上位 4bit、b のバイト i のニブルを下位 4bit へ */
static inline u8 pack_nib(u32 a, u32 b, int i) {
    return (u8)((((a >> (8 * i)) & 0x0Fu) << 4) | ((b >> (8 * i)) & 0x0Fu));
}

/* ---- マージ方式 (32 ピクセル = 8 ロングを 5 段の交換で 8 プレーンへ) ----
 * MERGE(a,b,mask,s): a の s ビット側と b を入れ替える定番の操作。
 * 段 (シフト, 相手の番号の差) = (16,4) (8,2) (4,1) (2,4) (1,2) で、
 * 結果のプレーン p は w[7,5,3,1,6,4,2,0][p] に入る
 * (tools/ 相当の Python 総当たりで求め、乱数 2000 組で検証済み)。
 * 以前の 4x4 転置 + 1 バイトずつ組み立てる方式より演算が約 1/3 で、
 * チップ RAM へは 32bit で書く (実機 68060 の速度対策、2026-11-15)。 */
#define MERGE(a, b, m, sh) do { u32 t_ = (((b) >> (sh)) ^ (a)) & (m);                                 (a) ^= t_; (b) ^= t_ << (sh); } while (0)

static void c2p_rows(const u8 *src, u8 *planes, int width, int plane_size,
                     int y0, int rows) {
    int rowbytes = width >> 3;
    for (int y = y0; y < y0 + rows; y++) {
        const u32 *s = (const u32 *)(src + (u32)(y - y0) * width);
        u32 *d = (u32 *)(planes + y * rowbytes);
        const int ps = plane_size >> 2;          /* ロング単位 */
        for (int x = 0; x < width; x += 32) {
            u32 w0 = s[0], w1 = s[1], w2 = s[2], w3 = s[3];
            u32 w4 = s[4], w5 = s[5], w6 = s[6], w7 = s[7];
            s += 8;
            MERGE(w0, w4, 0x0000ffffu, 16); MERGE(w1, w5, 0x0000ffffu, 16);
            MERGE(w2, w6, 0x0000ffffu, 16); MERGE(w3, w7, 0x0000ffffu, 16);
            MERGE(w0, w2, 0x00ff00ffu, 8);  MERGE(w1, w3, 0x00ff00ffu, 8);
            MERGE(w4, w6, 0x00ff00ffu, 8);  MERGE(w5, w7, 0x00ff00ffu, 8);
            MERGE(w0, w1, 0x0f0f0f0fu, 4);  MERGE(w2, w3, 0x0f0f0f0fu, 4);
            MERGE(w4, w5, 0x0f0f0f0fu, 4);  MERGE(w6, w7, 0x0f0f0f0fu, 4);
            MERGE(w0, w4, 0x33333333u, 2);  MERGE(w1, w5, 0x33333333u, 2);
            MERGE(w2, w6, 0x33333333u, 2);  MERGE(w3, w7, 0x33333333u, 2);
            MERGE(w0, w2, 0x55555555u, 1);  MERGE(w1, w3, 0x55555555u, 1);
            MERGE(w4, w6, 0x55555555u, 1);  MERGE(w5, w7, 0x55555555u, 1);
            d[0 * ps] = w7; d[1 * ps] = w5; d[2 * ps] = w3; d[3 * ps] = w1;
            d[4 * ps] = w6; d[5 * ps] = w4; d[6 * ps] = w2; d[7 * ps] = w0;
            d++;
        }
    }
}

/* アセンブラ版 (framework/c2p_merge.asm、tools/gen_c2p_asm.py で生成)。
 * 幅 320、プレーン間隔 10240 バイト (320x256) 専用。演算は c2p_rows と
 * 同じで出力も同じ。チップ RAM への 8 回の書き込みを、次のブロックの
 * 計算の間に 1〜2 回ずつ挟む。実機 68060 の bench (2026-11-15) で
 * c2p 297 走査線に対しチップ RAM へ書くだけで 225 だったため、その差を
 * 詰める目的。 */
void c2p_merge320(const u8 *src, u8 *dst, u32 nblocks);
void c2p_merge320_4(const u8 *src, u8 *dst, u32 nblocks);

static void c2p_dispatch(const u8 *src, u8 *planes, int width, int plane_size,
                         int y0, int rows) {
    if (width == 320 && plane_size == 10240) {
        c2p_merge320(src, planes + y0 * 40, (u32)rows * 10u);
        return;
    }
    c2p_rows(src, planes, width, plane_size, y0, rows);
}

/* width は 32 の倍数であること (320 / 640)。 */
void c2p8_fast(const u8 *src, u8 *planes, int width, int height) {
    c2p_dispatch(src, planes, width, (width >> 3) * height, 0, height);
}

/* 画面 (高さ plane_h) の y0 行目から rows 行だけを変換する。src は rows 行分。 */
void c2p8_rect(const u8 *src, u8 *planes, int width, int plane_h, int y0, int rows) {
    c2p_dispatch(src, planes, width, (width >> 3) * plane_h, y0, rows);
}

/* 320x256 画面 (プレーン間隔 10240) の y0 行目から rows 行について、
 * 32 ドット単位の列 bx0..bx1-1 だけを変換する。src は rows 行分
 * (1 行 320 バイト) の先頭。範囲外の列のプレーンは書かない。
 * JPLOGO のように絵が画面の中央にしか無いパート用 (2026-11-15)。 */
void c2p8_cols(const u8 *src, u8 *planes, int y0, int rows, int bx0, int bx1) {
    if (bx1 <= bx0)
        return;
    if (bx0 == 0 && bx1 == 10) {
        c2p_merge320(src, planes + y0 * 40, (u32)rows * 10u);
        return;
    }
    for (int r = 0; r < rows; r++)
        c2p_merge320(src + r * 320 + bx0 * 32, planes + (y0 + r) * 40 + bx0 * 4,
                     (u32)(bx1 - bx0));
}

/* 320x256 専用: 下位4プレーンだけを書き込み、上位4プレーンは保持する。 */
void c2p4_fast(const u8 *src, u8 *planes) {
    c2p_merge320_4(src, planes, 2560u);
}

void c2p4_cols(const u8 *src, u8 *planes, int y0, int rows, int bx0, int bx1) {
    if (bx1 <= bx0)
        return;
    if (bx0 == 0 && bx1 == 10) {
        c2p_merge320_4(src, planes + y0 * 40, (u32)rows * 10u);
        return;
    }
    for (int r = 0; r < rows; r++)
        c2p_merge320_4(src + r * 320 + bx0 * 32,
                       planes + (y0 + r) * 40 + bx0 * 4,
                       (u32)(bx1 - bx0));
}

void c2p_track_reset(c2p_track *t) {
    for (int i = 0; i < VIDEO_NBUF; i++) {
        t->buf[i] = 0;
        t->n[i] = 0;
    }
}

static int trk_area(int y0, int y1, int x0, int x1) {   /* 行 x 32 ドット列 */
    return (y1 - y0) * (((x1 + 31) >> 5) - (x0 >> 5));
}

static void trk_add1(c2p_track *t, int s, int y0, int y1, int x0, int x1) {
    /* まとめても変換量が増えない矩形とはまとめる */
    for (int i = 0; i < t->n[s]; i++) {
        s16 *m = t->r[s][i];
        int a = m[0] < y0 ? m[0] : y0, b = m[1] > y1 ? m[1] : y1;
        int c = m[2] < x0 ? m[2] : x0, d = m[3] > x1 ? m[3] : x1;
        if (trk_area(a, b, c, d) <=
            trk_area(m[0], m[1], m[2], m[3]) + trk_area(y0, y1, x0, x1)) {
            m[0] = (s16)a; m[1] = (s16)b; m[2] = (s16)c; m[3] = (s16)d;
            return;
        }
    }
    if (t->n[s] < 3) {
        s16 *m = t->r[s][t->n[s]++];
        m[0] = (s16)y0; m[1] = (s16)y1; m[2] = (s16)x0; m[3] = (s16)x1;
        return;
    }
    s16 *m = t->r[s][2];                    /* あふれたら最後の矩形に合わせる */
    if (y0 < m[0]) m[0] = (s16)y0;
    if (y1 > m[1]) m[1] = (s16)y1;
    if (x0 < m[2]) m[2] = (s16)x0;
    if (x1 > m[3]) m[3] = (s16)x1;
}

void c2p_track_add(c2p_track *t, int y0, int y1, int x0, int x1) {
    if (y0 < 0) y0 = 0;
    if (y1 > 256) y1 = 256;
    if (x0 < 0) x0 = 0;
    if (x1 > 320) x1 = 320;
    if (y1 <= y0 || x1 <= x0)
        return;
    for (int i = 0; i < VIDEO_NBUF; i++)
        trk_add1(t, i, y0, y1, x0, x1);
}

static void c2p_track_flush_rows_impl(c2p_track *t, const u8 *chunky, u8 *planes,
                                      int *ry0, int *ry1, int nplanes) {
    int y0 = 256, y1 = 0;
    int s = -1;
    for (int i = 0; i < VIDEO_NBUF && s < 0; i++)
        if (t->buf[i] == planes)
            s = i;
    if (s < 0) {
        for (int i = 0; i < VIDEO_NBUF && s < 0; i++)
            if (!t->buf[i])
                s = i;
        if (s < 0)
            s = 0;
        t->buf[s] = planes;
        if (nplanes == 4)
            c2p4_fast(chunky, planes);
        else
            c2p8_fast(chunky, planes, 320, 256);
        y0 = 0;
        y1 = 256;
    } else {
        for (int i = 0; i < t->n[s]; i++) {
            const s16 *m = t->r[s][i];
            if (nplanes == 4)
                c2p4_cols(chunky + m[0] * 320, planes, m[0], m[1] - m[0],
                          m[2] >> 5, (m[3] + 31) >> 5);
            else
                c2p8_cols(chunky + m[0] * 320, planes, m[0], m[1] - m[0],
                          m[2] >> 5, (m[3] + 31) >> 5);
            if (m[0] < y0) y0 = m[0];
            if (m[1] > y1) y1 = m[1];
        }
    }
    t->n[s] = 0;
    if (ry0) *ry0 = y0;
    if (ry1) *ry1 = y1;
}

void c2p_track_flush_rows(c2p_track *t, const u8 *chunky, u8 *planes,
                          int *ry0, int *ry1) {
    c2p_track_flush_rows_impl(t, chunky, planes, ry0, ry1, 8);
}

void c2p_track_flush_rows4(c2p_track *t, const u8 *chunky, u8 *planes,
                           int *ry0, int *ry1) {
    c2p_track_flush_rows_impl(t, chunky, planes, ry0, ry1, 4);
}

void c2p_track_flush(c2p_track *t, const u8 *chunky, u8 *planes) {
    c2p_track_flush_rows(t, chunky, planes, 0, 0);
}

/* C 版 (比較・測定用)。 */
void c2p8_rect_c(const u8 *src, u8 *planes, int width, int plane_h, int y0, int rows) {
    c2p_rows(src, planes, width, (width >> 3) * plane_h, y0, rows);
}

/* ---- 自己テスト ---- */
#define ST_W 320
#define ST_H 256
#define ST_PLANE (ST_W / 8 * ST_H)

static u8 st_src[ST_W * ST_H];
static u8 st_old[8 * ST_PLANE];
static u8 st_new[8 * ST_PLANE];

int c2p_selftest(u32 *old_frames, u32 *new_frames) {
    /* テストパターン生成 (xorshift) */
    u32 s = 0x12345678u;
    for (u32 i = 0; i < sizeof(st_src); i++) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        st_src[i] = (u8)s;
    }

    u32 f0 = vblank_frames();
    for (int i = 0; i < 3; i++)
        c2p8(st_src, st_old, ST_W, ST_H);
    u32 f1 = vblank_frames();
    for (int i = 0; i < 3; i++)
        c2p8_fast(st_src, st_new, ST_W, ST_H);
    u32 f2 = vblank_frames();

    if (old_frames)
        *old_frames = f1 - f0;
    if (new_frames)
        *new_frames = f2 - f1;

    for (u32 i = 0; i < sizeof(st_old); i++) {
        if (st_old[i] != st_new[i])
            return (int)i + 1;
    }

    /* 下位4プレーン変換は8プレーン基準実装と一致し、上位を変更しない。 */
    memset(st_new, 0xA5, sizeof(st_new));
    c2p4_fast(st_src, st_new);
    for (u32 i = 0; i < 4u * ST_PLANE; i++) {
        if (st_old[i] != st_new[i])
            return (int)(i + 1);
    }
    for (u32 i = 4u * ST_PLANE; i < sizeof(st_new); i++) {
        if (st_new[i] != 0xA5)
            return (int)(i + 1);
    }

    /* 部分列変換でも範囲外と上位プレーンを変更しない。 */
    {
        const int y0 = 17, rows = 9, bx0 = 2, bx1 = 7;
        memset(st_new, 0xA5, sizeof(st_new));
        c2p4_cols(st_src + y0 * ST_W, st_new, y0, rows, bx0, bx1);
        for (int p = 0; p < 8; p++) {
            for (int y = 0; y < ST_H; y++) {
                for (int x = 0; x < ST_W / 8; x++) {
                    u32 i = (u32)p * ST_PLANE + y * (ST_W / 8) + x;
                    int in_rect = p < 4 && y >= y0 && y < y0 + rows &&
                                  x >= bx0 * 4 && x < bx1 * 4;
                    u8 expected = in_rect ? st_old[i] : 0xA5;
                    if (st_new[i] != expected)
                        return (int)(i + 1);
                }
            }
        }
    }
    return 0;
}
