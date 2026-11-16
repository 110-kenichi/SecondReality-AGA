/*
 * M3 シーン 5: ALKU (イントロテキスト) — オリジナル ALKU/MAIN.C の移植 (適応版)
 *
 * オリジナルは VGA mode-X (640x400 / 4 プレーン / CRTC スクロール / 銅線フェード) で
 * 動作するため、Amiga (320x256 / 8 プレーン) では「見た目」を再現する適応版とした:
 *   - 背景 HOI (640x200, 256 色) を縦 256 行へ拡大して全面表示し、320 段階で横パン
 *     (9 フレームごとに 1px — オリジナルの SCRLF=9 と同じ)
 *   - フォント FONA (1500x30, ハーフピクセル 3 階調) を 1:1 に統合して描画
 *     (1 グリフ = 幅/2 の論理ピクセル。'A' = 10x14 px)
 *   - 3 ページのイントロ (A / Future Crew / Production など) + 画面フェード
 *   - クレジット 4 ページ (Graphics / Music / Code / Additional Design)
 *   - テキストはパレット (253-255) でフェード
 *
 * テキストの位置:
 *   オリジナル (fmaketext/maketext) ではテキストは「画面固定」で、
 *   背景だけが横スクロールする。テキストをワールド座標に固定すると
 *   背景と一緒に流れてしまうので、テキストは常に画面座標へ描く。
 *   表示中のバッファを書き換えると文字が震えるため、ワールドを 2 枚
 *   (world[0]/world[1]) 用意し、裏側に「背景 + テキスト」を描いてから
 *   表示を切り替える (オリジナルも隠しページへ描いて切り替えている)。
 *
 * 横スクロール:
 *   BPLCON1 のファインスクロールは左端に最大 15px の「データ欠け」帯を
 *   作るため使わない。背景をチップ RAM のワールドへ 1px 単位で
 *   シフトコピーする (320x256x8 プレーン = 80KB / 9 フレーム)。
 *   これで左端のカラム欠けが起きない。
 *
 * 状態機械は VBL 経過数で駆動 (フレームレートに依存しない)。
 *
 * データ: DF2:hoi.uh (背景), DF2:fona.uh (フォント) — main.c が TakeSystem 前にロード
 */
#include "scenes.h"
#include "../framework/mem.h"
#include "../framework/video.h"
#include "../framework/vblank.h"
#include "../framework/dis.h"
#include "../music/music.h"
#include "../framework/c2p.h"

#include <proto/exec.h>
#include <exec/memory.h>

extern const u8 *g_hoi_data;
extern const u8 *g_fona_data;

#define HOI_W       640
#define HOI_H       200
#define HOI_DATA    0x310           /* 49*16 */

#define SRC_RB      (HOI_W / 8 + 4) /* 背景ソース 1 行 (640 + 余白 32px = 84B) */
#define SRC_PS      (SRC_RB * 256)  /* 背景ソース 1 プレーン */

#define SCR_W       320
#define SCR_H       256
#define SCR_RB      (SCR_W / 8)     /* 40 バイト/行 */
#define SCR_PS      (SCR_RB * SCR_H)

#define FONT_W      1500
#define FONT_H      30
#define FONT_DATA   0x310

/* 原作 ALKU/MAIN.C:7 の SCRLF=9 は 70Hz で 9 フレームごとに 1px。
 * PAL 50Hz では 9*50/70 = 6.43 VBL ごと。tick() は 1 VBL ごとに
 * 呼ばれるので、7/5=1.4 を足して 45 (=9*5) で 1px 進める。 */
#define SCRLF       9               /* 70Hz のフレーム数 (原作) */
#define SCRLF_UNIT  (SCRLF * 5)     /* 45 */
#define SCROLL_END  320             /* パン量 (背景 640 - 画面 320) */

/* 背景の表示位置 (オリジナル: 400 ライン中 50..349 に画像 150 行を 2 倍)。
 * 移植版は 150 行を 1:1 で 53..202 行に置く (ユーザー指定 2026-11-13)。 */
