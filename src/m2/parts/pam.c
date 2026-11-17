/*
 * M4 シーン: PAM (PRAX4.FLI のアニメーション再生) — 実パート第 9 号
 *
 * オリジナル: reference/SecondReality/PAM/ (VFLI.C / ANIM.C / OUTTAA.C)
 *
 * 元は 320x200 Mode 13h の FLIC アニメーションを差分パックしたものを
 * 1 フレーム 4 VBL (70Hz) で再生し、パレットを wfade テーブルで
 * フェードアウト/インする。
 *
 * Amiga 版:
 *  - 320x256 のカスタム画面に 320x200 を中央配置 (y=28)。
 *  - 差分パック (pam.ani) を 64000 バイトの作業バッファへ展開し、
 *    chunky バッファへコピーして c2p する。
 *  - パレットは 6bit -> 8bit 済みの pam.pal を wfade で減光する。
 *  - 50Hz に合わせて 1 フレーム 3 VBL (60ms) で進める。
 */
#include "scenes.h"
#include "../framework/c2p.h"
#include "../framework/vblank.h"
#include "../framework/video.h"

#define PAM_W       320
#define PAM_H       200
#define PAM_YOFF    28
#define PAM_SIZE    (PAM_W * PAM_H)
#define PAM_STEPS   45              /* OUTTAA.C の wfade と同じ長さ */

extern const u8 *g_pam_ani;
extern u32 g_pam_ani_size;
extern const u8 *g_pam_pal;

/* 原作は 2 ページを交互に使う (OUTTAA.C:51-60: ulosta_frame を 0xa000 と
 * 0xa400 に交互に書く)。OUT.ANI の差分も 2 フレーム前との差
 * (ANIM.C:28-32,40: mpage1 を画面に写してから次を読むので、比較相手の
 * 画面は 2 フレーム前の絵)。以前は 1 枚のバッファに続けて当てていたため、
 * 1 フレーム前と違う画素が残り、爆発の一部が欠けて見えた
 * (2026-11-15 ユーザー報告)。フレーム k は ani_page[k & 1] に当てる。 */
static u8 ani_page[2][PAM_SIZE] __attribute__((aligned(4)));
static u8 cur_pal[768];
static u32 ani_off;
static int step, tick, pam_acc, pam_done;
static u32 pam_last_vbl;
static int pam_dirty;           /* 画面バッファすべてに描くまで残す */

/* OUTTAA.C の wfade テーブル (0..63)。63 = 通常輝度。 */
static const u8 wfade[PAM_STEPS] = {
    63, 32, 16, 8, 4, 2, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 4, 6, 9, 14, 20, 28, 37, 46,
    56, 63, 63, 63, 63
};

/* OUTTAA.C の wfade パレット。
 * オリジナルは pal[a*768+b] = (63*a + (64-a)*pal[b]) / 64 で、
 * レベル 63 が「白」、0 が元の画像のパレット。つまり白フラッシュから
 * 画像へ、そして最後にまた白へ溶ける。 */
static void set_fade_pal(int level) {
    for (int i = 0; i < 768; i++)
        cur_pal[i] = (u8)(((u32)252 * (u32)level +
                           (u32)(64 - level) * (u32)g_pam_pal[i]) / 64u);
    video_set_palette(cur_pal);
}

/* 次の差分フレームを ani_buf に適用する。 */
static void pam_decode(u8 *ani_buf) {
    int pos = 0;

    while ((ani_off & 15u) && ani_off < g_pam_ani_size)
        ani_off++;

    while (ani_off < g_pam_ani_size) {
        u8 c = g_pam_ani[ani_off++];
        if (c == 0)
            break;
        if (c < 128) {
            u8 col = g_pam_ani[ani_off++];
            int n = c;
            while (n-- > 0 && pos < PAM_SIZE)
                ani_buf[pos++] = col;
        } else {
            pos += 256 - c;
            if (pos > PAM_SIZE)
                pos = PAM_SIZE;
        }
    }
}

static void pam_restart(void) {
    for (int i = 0; i < PAM_SIZE; i++)
        ani_page[0][i] = ani_page[1][i] = 0;
    ani_off = 0;
    pam_decode(ani_page[0]);        /* フレーム 0 (step 0) */
}

void pam_init(void) {
    step = 0;
    tick = 0;
    pam_acc = 0;
    pam_last_vbl = 0;
    pam_dirty = VIDEO_NBUF;   /* 3 枚の画面バッファすべてに描く (トリプルバッファ) */
    pam_done = 0;
    ani_off = 0;

    if (!g_pam_ani || !g_pam_pal)
        return;

    pam_restart();
    set_fade_pal(wfade[0]);
}

int pam_finished(void) {
    return pam_done;
}

int pam_render(u8 *chunky, u8 *planes, int width, int height) {
    (void)chunky;
    (void)width;
    (void)height;

    if (!g_pam_ani || !g_pam_pal)
        return 0;

    /* 原作 OUTTAA.C:45 は 4 VGA フレーム (70Hz) ごとに 1 フレーム。
     * PAL 50Hz では 4*50/70 = 2.857 VBL。7/20 の固定小数点で進める。
     * 経過時間は vblank_frames() の差分で取る (AGENTS.md)。以前は描画
     * 1 回ごとに 7 を足していたため、描画が 50fps を下回る実機では
     * 爆発が遅くなっていた (2026-11-15 ユーザー報告、68060)。 */
    {
        u32 now = vblank_frames();
        u32 el = pam_last_vbl ? now - pam_last_vbl : 1;
        if (el > 8)
            el = 8;
        pam_last_vbl = now;
        pam_acc += (int)el * 7;
    }
    while (pam_acc >= 20) {
        pam_acc -= 20;
        if (step < PAM_STEPS - 1) {
            step++;
            pam_decode(ani_page[step & 1]);
            set_fade_pal(wfade[step]);
            pam_dirty = VIDEO_NBUF;   /* 3 枚の画面バッファすべてに描く (トリプルバッファ) */
        } else {
            pam_done = 1;
        }
    }

    /* 320x200 を y=28 に置く。上下の黒帯は切り替え時に消してあるので、
     * 絵が変わったときだけ 200 行分を c2p する (2 枚のバッファの両方)。 */
    if (pam_dirty > 0) {
        c2p8_rect(ani_page[step & 1], planes, PAM_W, SCREEN_H, PAM_YOFF, PAM_H);
        pam_dirty--;
    }
    return 1;                        /* プレーンへ直接描いた */
}
