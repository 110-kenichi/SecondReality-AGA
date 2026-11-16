/*
 * M4 シーン: SCROLL (エンドスクロール) — オリジナル ENDSCRL/MAIN.C の移植
 *
 * オリジナルは 640x400 の 4 プレーンで、1 VBL に 1 走査線ずつ
 * テキストのフォント行をリングバッファへ書き、表示開始行を進める
 * (CRTC の start address)。70Hz なので 1 VBL = 1 行。
 *
 * Amiga 版:
 *  - 320x544 の 8 プレーンバッファ (チップ RAM) をリングとして使う
 *  - 表示は BPLxPT の行オフセット (video_set_scroll_y) だけで動かす
 *  - 原作は 2 VBL に 1 走査線 (MAIN.C:47) = 35 行/秒。50Hz で 7/10 行/VBL
 *  - 原作 400 走査線を 256 行へ 16/25 倍で写す (フォントもスクロールも)
 *  - フォントは最終版 25x1550 (SecondReality++ 由来) を横 1/2 で描く
 *  - テキストは原本 MAIN/DATA/ENDSCROL.TXT を実行時に読む
 *  - [NN 行は高さ NN 走査線の空行 (C++ ENDSCRL_MAIN.cpp:62-87)
 *  - % 行でテキストの先頭へ戻る (MAIN.C:94-97)
 */
#include "scenes.h"
#include "../framework/mem.h"
#include "../framework/video.h"
#include "../framework/vblank.h"

#include <proto/exec.h>
#include <exec/memory.h>

/* 原作と同じ横 640 ドット: Amiga hires 640x256 の 4 プレーン (16 色)。
 * 原作も 4 プレーン (ENDSCRL/MAIN.C:21 mode 0Eh) で色は 0..3 だけ使う。
 * 以前は lowres 320 で横を 1/2 に間引いていた (2026-11-14 ユーザー指示で
 * 非インターレース hires に変更)。 */
#define SCR_W       640
#define SCR_RB      80              /* 640 / 8 */
#define SCR_PLANES  4
#define SCR_ROWS    608             /* 256 行の窓 + 32 行 + 最大 30 行の行 + 余白 */
#define PLANE_PS    (SCR_RB * SCR_ROWS)

/* ENDSCRL のフォント: 25 行 x 1550 (値 0..3)。
 * 参照ソース ENDSCRL/MAIN.C の FONA.UH (30x1500) は旧版で、以前はさらに
 * ALKU のフォント (DH0:fona.uh) を流用していた。最終版は SecondReality++
 * Parts/ENDSCRL/ENDSCRL_MAIN.cpp:7 の FONAY = 25 と
 * ENDSCRL_MAIN_Data.cpp の endscrl_font[25][1550]。
 * データは DH0:endscrl_font.bin (assets/endscrl/endscrl_font.bin)。 */
#define FONT_W      1550
#define FONT_H      25

/* 縦の縮尺: 原作は 640x400 (ENDSCRL/MAIN.C:21-25: mode 0Eh + CRTC 9=0)
 * で 400 走査線が画面の高さ。こちらは 256 行なので 256/400 = 16/25 倍。
 * スクロールもフォントも原作の走査線 (ソース行) で数え、表示行へは
 * 16/25 を掛けて写す。 */
#define VSCALE_NUM  16
#define VSCALE_DEN  25

#define ENTER_Y     256             /* 窓 (256 行) のすぐ下のリング行 */
#define LINE_MAX    120

extern const u8 *g_endscrl_font;
extern const u8 *g_endscrol_data;

static u8 *buf;                     /* 8 プレーン × 40 × 544 (チップ RAM) */
static int gpos[256], gwid[256];
static int y_off;
static u8 palette[256 * 3];

/* テキスト読み出し状態 */
static int text_pos;
static char cur_line[LINE_MAX + 1];
static int cur_chars;
static int cur_height = FONT_H;
static int src_pos;                 /* スクロールしたソース行の累計 */
static int next_line_src;           /* 次の行を取り込むソース行 */
static int wrap_sub;                /* リング巻き戻しで引いた表示行 */

static u32 scroll_last_vbl;
static u32 scroll_acc;              /* 7/5 行/VBL */

