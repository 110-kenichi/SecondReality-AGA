/*
 * CREDITS (クレジット) — reference/SecondReality/CREDITS/MAIN.C の移植
 *
 * 原本 (CREDITS/MAIN.C:174, C++ CREDITS_MAIN.cpp:104-106) は
 * 320x400 の仮想フレームの上半分 (200 行) に 160x100 の画像を等倍で置き、
 * 下半分 (200 行) に 32 行フォントの文字を置く。VGA の 320x400 を
 * 4:3 で見ると、画像は 160x200 (縦 2 倍) の大きさになる。
 *
 * Amiga の 320x256 では 320x200 相当の窓 (y=28..227) に写す:
 *   画像 : 160x100 を等倍で (80,28)-(239,127)
 *   文字 : 32 行フォントを 1/2 の 16 行で (y=136 から 21 行間隔)
 * 画像は原作どおり横スライドで入退場させる。
 */
#include "scenes.h"
#include "../framework/video.h"
#include "../framework/vblank.h"
#include "../framework/c2p.h"
#include "../framework/mem.h"
#include <exec/memory.h>

#define CR_W        320
#define CR_H        256
#define CR_YOFF     28          /* 320x200 の表示位置 (他パートと同じ) */
#define CR_PIC_X    80          /* 原本: tw_putpixel(80+x, ...) */
#define CR_PIC_W    160
#define CR_PIC_H    100
#define CR_FONT_H   16          /* 32 行フォントを 1/2 (320x200 相当) */
#define CR_TEXT_Y   (CR_YOFF + 108)  /* 原本: 下半分の y=16 → 8 → 108 */
#define CR_TEXT_STEP 21         /* (FONAY+10)/2 = 42/2 */

#define FONAY       32
#define FONAW       1500

extern const u8 *g_credits_pics;
extern const u8 *g_credits_font;