#define ALKU_BG_TOP 53          /* 150 行を中央に 1:1 で置く */
#define ALKU_BG_H   150

#define TEXT_LO     253
#define TEXT_MID    254
#define TEXT_HI     255

#define OVERLAY_MAX 4

static u8 *world[2];                /* 8 プレーン × 40 × 256 (チップ RAM) */
static int disp;                    /* 表示中のワールド (0/1) */
static u8 *wb;                      /* 描画先 (compose: ファスト RAM) */
static u8 *compose;                 /* 8 プレーン x 40 x 256 (ファスト RAM) */
static u8 *hoi_planar;              /* 背景 (8 × 84 × 256, Fast RAM, 256 行へ拡大済み) */
static u8 pal_base[768];
static u8 pal_text[768];
static u8 pal_work[768];
static int credit_text;
static int gpos[256];
static int gwid[256];

static int state, frame, scroll_frame, scroll_pos, page_idx;
static int pic_level, text_level, credit_text_pos;
static int fade_acc, pic_acc;
static int need_redraw;
static u32 last_vbl;

/* 画面固定オーバーレイ (テキスト) */
static const char *ov_text[OVERLAY_MAX];
static int ov_y[OVERLAY_MAX];
static int ov_logo[OVERLAY_MAX];
static int ov_lines;

/* デバッグ表示用 (main.c の draw_debug が読む) */
volatile int alku_dbg_state, alku_dbg_pic, alku_dbg_text, alku_dbg_frame, alku_dbg_scroll;

/* オリジナルの fonaorder (CP437 の Å=0x8F, Ö=0x99 を含む) */
static const char *fonaorder =
    "ABCDEFGHIJKLMNOPQRSTUVWXabcdefghijklmnopqrstuvwxyz0123456789!?,.:"
    "\x8f\x8f()+-*='\x8f\x99";

/* ---- イントロ 3 ページ (オリジナル MAIN.C の prtc 呼び出しに対応) ---- */
typedef struct {
    const char *line[3];
    int y[3];
    int logo[3];                    /* 1 = Dolby ロゴ上段 / 2 = 下段 */
} IntroPage;

static const IntroPage intro_pages[3] = {
    { { "A", "Future Crew", "Production" },       { 78, 106, 134 }, { 0, 0, 0 } },
    { { "First Presented", "at Assembly 93", 0 }, { 106, 134, 0 },  { 0, 0, 0 } },
    { { "in", 0, 0 },                             { 78, 102, 121 }, { 0, 1, 2 } },
};

/* ---- クレジット 4 ページ (オリジナル addtext/faddtext に対応) ---- */
typedef struct {
    const char *line[4];
    int y[4];
} CreditPage;

static const CreditPage credit_pages[4] = {
    { { "Graphics", "Marvel", "Pixel", 0 },           { 96, 122, 147, 0 } },
    { { "Music", "Purple Motion", "Skaven", 0 },      { 96, 122, 147, 0 } },
    { { "Code", "Psi", "Trug", "Wildfire" },          { 83, 109, 134, 159 } },
    { { "Additional Design", "Abyss", "Gore", 0 },    { 96, 122, 147, 0 } },
};

#define DOLBY_X     1014            /* Dolby ロゴ上段 (0x8f) のフォント列位置 */
#define DOLBY_X2    1231            /* Dolby ロゴ下段 (0x99) のフォント列位置 */
#define DOLBY_COLS  215
#define DOLBY_DY    19              /* 2 段の重なり (オリジナルと同じ 19px) */

/* ---- プラナー 1 ピクセル書き込み (画面座標, 8 プレーン) ---- */
static void put_px(int x, int y, u8 c) {
    if (x < 0 || x >= SCR_W || y < 0 || y >= SCR_H)
        return;
    u8 mask = (u8)(0x80 >> (x & 7));
    u8 *p = wb + (x >> 3) + y * SCR_RB;
    for (int b = 0; b < 8; b++) {
        u8 *pp = p + b * SCR_PS;
        if (c & (1 << b))
            *pp |= mask;
        else
            *pp &= (u8)~mask;
    }
}