/* ---- FONA フォントの走査 (ENDSCRL/MAIN.C:13,103-124 と同じ順) ----
 * MAIN.C:13 の fonaorder は ':' と '(' の間に 0x8f が 2 文字ある:
 *   "...0123456789!?,.:\x8f\x8f()+-*='"
 * フォントシートにもその 2 グリフが存在する。これを飛ばすと '(' 以降が
 * 2 グリフずれ、「(C)」が「'C'」になる (2026-11-14 修正)。 */
static void scan_font(void) {
    const u8 *font = g_endscrl_font;
    const char *o = "ABCDEFGHIJKLMNOPQRSTUVWXabcdefghijklmnopqrstuvwxyz0123456789!?,.:\x8f\x8f()+-*='Z\x84\x94Y/&";  /* SecondReality++ ENDSCRL_MAIN.cpp:11-15 */
    int x = 0;
    while (x < FONT_W && *o) {
        int y;
        while (x < FONT_W) {
            for (y = 0; y < FONT_H; y++)
                if (font[y * FONT_W + x])
                    break;
            if (y != FONT_H)
                break;
            x++;
        }
        int b = x;
        while (x < FONT_W) {
            for (y = 0; y < FONT_H; y++)
                if (font[y * FONT_W + x])
                    break;
            if (y == FONT_H)
                break;
            x++;
        }
        gpos[(u8)*o] = b;
        gwid[(u8)*o] = x - b;
        o++;
    }
    /* 原文 MAIN.C:123-124 (C++ 版 ENDSCRL_MAIN.cpp:136-137 は 1550-20) */
    gpos[' '] = FONT_W - 20;
    gwid[' '] = 16;
}

/* 新しい 1 行はファスト RAM の stage (4 プレーン x STAGE_ROWS 行) に描き、
 * 32bit 単位でチップ RAM のリングへ写す。チップ RAM に 1 ドットずつ
 * 読み書きすると、行が入ってくるフレームだけ重くなり、スクロールが
 * 引っかかっていた (2026-11-15 ユーザー報告、68060)。
 * put_px の y は「stage_y0 からの相対行」。 */
#define STAGE_ROWS 48
static u8 stage[SCR_PLANES * SCR_RB * STAGE_ROWS];
#define STAGE_PS (SCR_RB * STAGE_ROWS)
static int stage_y0;

static inline void put_px(int x, int y, u8 c) {
    y -= stage_y0;
    if ((unsigned)x >= SCR_W || (unsigned)y >= STAGE_ROWS)
        return;
    u8 *p = stage + y * SCR_RB + (x >> 3);
    u8 bit = (u8)(0x80u >> (x & 7));
    u8 nbit = (u8)~bit;
    if (c & 0x01) p[0 * STAGE_PS] |= bit; else p[0 * STAGE_PS] &= nbit;
    if (c & 0x02) p[1 * STAGE_PS] |= bit; else p[1 * STAGE_PS] &= nbit;
    if (c & 0x04) p[2 * STAGE_PS] |= bit; else p[2 * STAGE_PS] &= nbit;
    if (c & 0x08) p[3 * STAGE_PS] |= bit; else p[3 * STAGE_PS] &= nbit;
}

/* stage の rows 行を、リングの行 y へ写す。y >= 288 の行は y-288 にも
 * 写しておく (巻き戻しで y_off を 288 減らしたとき、同じ絵がそこにある
 * ように)。これで巻き戻し時の 100KB コピーが要らなくなる。 */
static void stage_commit(int y, int rows) {
    for (int k = 0; k < rows; k++) {
        int ys[2] = { y + k, y + k - 288 };
        for (int n = 0; n < 2; n++) {
            int yy = ys[n];
            if ((unsigned)yy >= SCR_ROWS)
                continue;
            for (int p = 0; p < SCR_PLANES; p++) {
                const u32 *sp = (const u32 *)(stage + p * STAGE_PS + k * SCR_RB);
                u32 *dp = (u32 *)(buf + p * PLANE_PS + yy * SCR_RB);
                for (int i = 0; i < SCR_RB / 4; i++)
                    dp[i] = sp[i];
            }
        }
    }
}