static const char fonaorder[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/?!:,.\"()+-";
static int fonap[256], fonaw[256];

static u8 palette[768];
static u8 picbuf[160 * 100];
static int pic_count;
static int pic_no;
static int phase;           /* 0=in, 1=hold, 2=out */
static int cr_y;            /* 原作 screenin の y (0..200*128) */
static int cr_v;            /* 出るときの速度 (v+=15) */
static int hold_t;
static u32 credits_last_vbl;
static int cr_acc;
static int credits_done;

/* 画面転送 (c2p) を絵と文字の範囲だけにする (2026-11-15 ユーザー指示)。
 * chunky は毎フレーム 0 で消してから、絵 (160x100) と文字だけを描くので、
 * それ以外は常に 0。描く内容は (pic_no, 絵の横ずれ hx, 文字の split) だけで
 * 決まる。裏画面 2 枚それぞれについて、前回変換した矩形と今回の矩形を
 * 合わせて変換すれば、全体を変換したのと同じプレーンになる。
 * 内容 (key) が前回と同じなら描画も変換もしない (保持中の 143 VBL)。
 * 初めて使う裏画面は 256 行全体を変換する (c2p_scene の最初と同じ)。 */
struct cr_rect { s16 y0, y1, x0, x1; };    /* 行 y0..y1-1、x0..x1-1 */
static c2p_track cr_trk;
static struct cr_rect cr_drawn[2];           /* chunky に今描いてある矩形 */
static u32 cr_drawn_key;                     /* chunky に描いてある内容 */

/* 最終版のクレジット。リポジトリの原作ソース (CREDITS/MAIN.C:39-155) は
 * 最終版より古く、C++ 版 (Parts/CREDITS/CREDITS_MAIN.cpp:240,280,287,
 * 323,329,335 "W32 PORT CHANGE (updated from final)") が最終版に合わせて
 * 直した 6 箇所 (pic2/10/10b/16/17/18) はそちらに従う。 */
static const char *const texts[] = {
    "GRAPHICS - MARVEL\0MUSIC - SKAVEN\0CODE - WILDFIRE\0",
    "GRAPHICS - MARVEL\0MUSIC - SKAVEN\0CODE - PSI\0OBJECTS - WILDFIRE\0",
    "GRAPHICS - MARVEL\0MUSIC - SKAVEN\0CODE - WILDFIRE\0ANIMATION - TRUG\0",
    "\0GRAPHICS - PIXEL\0",
    "GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0CODE - PSI\0",
    "\0MUSIC - PURPLE MOTION\0CODE - TRUG\0",
    "\0MUSIC - PURPLE MOTION\0CODE - PSI\0",
    "\0MUSIC - PURPLE MOTION\0CODE - PSI\0",
    "\0GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0",
    "GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0CODE - TRUG\0RENDERING - TRUG\0",
    "SKETCH - SKAVEN\0GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0CODE - PSI\0",
    "SKETCH - SKAVEN\0GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0CODE - PSI\0",
    "\0MUSIC - PURPLE MOTION\0CODE - WILDFIRE\0",
    "\0MUSIC - PURPLE MOTION\0CODE - WILDFIRE\0",
    "\0MUSIC - PURPLE MOTION\0CODE - PSI\0",
    "GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0CODE - TRUG\0RENDERING - TRUG\0",
    "\0MUSIC - PURPLE MOTION\0CODE - PSI\0",
    "GRAPHICS - MARVEL\0MUSIC - PURPLE MOTION\0CODE - PSI\0",
    "MUSIC - SKAVEN\0CODE - PSI\0WORLD - TRUG\0",
    "GRAPHICS - PIXEL\0MUSIC - SKAVEN\0",
    "GRAPHICS - PIXEL\0MUSIC - SKAVEN\0CODE - WILDFIRE\0",
};

static void set_pic_palette(const u8 *src)
{
    /* 原本: パレットを 16 エントリずらし、0..9 をグレー階調にする */
    for (int i = 0; i < 768; i++)
        palette[i] = 0;
    for (int i = 0; i < 240 * 3 && i < 768; i++)
        palette[16 * 3 + i] = src[i];
    for (int a = 0; a < 10; a++) {
        u8 v = (u8)(a * 7 * 255 / 63);
        palette[a * 3 + 0] = v;
        palette[a * 3 + 1] = v;
        palette[a * 3 + 2] = v;
    }
    video_set_palette(palette);
}

static void load_pic(int n)
{
    const u8 *p = g_credits_pics + 2 + n * (768 + 16000);
    set_pic_palette(p);
    for (int i = 0; i < 160 * 100; i++)
        picbuf[i] = p[768 + i];
}

static void font_init(void)
{
    const u8 *f = g_credits_font;
    int x = 0;
    const char *o = fonaorder;
    while (x < FONAW && *o) {
        int b;
        while (x < FONAW) {
            int y;
            for (y = 0; y < FONAY; y++)
                if (f[y * FONAW + x])
                    break;
            if (y != FONAY)
                break;
            x++;
        }
        b = x;
        while (x < FONAW) {
            int y;
            for (y = 0; y < FONAY; y++)
                if (f[y * FONAW + x])
                    break;
            if (y == FONAY)
                break;
            x++;
        }
        fonap[(u8)*o] = b;
        fonaw[(u8)*o] = x - b;
        o++;
    }
    fonap[' '] = FONAW - 32;
    fonaw[' '] = 8;
}

static void put_px(u8 *chunky, int x, int y, u8 c)
{
    if ((unsigned)x < 320u && (unsigned)y < 256u)
        chunky[y * 320 + x] = c;
}

static void draw_text_line(u8 *chunky, int x, int y, const char *txt)
{
    const u8 *f = g_credits_font;
    while (*txt) {
        int w = fonaw[(u8)*txt];
        int sx = fonap[(u8)*txt];
        for (int i = 0; i < w; i++) {
            for (int j = 0; j < CR_FONT_H; j++) {
                /* 32 行フォントを 2 行おきに使う (縦 1/2)。 */
                u8 c = f[(j * 2) * FONAW + sx + i];
                if (c)
                    put_px(chunky, x + i, y + j, c);
            }
        }
        x += w + 2;
        txt++;
    }
}

static int text_width(const char *t)
{
    int w = 0;
    while (*t)
        w += fonaw[(u8)*t++] + 2;
    return w;
}


static void cr_draw_pic(u8 *chunky, int hx)
{
    for (int y = 0; y < CR_PIC_H; y++) {
        for (int x = 0; x < CR_PIC_W; x++) {
            u8 c = (u8)(picbuf[y * CR_PIC_W + x] + 16);
            put_px(chunky, CR_PIC_X + hx + x, CR_YOFF + y, c);
        }
    }
}

static void cr_draw_text(u8 *chunky, int split)
{
    const char *t = texts[pic_no < (int)(sizeof(texts) / sizeof(texts[0])) ? pic_no : 0];
    int y = split + 8;              /* 原作の下半分 y=16 -> 8 */
    /* 原作: y=16; while(*(text=prtc(160,y,text))) y+=FONAY+10;
     * (CREDITS/MAIN.C:171)。1 行目は空でも必ず prtc し (空行になる)、
     * 次の文字列が空 (終端) になったら止まる。以前は while(*t) で
     * 判定していたため、"\0MUSIC - ..." のように空行で始まる
     * クレジットは文字が 1 行も出なかった。 */
    for (;;) {
        int w = text_width(t);
        if (y < CR_H)
            draw_text_line(chunky, (320 - w) / 2, y, t);
        while (*t)
            t++;
        t++;
        if (!*t)
            break;
        y += CR_TEXT_STEP;
    }
}

/* ---- ハードウェアスクロール (2026-11-16 ユーザー提案) ----
 * 原作 CREDITS/MAIN.C:183-207 も、上の絵は表示開始位置と pel panning で
 * 横に動かし、下の文字は画面分割 (line compare) で上下させるだけで、
 * 描き直していない。同じ形にする:
 *   上 (表示 0..127 行): 幅 1024 ドットのチップ画面 cw_top に、絵を
 *     x = 400 (画面の 80 + 320) に置き、表示開始 x を 320 - hx にする。
 *   下 (表示 130..255 行): 256 行のチップ画面 cw_bot の 128 行目以降に、
 *     split = 128 のときの文字を置き、表示 130 行目に 258 - split 行目を
 *     出す (video_set_split)。表示 128, 129 行目は切り替えで黒になるが、
 *     絵は 28..127 行、文字は split + 8 >= 136 行からなので元から黒。
 * 絵と文字は、絵が切り替わったとき (画面の外にいるとき) にだけ作る。 */
#define CW_TOP_STRIDE 128               /* 1024 ドット */
#define CW_TOP_ROWS   130
#define CW_PIC_BYTE   40                /* 画面 x = 0 の位置 = ワールド x 320 */
static u8 *cw_top, *cw_bot;
static int cw_pic = -1, cw_on;

static int cr_hw(u8 *chunky, int hx, int split)
{
    if (!cw_top)
        cw_top = (u8 *)mem_alloc(8 * CW_TOP_STRIDE * CW_TOP_ROWS, MEMF_CHIP | MEMF_CLEAR);
    if (!cw_bot)
        cw_bot = (u8 *)mem_alloc(8 * 40 * 256, MEMF_CHIP | MEMF_CLEAR);
    if (!cw_top || !cw_bot)
        return 0;                       /* 確保できなければ従来の方法 */
    if (cw_pic != pic_no) {
        u8 *tmp = video_back_shadow();  /* ファスト RAM の作業場 (320x256 x 8) */
        for (int i = 0; i < CR_W * CR_H; i++)
            chunky[i] = 0;
        cr_draw_pic(chunky, 0);
        c2p8_fast(chunky, tmp, CR_W, CR_H);
        for (int p = 0; p < 8; p++)
            for (int y = CR_YOFF; y < CR_YOFF + CR_PIC_H; y++) {
                const u32 *sp = (const u32 *)(tmp + p * 10240 + y * 40);
                u32 *dp = (u32 *)(cw_top + p * CW_TOP_STRIDE * CW_TOP_ROWS +
                                  y * CW_TOP_STRIDE + CW_PIC_BYTE);
                for (int i = 0; i < 10; i++)
                    dp[i] = sp[i];
            }
        for (int i = 0; i < CR_W * CR_H; i++)
            chunky[i] = 0;
        cr_draw_text(chunky, 128);
        c2p8_fast(chunky, tmp, CR_W, CR_H);
        for (int p = 0; p < 8; p++)
            for (int y = 128; y < 256; y++) {
                const u32 *sp = (const u32 *)(tmp + p * 10240 + y * 40);
                u32 *dp = (u32 *)(cw_bot + p * 40 * 256 + y * 40);
                for (int i = 0; i < 10; i++)
                    dp[i] = sp[i];
            }
        cw_pic = pic_no;
    }
    if (!cw_on) {
        video_set_custom_screen_rows(cw_top, CW_TOP_STRIDE, CW_TOP_ROWS);
        cw_on = 1;
    }
    video_set_scroll(320 - hx);         /* hx = -320..320 -> 0..640 */
    video_set_split(cw_bot, 256, 258 - split);
    return 1;
}

/* パートが終わって標準画面に戻った後に main から呼ぶ (チップ RAM を返す) */
void credits_release(void)
{
    if (cw_top) {
        mem_free(cw_top, 8 * CW_TOP_STRIDE * CW_TOP_ROWS);
        cw_top = 0;
    }
    if (cw_bot) {
        mem_free(cw_bot, 8 * 40 * 256);
        cw_bot = 0;
    }
    cw_pic = -1;
    cw_on = 0;
}

void credits_init(void)
{
    const u8 *p = g_credits_pics;
    pic_count = p ? (((u16)p[0] << 8) | p[1]) : 0;
    pic_no = 0;
    phase = 0;
    cr_y = 200 * 128;           /* 画面下 (split=400) から */
    cr_v = 0;
    hold_t = 0;
    credits_done = 0;
    cr_acc = 0;
    credits_last_vbl = vblank_frames();
    c2p_track_reset(&cr_trk);
    cw_pic = -1;
    cw_on = 0;
    cr_drawn_key = 0xffffffffu;
    /* main が切り替え時に chunky 全体を 0 にしている */
    cr_drawn[0].y0 = cr_drawn[0].y1 = cr_drawn[1].y0 = cr_drawn[1].y1 = 0;
    cr_drawn[0].x0 = cr_drawn[0].x1 = cr_drawn[1].x0 = cr_drawn[1].x1 = 0;

    font_init();
    if (pic_count > 0)
        load_pic(0);
}

int credits_started(void) { return 0; }

int credits_finished(void)
{
    return credits_done;
}

int credits_render(u8 *chunky, u8 *planes, int width, int height)
{
    (void)width;
    (void)height;

    if (!g_credits_pics || !g_credits_font || pic_count <= 0)
        return 0;

    /* 原作 CREDITS/MAIN.C:161-213 の screenin。
     * 画面下 (split=400) から y=y*12/13 で減速しながら上へ (イナーシャ)、
     * 200 フレーム保持、y+=v, v+=15 で加速しながら下へ出す。
     * y は 200*128..0 で、split(200..400) は 128 + y/200 (256 行)。 */
    u32 now = vblank_frames();
    u32 el = now - credits_last_vbl;
    credits_last_vbl = now;
    cr_acc += (int)el * 7;      /* 70Hz の 1 フレーム = 1.4/VBL */
    int steps = cr_acc / 5;
    cr_acc -= steps * 5;

    if (phase == 0) {
        while (steps-- > 0 && cr_y > 0) {
            cr_y = cr_y * 12 / 13;
            if (cr_y <= 0) {
                cr_y = 0;
                break;
            }
        }
        if (cr_y == 0) {
            phase = 1;
            hold_t = 0;
        }
    } else if (phase == 1) {
        hold_t += (int)el;
        if (hold_t >= 143) {    /* 200 frames at 70 Hz -> 143 VBL */
            phase = 2;
            cr_y = 0;
            cr_v = 0;
        }
    } else {
        while (steps-- > 0 && cr_y < 200 * 128) {
            cr_y += cr_v;
            cr_v += 15;
            if (cr_y > 200 * 128)
                cr_y = 200 * 128;
        }
        if (cr_y >= 200 * 128) {
            phase = 0;
            cr_y = 200 * 128;
            cr_v = 0;
            pic_no++;
            if (pic_no >= pic_count) {
                credits_done = 1;
                pic_no = pic_count - 1;
            } else {
                load_pic(pic_no);
            }
        }
    }

    int hx = (phase == 2) ? -(cr_y / 80) : (cr_y / 80);
    int split = 128 + cr_y / 200;   /* 400-line split -> 256 lines */
    if (cr_hw(chunky, hx, split))
        return 1;
    u32 key = ((u32)pic_no << 24) ^ ((u32)(hx & 0xfff) << 12) ^ (u32)(split & 0xfff);
    struct cr_rect cur[2];

    /* 絵の範囲 */
    {
        int x0 = CR_PIC_X + hx, x1 = x0 + CR_PIC_W;
        if (x0 < 0) x0 = 0;
        if (x1 > CR_W) x1 = CR_W;
        cur[0].y0 = CR_YOFF;
        cur[0].y1 = CR_YOFF + CR_PIC_H;
        cur[0].x0 = (s16)x0;
        cur[0].x1 = (s16)(x1 > x0 ? x1 : x0);
    }
    /* 文字の範囲 (下の描画と同じ行の進め方) */
    {
        const char *t = texts[pic_no < (int)(sizeof(texts) / sizeof(texts[0])) ? pic_no : 0];
        int y = split + 8, xmin = CR_W, xmax = 0, ymax = y;
        for (;;) {
            int w = text_width(t);
            if (y < CR_H && w > 0) {
                int x = (320 - w) / 2;
                if (x < xmin) xmin = x;
                if (x + w > xmax) xmax = x + w;
                ymax = y + CR_FONT_H;
            }
            while (*t)
                t++;
            t++;
            if (!*t)
                break;
            y += CR_TEXT_STEP;
        }
        if (xmin < 0) xmin = 0;
        if (xmax > CR_W) xmax = CR_W;
        if (ymax > CR_H) ymax = CR_H;
        cur[1].y0 = (s16)(split + 8 < CR_H ? split + 8 : CR_H);
        cur[1].y1 = (s16)ymax;
        cur[1].x0 = (s16)(xmax > xmin ? xmin : 0);
        cur[1].x1 = (s16)(xmax > xmin ? xmax : 0);
    }

    if (key != cr_drawn_key) {
        cr_drawn_key = key;
        /* 背景: 絵と文字以外は常に 0 なので、前回描いた矩形だけを 0 に
         * 戻す (以前は毎フレーム 320x256 全体を消していた)。変わるのは
         * 前回の矩形と今回の矩形だけなので、その 2 つを変換対象にする
         * (重なっていれば c2p_track が 1 つにまとめる)。 */
        for (int k = 0; k < 2; k++) {
            const struct cr_rect *r = &cr_drawn[k];
            for (int y = r->y0; y < r->y1; y++)
                for (int x = r->x0; x < r->x1; x++)
                    chunky[y * 320 + x] = 0;
            c2p_track_add(&cr_trk, r->y0, r->y1, r->x0, r->x1);
            c2p_track_add(&cr_trk, cur[k].y0, cur[k].y1, cur[k].x0, cur[k].x1);
            cr_drawn[k] = cur[k];
        }

        /* 画像 (上半分)。原作は表示開始アドレスと pel panning で横に
         * スライドさせる: 入りは yy=320-y/80、出は yy=320+y/80
         * (CREDITS/MAIN.C:183-191,199-207)。C++ 版の reconstitute_split
         * (CREDITS_MAIN.cpp:24-33,104,112) では横ずれ = 320-yy で、
         * 入り +y/80 (右から)、出 -y/80 (左へ)。 */
        cr_draw_pic(chunky, hx);

        /* クレジット文字 (下半分、split と一緒に上下する) */
        cr_draw_text(chunky, split);
    }

    c2p_track_flush(&cr_trk, chunky, planes);
    return 1;
}