/* 描画先ワールドの現在の色 (背景色 0..63) を読む */
static u8 get_px(int x, int y) {
    if (x < 0 || x >= SCR_W || y < 0 || y >= SCR_H)
        return 0;
    u8 mask = (u8)(0x80 >> (x & 7));
    u8 *p = wb + (x >> 3) + y * SCR_RB;
    u8 v = 0;
    for (int b = 0; b < 8; b++)
        if (p[b * SCR_PS] & mask)
            v |= (u8)(1 << b);
    return v;
}

/* ---- グリフ走査 (オリジナル init() の空列スキャンを移植) ---- */
static void scan_font(void) {
    const u8 *font = g_fona_data + FONT_DATA;
    int x = 0;
    const u8 *o = (const u8 *)fonaorder;

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

    /* スペースはフォントに含まれないので、オリジナル (MAIN.C の
     * fonap[32]=1500-20 / fonaw[32]=16) と同じ幅を割り当てる。 */
    gpos[' '] = FONT_W - 20;
    gwid[' '] = 16;
}

/* ---- テキスト描画 (フォント 1 列 = 画面 1 ピクセル、3 階調) ---- */
static u8 merge_px(int v) {
    if (v >= 3)
        return 3;
    return (u8)v;                       /* 0..3 の階調 */
}

static void draw_glyph(int x, int y, u8 ch) {
    const u8 *font = g_fona_data + FONT_DATA;
    int gx = gpos[ch], gw = gwid[ch];
    for (int j = 0; j < gw; j++) {
        for (int yy = 0; yy < FONT_H; yy++) {
            u8 lv = merge_px(font[yy * FONT_W + gx + j]);
            if (!lv)
                continue;
            if (credit_text) {
                /* 原作 MAIN.C:184-210: 背景色 0..63 ごとに 3 階調を
                 * ブレンドしたパレット 64..255 を使う */
                u8 bg = (u8)(get_px(x + j, y + yy) & 63);
                put_px(x + j, y + yy, (u8)(lv * 64 + bg));
            } else {
                put_px(x + j, y + yy,
                       (u8)(lv == 1 ? TEXT_LO : (lv == 2 ? TEXT_MID : TEXT_HI)));
            }
        }
    }
}

static int text_w(const char *t) {
    int w = 0;
    while (*t)
        w += gwid[(u8)*t++] + 2;
    return w ? w - 2 : 0;
}

static void draw_text(int x, int y, const char *t) {
    while (*t) {
        u8 ch = (u8)*t++;
        draw_glyph(x, y, ch);
        x += gwid[ch] + 2;
    }
}

static void draw_text_center(int cx, int y, const char *t) {
    draw_text(cx - text_w(t) / 2, y, t);
}

/* Dolby ロゴ (フォント列の一部を直接描画)
 * オリジナルは 0x8f (上段) と 0x99 (下段) を 19px ずらして重ねる。 */
static void draw_dolby(int cx, int y, int part) {
    const u8 *font = g_fona_data + FONT_DATA;
    int base = (part == 2) ? DOLBY_X2 : DOLBY_X;
    int x = cx - DOLBY_COLS / 2;
    for (int j = 0; j < DOLBY_COLS; j++) {
        for (int yy = 0; yy < FONT_H; yy++) {
            u8 lv = merge_px(font[yy * FONT_W + base + j]);
            if (lv)
                put_px(x + j, y + yy,
                       (u8)(lv == 1 ? TEXT_LO : (lv == 2 ? TEXT_MID : TEXT_HI)));
        }
    }
}