/* 横 1/2: 隣り合う 2 列の平均をフォント値として使う (0..15)。 */
/* 縦 16/25 の縮小は面積平均で行う。表示行 r はソース行の区間
 * [r*25/16, (r+1)*25/16) を覆うので、重なるソース行 (最大 3 行) を
 * 重なり幅で重み付けして平均する (重みの和 = 25)。行を間引くと横線が
 * 消えて文字が潰れて見えた (2026-11-14 ユーザー指摘)。 */
static int line_rows;
static signed char line_srow[40];           /* 最初に重なるソース行 (行内) */
static u8 line_w[40][3];                    /* 重み (1/25 単位) */

static void draw_glyph(int x, int y, u8 ch) {
    const u8 *font = g_endscrl_font;
    int gx = gpos[ch], gw = gwid[ch];
    for (int j = 0; j < gw; j++)
        for (int k = 0; k < line_rows; k++) {
            int acc = 0;
            for (int t = 0; t < 3; t++) {
                int yy = line_srow[k] + t;
                if (!line_w[k][t] || yy < 0 || yy >= FONT_H)
                    continue;
                acc += line_w[k][t] * font[yy * FONT_W + gx + j];
            }
            int v = (acc + 12) / 25;        /* 重みの和 25 */
            if (v)
                put_px(x + j, y + k, (u8)v);
        }
}

static int text_w(const char *t) {
    int w = 0;
    while (*t)
        w += gwid[(u8)*t++] + 2;
    return w;                   /* MAIN.C:69 tstart = Σ(fonaw+2) */
}

static void draw_line_at(int y, const char *t) {
    /* MAIN.C:66-70: tstart = (639 - Σ(fonaw+2)) / 2、文字間 +2 */
    int x = (639 - text_w(t)) / 2;
    while (*t) {
        u8 ch = (u8)*t++;
        draw_glyph(x, y, ch);
        x += gwid[ch] + 2;
    }
}

/* 原本 MAIN.C:27-41: 1=20/63, 2=40/63, 3..15=60/63 のグレー */
static void make_palette(void) {
    for (int i = 0; i < 256 * 3; i++)
        palette[i] = 0;
    palette[1 * 3 + 0] = 81;  palette[1 * 3 + 1] = 81;  palette[1 * 3 + 2] = 81;
    palette[2 * 3 + 0] = 162; palette[2 * 3 + 1] = 162; palette[2 * 3 + 2] = 162;
    for (int c = 3; c < 16; c++) {
        palette[c * 3 + 0] = 243;
        palette[c * 3 + 1] = 243;
        palette[c * 3 + 2] = 243;
    }
    video_set_palette(palette);
}

/* 原本 MAIN.C:66-74: 1 行読み、[NN なら高さを変え、% なら先頭へ戻る。 */
static void fetch_line(void) {
    int n = 0;
    cur_height = FONT_H;
    const u8 *t = g_endscrol_data;
    int len = 0;
    while (t && t[len] && len < 64000)
        len++;

    if (!t || len <= 0) {
        cur_line[0] = 0;
        cur_chars = 0;
        return;
    }

    if (text_pos >= len)
        text_pos = 0;
    /* 空行も 1 行として消費する (原本は \n までを 1 行) */
    if (t[text_pos] == '\n') {
        text_pos++;
        cur_chars = 0;
        return;
    }

    while (text_pos < len && t[text_pos] != '\n' && n < LINE_MAX) {
        char c = (char)t[text_pos++];
        if (c != '\r')
            cur_line[n++] = c;
    }
    if (text_pos < len && t[text_pos] == '\n')
        text_pos++;
    cur_line[n] = 0;

    if (cur_line[0] == '%') {
        text_pos = 0;
        cur_line[0] = 0;
        n = 0;
    }
    /* C++ 版 ENDSCRL_MAIN.cpp:62-65,83-87: "[NN" は文字を描かない高さ NN
     * 走査線の空行。後続の行の高さは FONAY のまま (以前は cur_height を
     * 変えたままにしていたため、[15 以降は行間が詰まり、[12 以降は
     * 文字の下が欠けていた)。 */
    if (cur_line[0] == '[') {
        int h = (cur_line[1] - '0') * 10 + (cur_line[2] - '0');
        if (h >= 1 && h <= 40)
            cur_height = h;
        cur_chars = 0;
        return;
    }
    cur_chars = n;
}

void scroll_init(void) {
    if (!buf) {
        buf = (u8 *)mem_alloc(SCR_PLANES * PLANE_PS, MEMF_CHIP | MEMF_CLEAR)  /* hires 4 プレーンだけ (以前は 8 枚分 389KB を確保していた) */;
        if (!buf)
            return;
    }
    for (int i = 0; i < SCR_PLANES * PLANE_PS; i++)
        buf[i] = 0;

    if (g_endscrl_font)
        scan_font();

    y_off = 0;
    text_pos = 0;
    cur_height = FONT_H;
    src_pos = 0;
    next_line_src = 0;
    wrap_sub = 0;
    scroll_acc = 0;
    scroll_last_vbl = vblank_frames();
    make_palette();
    video_set_custom_hires(buf, SCR_RB, SCR_ROWS);
}

int scroll_render(u8 *chunky, u8 *planes, int width, int height) {
    (void)chunky;
    (void)planes;
    (void)width;
    (void)height;

    if (!buf)
        return 1;

    /* 原本 ENDSCRL/MAIN.C:47 は
     *   if(dis_waitb()==1) dis_waitb();
     * で、2 VBL に 1 行 = 35 行/秒でスクロールする (70Hz)。
     * 50Hz でも 35 行/秒になるよう 7/10 行/VBL で進める。 */
    u32 now = vblank_frames();
    u32 el = now - scroll_last_vbl;
    if (el > 6)
        el = 6;
    scroll_last_vbl = now;
    scroll_acc += el * 7;

    while (scroll_acc >= 10) {
        scroll_acc -= 10;

        if (src_pos >= next_line_src) {
            int x, k;
            fetch_line();
            if (!g_endscrl_font)
                cur_chars = 0;
            /* ソース行 s0..s1-1 を表示行 s0*16/25 .. s1*16/25-1 へ写す */
            int s0 = next_line_src;
            int s1 = next_line_src + cur_height;
            int r0 = s0 * VSCALE_NUM / VSCALE_DEN;
            int r1 = s1 * VSCALE_NUM / VSCALE_DEN;
            line_rows = r1 - r0;
            if (line_rows > (int)sizeof(line_srow))
                line_rows = (int)sizeof(line_srow);
            for (k = 0; k < line_rows; k++) {
                /* 1/16 ソース行単位で、表示行は [a, a+25)、
                 * ソース行 sr は [sr*16, sr*16+16) */
                int a = (r0 + k) * VSCALE_DEN;
                int sr = a / VSCALE_NUM;
                line_srow[k] = (signed char)(sr - s0);
                for (int t = 0; t < 3; t++) {
                    int lo = (sr + t) * VSCALE_NUM, hi = lo + VSCALE_NUM;
                    if (lo < a) lo = a;
                    if (hi > a + VSCALE_DEN) hi = a + VSCALE_DEN;
                    line_w[k][t] = (u8)(hi > lo ? hi - lo : 0);
                }
            }
            next_line_src = s1;
            /* 新しい行は窓のすぐ下に置き、スクロールで入ってくる */
            int y = r0 - wrap_sub + ENTER_Y;
            for (int i = 0; i < (int)sizeof(stage) / 4; i++)
                ((u32 *)stage)[i] = 0;
            stage_y0 = y;
            if (cur_chars > 0)
                draw_line_at(y, cur_line);
            stage_commit(y, line_rows);
            (void)x;
        }

        src_pos++;
        y_off = src_pos * VSCALE_NUM / VSCALE_DEN - wrap_sub;
        if (y_off >= 288) {
            /* 表示窓は y_off..y_off+255。y_off=288 で窓が 288..543 に
             * なるので、288..607 (320 行) を 0..319 へコピーして巻き戻す。
             * 旧実装は 256 行で巻き戻していたため、y_off=256 付近で
             * 描いた行の下半分 (512..543) がコピーされず、文字の下数
             * ドットが欠けていた。 */
            /* 288 行目以降に描いた行は stage_commit が 288 行上にも
             * 書いてあるので、ここではコピーせずに位置だけ戻す。 */
            wrap_sub += 288;
            y_off -= 288;
        }
    }

    video_set_scroll_y(y_off);
    return 1;                   /* 表示はハードウェア縦スクロール */
}