/* ---- パレット (pic_level: 背景 0-64 / text_level: テキスト 0-64) ---- */
static void apply_palette(void) {
    if (credit_text) {
        /* 原作 textin/textout: 画像パレット (palette) から
         * 文字入りパレット (palette2) へ text_level/64 で補間。
         * 文字パレット 64..255 は背景色 0..63 とのブレンド。 */
        for (int i = 0; i < 64 * 3; i++)
            pal_work[i] = (u8)((pal_base[i] * pic_level) >> 6);
        for (int i = 64 * 3; i < 256 * 3; i++) {
            int bg = (i / 3) & 63;
            int base = (pal_base[bg * 3 + (i % 3)] * pic_level) >> 6;
            int tgt = pal_text[i];
            pal_work[i] = (u8)(base + ((tgt - base) * text_level) / 64);
        }
    } else {
        /* イントロ (黒背景): 文字は 253..255 のフラットな 3 階調。
         * 原作 MAIN.C:192-209 の fade2 は背景パレットの色 1..3
         * (HOI.IN0: 10/30/63 の灰色)。以前は HOI.U の暗いパレット
         * (色 1..3 = 2/5/6) を使っていたため、根拠のない 92/180/252 を
         * 直接入れていた。 */
        for (int i = 0; i < TEXT_LO * 3; i++)
            pal_work[i] = (u8)((pal_base[i] * pic_level) >> 6);
        for (int i = TEXT_LO * 3; i < 256 * 3; i++)
            pal_work[i] = 0;
        for (int k = 0; k < 3; k++)
            for (int ch = 0; ch < 3; ch++)
                pal_work[(TEXT_LO + k) * 3 + ch] =
                    (u8)((pal_base[(1 + k) * 3 + ch] * text_level) >> 6);
    }
    video_set_palette(pal_work);
}

/* ---- 背景を 1px シフトしてワールドへコピー ----
 * ソース (hoi_planar) の [s, s+320) を wb の [0,320) へ。
 * 1 行 40 バイトを 32bit 単位でシフトコピーする。 */
static void blit_bg(int s) {
    int sb = s >> 3;                    /* ソースのバイト位置 */
    int sh = s & 7;                     /* ビットシフト量 */
    for (int b = 0; b < 8; b++) {
        const u8 *src = hoi_planar + b * SRC_PS;
        u8 *dst = wb + b * SCR_PS;
        for (int y = ALKU_BG_TOP; y < ALKU_BG_TOP + ALKU_BG_H; y++) {  /* 帯の外は常に 0 */
            const u8 *sp = src + y * SRC_RB + sb;
            /* チップ RAM へは 32bit で書く。以前は 1 バイトずつ書いて
             * いたため、1 回の描画で 8 プレーン x 40 x 256 = 80KB 分、
             * 8 万回のチップバスアクセスになり、実機 (68060) でも
             * スクロールが重かった (2026-11-15 ユーザー報告)。
             * A1200 のチップバスは 32bit なので 1/4 の回数で済む。 */
            u32 *dp = (u32 *)(dst + y * SCR_RB);
            u32 a = ((u32)sp[0] << 24) | ((u32)sp[1] << 16) |
                    ((u32)sp[2] << 8) | (u32)sp[3];
            for (int i = 0; i < SCR_RB; i += 4) {
                u32 c = ((u32)sp[i + 4] << 24) | ((u32)sp[i + 5] << 16) |
                        ((u32)sp[i + 6] << 8) | (u32)sp[i + 7];
                *dp++ = sh ? (a << sh) | (c >> (32 - sh)) : a;
                a = c;
            }
        }
    }
}

/* ---- オーバーレイ設定 (次の描画で新しいテキストを描く) ---- */
static void set_overlay(const char *const *lines, const int *ys, const int *logos, int n) {
    ov_lines = n;
    for (int i = 0; i < n && i < OVERLAY_MAX; i++) {
        ov_text[i] = lines[i] ? lines[i] : "";
        ov_y[i] = ys[i];
        ov_logo[i] = logos ? logos[i] : 0;
    }
    need_redraw = 1;
}

static void set_intro_overlay(int idx) {
    const IntroPage *p = &intro_pages[idx];
    const char *lines[3];
    int ys[3], logos[3];
    for (int i = 0; i < 3; i++) {
        lines[i] = p->line[i] ? p->line[i] : "";
        ys[i] = p->y[i];
        logos[i] = p->logo[i];
    }
    set_overlay(lines, ys, logos, 3);
}

static void set_credit_overlay(int idx) {
    const CreditPage *p = &credit_pages[idx];
    set_overlay(p->line, p->y, 0, 4);
}

/* ---- 裏のワールドに「背景 + テキスト」を描いて表示を切り替える ----
 * テキストは画面座標に描くので、背景がスクロールしても動かない。 */
__attribute__((noinline))
static void present(void) {
    int nb = disp ^ 1;
    /* 背景と文字はファスト RAM の compose に組み立て、最後に 32bit で
     * チップ RAM の world へ写す。文字の描画は 1 ドットごとに 8 プレーンを
     * 読み書きするので、チップ RAM に直接描くと実機で非常に遅い
     * (2026-11-15 ユーザー報告: 68060 でもイントロのスクロールが重い)。 */
    wb = compose ? compose : world[nb];

    blit_bg(scroll_pos);

    if (ov_lines > 0) {
        for (int i = 0; i < ov_lines; i++) {
            if (ov_logo[i])
                draw_dolby(160, ov_y[i], ov_logo[i]);
            else
                draw_text_center(160, ov_y[i], ov_text[i]);
        }
    }

    if (compose) {
        /* 絵があるのは背景の帯 (ALKU_BG_TOP から 150 行) だけで、文字
         * (y=78..189) もその中に収まる。帯の外は両方のバッファとも
         * 最初の 0 のままなので、帯の行だけを写す (256 行の 59%)。 */
        for (int b = 0; b < 8; b++) {
            const u32 *sp = (const u32 *)(compose + b * SCR_PS + ALKU_BG_TOP * SCR_RB);
            u32 *dp = (u32 *)(world[nb] + b * SCR_PS + ALKU_BG_TOP * SCR_RB);
            for (int i = 0; i < ALKU_BG_H * SCR_RB / 4; i++)
                dp[i] = sp[i];
        }
    }

    /* 表示を裏側へ切り替え (次のフレームから反映される) */
    video_switch_custom_screen(world[nb], SCR_RB, 0);
    disp = nb;
}

/* 原作 ALKU/MAIN.C:85-150: スクロールが右端 (a=320) まで行くと終わる。
 * 次の U2A.C:226-237 は order>10 && row>46 まで待ってから画面を
 * 切り替えるので、それまでは ALKU の最後の画面が残る。ここでは
 * その待ちも含めて ALKU を終わらせる。 */
int alku_finished(void) {
    return state == 7 && np_ord > 10 && np_row > 46;
}

/* ---- 状態機械を 1 フレーム進める ---- */
static void tick(void) {
    switch (state) {
    case 0:                                 /* イントロ: テキストのみフェードイン */
        /* オリジナル (ALKU/MAIN.C) では最初の 3 ページは黒背景にテキストだけを
         * 表示し、背景画像はその後の picin フェードで初めて現れる。
         * ここで pic_level を上げると背景まで見えてしまう。
         * 原作の dofade は 64 フレーム (70Hz = 0.91s)。50Hz では
         * 64/0.91*0.02 = 1.4 段階/VBL。 */
        fade_acc += 7;
        while (fade_acc >= 5) {
            fade_acc -= 5;
            if (text_level < 64)
                text_level++;
        }
        if (text_level >= 64) {
            text_level = 64;
            state = 1;
            frame = 0;
        }
        apply_palette();
        break;
    case 1:                                 /* イントロ: 保持 (原作 wait(300)
                                             * = 300/70 = 4.29s → 214 VBL) */
        if (++frame >= 214) {
            state = 2;
            frame = 0;
        }
        break;
    case 2:                                 /* イントロ: テキストフェードアウト */
        fade_acc += 7;
        while (fade_acc >= 5) {
            fade_acc -= 5;
            if (text_level > 0)
                text_level--;
        }
        if (text_level <= 0) {
            text_level = 0;
            apply_palette();
            page_idx++;
            if (page_idx < 3) {
                state = 20;                 /* 次の音楽同期を待つ */
            } else {
                page_idx = 0;
                ov_lines = 0;               /* イントロのテキストを消す */
                need_redraw = 1;
                state = 21;                 /* 曲の sync 4 を待つ */
            }
        } else {
            apply_palette();
        }
        break;
    case 3:                                 /* クレジット: テキストフェードイン
                                             * 原作 dofade 64 フレーム → 46 VBL */
        fade_acc += 7;
        while (fade_acc >= 5) {
            fade_acc -= 5;
            if (text_level < 64)
                text_level++;
        }
        if (text_level >= 64) {
            text_level = 64;
            state = 4;
            frame = 0;
        }
        apply_palette();
        break;
    case 4:                                 /* クレジット: 保持
                                             * 原作 f=20..50 の 30 スクロール
                                             * ステップ = 30*128.6ms = 3.86s
                                             * → 193 VBL */
        if (++frame >= 193) {
            state = 5;
            frame = 0;
        }
        break;
    case 5:                                 /* クレジット: テキストフェードアウト
                                             * 原作 dofade 64 フレーム → 46 VBL */
        fade_acc += 7;
        while (fade_acc >= 5) {
            fade_acc -= 5;
            if (text_level > 0)
                text_level--;
        }
        if (text_level <= 0) {
            text_level = 0;
            apply_palette();
            page_idx++;
            if (page_idx < 4) {
                state = 22;             /* 次のクレジットを音楽位置で待つ */
            } else {
                ov_lines = 0;               /* テキストを消す */
                need_redraw = 1;
                state = 6;
            }
        } else {
            apply_palette();
        }
        break;
    case 6:                                 /* スクロールのみ (最後まで) */
        if (scroll_pos >= SCROLL_END) {
            state = 7;
            frame = 0;
        }
        break;
    case 20:                                /* 次のページを出す音楽位置を待つ
                                             * (オリジナル: while(dis_sync()<n)) */
        if (dis_sync() >= page_idx + 1) {
            set_intro_overlay(page_idx);
            text_level = 0;
            credit_text = 0;
            fade_acc = 0;
            state = 0;
            frame = 0;
        }
        break;
    case 21:                                /* 背景画像のフェードイン開始を待つ */
        if (dis_sync() >= 4) {
            pic_acc = 0;
            state = 9;
        }
        break;
    case 22:                                /* クレジットを出す音楽位置を待つ
                                             * 原作は switch(tptr++) の後に
                                             * while(dis_sync()<4+tptr) なので、
                                             * 最初のページは sync 5 になる。 */
        if (dis_sync() >= 5 + page_idx) {
            set_credit_overlay(page_idx);
            text_level = 0;
            credit_text = 1;
            apply_palette();        /* フェードイン前は背景と同色にする */
            fade_acc = 0;
            /* 原作 fmaketext(aa+16) はテキストを 16px 先のワールドに書き、
             * ALKU/MAIN.C:468 の while(a<=scrl) で a が aa+17 になるまで
             * 待つ。さらにページ設定後の do_scroll(1) が 1 回入るので、
             * フェードインまで 18 ステップ。 */
            credit_text_pos = scroll_pos + 18;
            if (credit_text_pos > SCROLL_END)
                credit_text_pos = SCROLL_END;
            state = 23;
            frame = 0;
        }
        break;
    case 23:                                /* fmaketext + do_scroll(1) の
                                             * スクロール待ち (原作
                                             * ALKU/MAIN.C:138-145,468) */
        if (scroll_pos >= credit_text_pos) {
            state = 3;
            frame = 0;
        }
        break;
    case 9:                                 /* 画像フェードイン (クレジット前)
                                             * 原作 cop_dofade=128 = 1.83s
                                             * → 50Hz で 0.7 段階/VBL */
        pic_acc += 7;
        while (pic_acc >= 10) {
            pic_acc -= 10;
            if (pic_level < 64)
                pic_level++;
        }
        if (pic_level >= 64) {
            pic_level = 64;
            page_idx = 0;
            state = 22;
        }
        apply_palette();
        break;
    case 7:                                 /* スクロール終了後: U2A まで画像を保持
                                             * (オリジナルも最後まで景色が残る) */
        break;
    default:                                /* 黒保持 */
        break;
    }

    /* スクロール (画像フェードイン以降: 9 フレームごとに 1px) */
    if ((state == 9 || state == 22 || state == 23 ||
         (state >= 3 && state <= 6)) && scroll_pos < SCROLL_END) {
        scroll_frame += 7;              /* 70/50 = 1.4 VBL/フレーム */
        if (scroll_frame >= SCRLF_UNIT) {
            scroll_frame -= SCRLF_UNIT;
            scroll_pos++;
            need_redraw = 1;
        }
    }
}

void alku_init(void) {
    state = 20;                         /* 最初のページは sync 1 を待つ */
    frame = 0;
    scroll_frame = 0;
    scroll_pos = 0;
    page_idx = 0;
    pic_level = 0;
    text_level = 0;
    fade_acc = 0;
    pic_acc = 0;
    ov_lines = 0;
    need_redraw = 1;
    disp = 0;
    last_vbl = vblank_frames();

    if (!world[0]) {
        world[0] = (u8 *)mem_alloc(8 * SCR_PS, MEMF_CHIP | MEMF_CLEAR);
        world[1] = (u8 *)mem_alloc(8 * SCR_PS, MEMF_CHIP | MEMF_CLEAR);
        if (!world[0] || !world[1])
            return;
    }
    if (!hoi_planar)
        hoi_planar = (u8 *)mem_alloc(8 * SRC_PS, MEMF_FAST | MEMF_CLEAR);
    if (!compose)
        compose = (u8 *)mem_alloc(8 * SCR_PS, MEMF_FAST | MEMF_CLEAR);

    if (!g_fona_data || !g_hoi_data || !hoi_planar)
        return;

    scan_font();

    /* 背景 (チャンキー 640x200) をプラナー化し、hoi_planar へ。
     * オリジナル (ALKU/ASMYT.ASM outline) は画像の 150 行を 2 倍して
     * 400 ライン画面の 50..349 行に置く (上下に黒帯)。320x256 では
     * 同じ比率で 32..223 行に置き、その外側を黒にする。
     * これで背景だけが上に寄るのを防ぐ。 */
    {
        u8 *tmp = (u8 *)mem_alloc(8 * (80 * HOI_H), MEMF_FAST);
        if (tmp) {
            c2p8_fast(g_hoi_data + HOI_DATA, tmp, HOI_W, HOI_H);
            for (int b = 0; b < 8; b++) {
                const u8 *src = tmp + b * (80 * HOI_H);
                u8 *dst = hoi_planar + b * SRC_PS;
                for (int y = 0; y < SCR_H; y++) {
                    u8 *d = dst + y * SRC_RB;
                    if (y < ALKU_BG_TOP || y >= ALKU_BG_TOP + ALKU_BG_H) {
                        for (int i = 0; i < SRC_RB; i++)
                            d[i] = 0;
                        continue;
                    }
                    int sy = (y - ALKU_BG_TOP) * 150 / ALKU_BG_H;
                    if (sy < 0) sy = 0;
                    if (sy > 149) sy = 149;
                    const u8 *s = src + sy * 80;
                    for (int i = 0; i < 80; i++)
                        d[i] = s[i];
                    for (int i = 80; i < SRC_RB; i++)
                        d[i] = 0;
                }
            }
            mem_free(tmp, 8 * (80 * HOI_H));
        }
    }

    /* パレット: HOI (6bit) → 8bit。
     * 原作 ALKU が実際にリンクしているのは INCLUDE.ASM:8-12 の
     * HOI.IN0/HOI.IN1 (hzpic) で、色 1..3 は 10/30/63 (星と文字の灰色)。
     * 参照フォルダの HOI.U は色 1..3 だけが 2/5/6 の暗い値で、以前は
     * こちらを使っていたため ALKU の星が U2A (U2ABG.UH: 同じ 10/30/63)
     * より暗かった (2026-11-14 ユーザー指摘)。assets/images/hoi.uh は
     * HOI.IN0+IN1 の db 値から作り直した (SecondReality++
     * ALKU_MAIN_Data.cpp の hzpic も同じ値)。 */
    for (int i = 0; i < 256 * 3; i++)
        pal_base[i] = (u8)(g_hoi_data[16 + i] << 2);
    /* 原作 MAIN.C:184-210 の palette2 (6bit):
     *   palette2[y] = (f*63 + palette[y%192] * (63-f)) >> 6
     *   f = fade2[y] = palette[lv*3 + ch] (lv = 1..3 は y/192)
     * 背景色 0..63 ごとに文字 3 階調をブレンドした 64..255 を作る。 */
    for (int c = 0; c < 64; c++)
        for (int ch = 0; ch < 3; ch++)
            pal_text[c * 3 + ch] = pal_base[c * 3 + ch];
    for (int lv = 1; lv <= 3; lv++) {
        for (int c = 0; c < 64; c++) {
            for (int ch = 0; ch < 3; ch++) {
                int f = g_hoi_data[16 + lv * 3 + ch];
                int bg = g_hoi_data[16 + c * 3 + ch];
                int v6 = (f * 63 + bg * (63 - f)) >> 6;
                pal_text[(lv * 64 + c) * 3 + ch] = (u8)(v6 << 2);
            }
        }
    }
    credit_text = 0;

    set_intro_overlay(0);
    text_level = 0;
    apply_palette();
    /* ワールドを表示画面に設定 (スクロールはソフトウェアで行う) */
    video_switch_custom_screen(world[0], SCR_RB, 0);
}

int alku_render(u8 *chunky, u8 *planes, int width, int height) {
    (void)chunky;
    (void)planes;
    (void)width;
    (void)height;

    /* VBL 経過数だけ状態機械を進める (フレームレート非依存) */
    {
        u32 now = vblank_frames();
        u32 elapsed = now - last_vbl;
        if (elapsed > 10)
            elapsed = 10;           /* 極端な遅延時はまとめて進める */
        last_vbl = now;
        while (elapsed--)
            tick();
    }

    /* 裏のワールドに描いてから表示を切り替える */
    if (need_redraw) {
        need_redraw = 0;
        present();
    }

    alku_dbg_state = state;
    alku_dbg_pic = pic_level;
    alku_dbg_text = text_level;
    alku_dbg_frame = frame;
    alku_dbg_scroll = scroll_pos;
    return 1;                       /* プレーン直接 (c2p 不要) */
}

/* パートが終わったあと、チップ RAM の画面バッファを返す (main.c から、
 * 表示が標準画面に戻った後に呼ぶ)。Workbench から起動するとチップ RAM に
 * 余裕が少なく、後半の ENDSCRL の画面が確保できずに始まらなかった
 * (2026-11-15 ユーザー報告)。次にこのパートを使うときは確保し直す。 */
void alku_release(void) {
    for (int i = 0; i < 2; i++)
        if (world[i]) {
            mem_free(world[i], 8 * SCR_PS);
            world[i] = 0;
        }
    /* ファスト RAM の作業バッファも返す (2026-11-16: -Start 6 から通すと
     * 3DSINFLD の確保 (SINUS-nomem) が失敗して黒画面になった。終わった
     * パートのファスト RAM が残ったままだった)。次に使うときは確保し直す。 */
    if (hoi_planar) {
        mem_free(hoi_planar, 8 * SRC_PS);
        hoi_planar = 0;
    }
    if (compose) {
        mem_free(compose, 8 * SCR_PS);
        compose = 0;
    }
}
