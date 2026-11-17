/*
 * M2: STMIK 互換 S3M プレイヤー テスト
 *
 * - Paula 4ch (8 ボイスを 2:1 ミックス) で MUSIC0 を再生
 * - 画面に np_* の状態をビットブロックで表示 (デバッグ用)
 */
#include "framework/framework.h"
#include "framework/video.h"
#include "framework/c2p.h"
#include "framework/vblank.h"
#include "framework/file.h"
#include "framework/mem.h"
#include "framework/mul64.h"
#include "framework/input.h"
#include "framework/dis.h"
#include "music/music.h"
#include "parts/scenes.h"

#include <proto/exec.h>
#include <exec/execbase.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <dos/dosextens.h>
#include <workbench/startup.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct GfxBase *GfxBase;
volatile struct Custom *custom;

extern const u8 music0_data[];

/* デバッグ: PROGDIR:music1_pcm.raw (8bit signed / 15625Hz / モノラル) を
 * プレイヤーを通さず Paula に流す (音声出力経路の切り分け用)。 */
const u8 *g_pcm_data;

/* MUSIC1 (後半パート用, PROGDIR:music1.srm からロード)。
 * オリジナルは BEGLOGO (タイトル) の後、GLENZ から MUSIC1 に切り替える。 */
const u8 *g_music1_data;

/* タイトル画像 (DF1:title.raw からロード、無ければ 0) */
const u8 *g_image_data;

/* エンドピクチャ (DF1:endpic.raw からロード、無ければ 0) */
const u8 *g_endpic_data;

/* ALKU: 背景 (DF2:hoi.uh) とフォント (DF2:fona.uh) */
const u8 *g_hoi_data;
const u8 *g_fona_data;

/* WATER (RAYSCRL): 背景 / パレット / フォント / 更新リスト */
const u8 *g_credits_pics;
const u8 *g_credits_font;
const u8 *g_endscrol_data;
const u8 *g_endscrl_font;   /* 25x1550 (SecondReality++ ENDSCRL_MAIN_Data.cpp) */
const u8 *g_u2e_obj;
const u8 *g_u2e_inst;
const u8 *g_u2e_script;
const u8 *g_u2e_pal;
const u8 *g_sinus_w1;
const u8 *g_sinus_w2;
const u8 *g_water_bg;
const u8 *g_water_pal;
const u8 *g_water_font;
const u8 *g_water_wat[3];

/* FOREST (MNTSCRL): 背景 / パレット / 加算マスク / 更新リスト */
const u8 *g_lens_back;
const u8 *g_lens_pal;
const u8 *g_lens_ex0;
const u8 *g_lens_ex1;
const u8 *g_lens_ex2;
const u8 *g_lens_ex3;
const u8 *g_lens_ex4;
const u8 *g_lens_path;

/* JPLOGO: 320x400 Ice Kingdom picture, palette, sine table */
const u8 *g_jplogo_pic;
const u8 *g_jplogo_pal;
const u8 *g_jplogo_sin;
const u8 *g_forest_bg;
const u8 *g_forest_pal;
const u8 *g_forest_mask;
const u8 *g_forest_pos[3];

/* DDSTARS: TEXTS.LBM を IMG1 化したテキスト画像 */
const u8 *g_ddstars_text;

/* PAM: 差分パックされたアニメーション (PRAX4.FLI) とパレット */
const u8 *g_pam_ani;
u32 g_pam_ani_size;
const u8 *g_pam_pal;

/* PLZPART: プラズマの正弦テーブル (PSINI.INC / LSINI4.INC / LSINI16.INC) */
const u8 *g_plasma_psini;
const u8 *g_plasma_l4;
const u8 *g_plasma_l16;

/* PLZPART plasmacube */
const u8 *g_cube_tex;
const u8 *g_cube_pal;
const u8 *g_cube_spline;
const u8 *g_cube_splcoef;
const u8 *g_cube_sin;

/* TUNNELI: リング形状テーブル pcalc[138][64] (TUNNEL.DAT) */
const u8 *g_tunnel_pcalc;
const u8 *g_tunnel_sinit;

/* U2A (VISU): パレット / 背景 / オブジェクト / アニメーション / シーンリスト */
const u8 *g_glenz_bg;
const u8 *g_glenz_pal;
const u8 *g_glenz_sin;
/* TECHNO: 干渉円の 4 プレーン (circle0 = 1 plane, circle123 = 3 planes) */
const u8 *g_techno_circle0;
const u8 *g_techno_circle123;
const u8 *g_techno_sin;                /* 1024 x s16 BE */
const u8 *g_techno_troll;              /* 320x400 x 8bit */
const u8 *g_techno_troll_pal;          /* 256 x RGB (8bit) */
const u8 *g_panic_monster;             /* 320x200 x 8bit */
const u8 *g_panic_pal;                 /* 256 x RGB (VGA 6bit) */
int g_glenz_start_frame;        /* デバッグ用: GLENZ の開始フレーム */
int g_glenz_diag;               /* デバッグ用: GLENZ の診断モード */
int g_glenz_blend = 2;          /* 塗り方式 0=上書き/1=加算/2=XOR/3=50% */
const u8 *g_u2a_pal;
const u8 *g_u2a_bg;
const u8 *g_u2a_obj;
const u8 *g_u2a_anim;
const u8 *g_u2a_scene;

static struct View *acti_view;
static UWORD saved_ints, saved_dma, saved_adkcon;

static inline void wait_blit(void) {
    (void)custom->dmaconr;
    while (custom->dmaconr & (1 << 14)) { }
}

static void TakeSystem(void) {
    Forbid();
    saved_adkcon = custom->adkconr;
    saved_ints = custom->intenar;
    saved_dma = custom->dmaconr;
    acti_view = GfxBase->ActiView;
    LoadView(0);
    WaitTOF();
    WaitTOF();
    OwnBlitter();
    wait_blit();
    custom->intena = 0x7fff;
    custom->intreq = 0x7fff;
    custom->dmacon = 0x7fff;
    for (int i = 0; i < 32; i++)
        custom->color[i] = 0;
    wait_vbl_poll();
    wait_vbl_poll();
}

static void FreeSystem(void) {
    wait_vbl_poll();
    wait_blit();
    custom->intena = 0x7fff;
    custom->intreq = 0x7fff;
    custom->dmacon = 0x7fff;
    custom->cop1lc = (ULONG)GfxBase->copinit;
    custom->cop2lc = (ULONG)GfxBase->LOFlist;
    custom->copjmp1 = 0x7fff;
    custom->intena = saved_ints | 0x8000;
    custom->dmacon = saved_dma | 0x8000;
    custom->adkcon = saved_adkcon | 0x8000;
    wait_blit();
    DisownBlitter();
    LoadView(acti_view);
    WaitTOF();
    WaitTOF();
    Permit();
}

/* ---- デバッグ表示用パレット ---- */
static void setup_palette(void) {    static u8 pal[256 * 3];
    /* 0: 黒, 1: 白, 2: 緑, 3: 暗灰, 4: 青, 5: 紫, 6: 黄, 7: 水色,
       8: 赤, 9: 橙, 10: 明緑 */
    static const u8 colors[11][3] = {
        {0, 0, 0}, {255, 255, 255}, {0, 220, 0}, {48, 48, 48}, {64, 64, 255},
        {200, 0, 200}, {220, 220, 0}, {0, 200, 220}, {220, 0, 0}, {255, 140, 0},
        {120, 255, 120},
    };
    for (int i = 0; i < 11; i++) {
        pal[i * 3 + 0] = colors[i][0];
        pal[i * 3 + 1] = colors[i][1];
        pal[i * 3 + 2] = colors[i][2];
    }
    video_set_palette(pal);
}

/* PROGDIR:tempo.cfg の数値 (パーセント)。テンポ確認用。 */
static void load_tempo_cfg(void) {
    u32 sz = 0;
    const u8 *p = (const u8 *)file_load_fast("PROGDIR:data/tempo.cfg", 8u, &sz);
    if (!p || !sz)
        return;
    u32 v = 0, n = 0;
    while (n < sz && p[n] >= '0' && p[n] <= '9') {
        v = v * 10 + (u32)(p[n] - '0');
        n++;
    }
    if (n && v >= 50 && v <= 300)
        audio_set_tempo_pct(v);
}

/* PROGDIR:mus1.cfg があれば MUSIC1 をテスト再生する。 */
static int test_music1;
static void load_music1_cfg(void) {
    u32 sz = 0;
    const u8 *p = (const u8 *)file_load_fast("PROGDIR:data/mus1.cfg", 8u, &sz);
    test_music1 = (p && sz) ? 1 : 0;
}

/* If PROGDIR:hidden.cfg exists, also play the hidden part (DDSTARS).
 * The original U2.ASM gates it with bit 7 of whattorun, which is clear in
 * the default value 7fh, so a normal run never shows it. */
static int play_hidden;
static void load_hidden_cfg(void) {
    u32 sz = 0;
    const u8 *p = (const u8 *)file_load_fast("PROGDIR:data/hidden.cfg", 8u, &sz);
    play_hidden = (p && sz) ? 1 : 0;
}

/* PROGDIR:start.cfg にパート番号 (scenes.c の 0..19) を書くと、そのパートから
 * 始める (デバッグ用)。音楽は下の表の曲・order の先頭から鳴らす。
 * 各パートは自分の音楽待ち (musplus 等) で原作どおり同期するので、
 * 表はパート開始の少し前の位置であればよい。
 *   section 0 = MUSIC0、1 = MUSIC1、2 = MUSIC0 (フィナーレ)
 * 根拠: MUSIC1 は BEGLOGO の終了で order 0 から (U2.ASM:817-821)。
 *   U2E は MUSIC0 order 18、隠しパートは 70、ENDLOGO は 25
 *   (U2.ASM:866-907 の restartmus の bx)。それ以外は旧キュー表
 *   (scene_cues) のパート開始位置か、その少し前の order。 */
static const struct { u8 section, order; } start_pos[20] = {
    {0, 0},   /* 0 ALKU */
    {0, 11},  /* 1 U2A */
    {0, 13},  /* 2 PAM */
    {0, 13},  /* 3 BEGLOGO */
    {1, 0},   /* 4 GLENZ */
    {1, 12},  /* 5 TUNNELI */
    {1, 16},  /* 6 TECHNO */
    {1, 16},  /* 7 PANIC (TECHNO の位置から) */
    {1, 33},  /* 8 MNTSCRL */
    {1, 39},  /* 9 LNS&ZOOM */
    {1, 52},  /* 10 PLZPART */
    {1, 64},  /* 11 MINVBALL */
    {1, 74},  /* 12 RAYSCRL */
    {1, 78},  /* 13 3DSINFLD */
    {1, 82},  /* 14 JPLOGO */
    {2, 70},  /* 15 DDSTARS */
    {2, 25},  /* 16 ENDLOGO */
    {2, 43},  /* 17 ENDSCRL */
    {2, 18},  /* 18 U2E */
    {2, 25},  /* 19 CRED */
};
static int start_scene = -1;
/* ダブル / トリプルバッファの選択。既定はダブル、data/triple.cfg で
 * トリプル。GLENZ (4) と U2E (18) は元々フレームレートが安定しないので
 * 常にトリプル (2026-11-17 ユーザー指示)。 */
static int g_triple;
static void pick_buffers(int scene) {
    video_set_triple(g_triple || scene == 4 || scene == 18);
}

static void load_start_cfg(void) {
    u32 sz = 0;
    const u8 *p = (const u8 *)file_load_fast("PROGDIR:data/start.cfg", 8u, &sz);
    int n = 0, any = 0;
    for (u32 i = 0; p && i < sz; i++) {
        if (p[i] < '0' || p[i] > '9')
            break;
        n = n * 10 + (p[i] - '0');
        any = 1;
    }
    start_scene = (any && n < 20) ? n : -1;
}

/* PROGDIR:glenz.cfg の数値 (GLENZ の開始フレーム)。デバッグ用。 */
static void load_glenz_cfg(void) {
    u32 sz = 0;
    const u8 *p = (const u8 *)file_load_fast("PROGDIR:data/glenz.cfg", 32u, &sz);
    if (!p || !sz)
        return;
    u32 v = 0, n = 0;
    while (n < sz && p[n] >= '0' && p[n] <= '9') {
        v = v * 10 + (u32)(p[n] - '0');
        n++;
    }
    if (n && v <= 20000)
        g_glenz_start_frame = (int)v;
    /* 2 つ目の数値 = 塗り方式 */
    while (n < sz && (p[n] < '0' || p[n] > '9'))
        n++;
    u32 w = 0, m = 0;
    while (n < sz && p[n] >= '0' && p[n] <= '9') {
        w = w * 10 + (u32)(p[n] - '0');
        n++;
        m++;
    }
    if (m && w <= 3)
        g_glenz_blend = (int)w;
    /* 3 つ目の数値 = 診断モード (1=形だけ / 2=色 0 のスパンを緑) */
    while (n < sz && (p[n] < '0' || p[n] > '9'))
        n++;
    u32 d = 0, k = 0;
    while (n < sz && p[n] >= '0' && p[n] <= '9') {
        d = d * 10 + (u32)(p[n] - '0');
        n++;
        k++;
    }
    if (k && d <= 127)
        g_glenz_diag = (int)d;
}

/* ---- 音楽タイミング ----
 * tick の発生は audio.c 側で「サンプル単位」で行う (fill_buffer)。
 * ここでは音声バッファの補充を回すだけ。以前はこの関数で経過 VBL 数から
 * tick を進めていたが、それだと 626 サンプル (40ms) のバッファ単位でしか
 * プレイヤー状態が更新されず、MUSIC1 (行 57.7ms) が 40/80ms の交互に
 * なって「走って」聞こえていた。 ---- */
static void music_frame(void) {
    audio_frame(0);
}
/* ---- デバッグ表示 (プレーン 0 直接描画・c2p 不要) ---- */
#define DBG_ROWBYTES 40                  /* 320 / 8 */
#define DBG_PLANE_SIZE (DBG_ROWBYTES * 256)

/* c2p 自己テスト結果 (起動時に計測) */
static int c2p_test_result;              /* 0 = OK, それ以外 = 不一致位置+1 */

/* 直前フレームの控え。PANICEND (SHUTDOWN.C) は _LOADER_ 定義により
 * 画像を読まず「今の画面」を消灯するので、切替直前の絵とパレットを渡す。 */
const u8 *g_prev_frame;
const u8 *g_prev_pal;
static u32 prev_frame_buf[320 * 256 / 4];
static u8 prev_pal_buf[768];
static u32 c2p_old_frames, c2p_new_frames;

/* 現在のシーン (デバッグ表示用) */
static int cur_scene;
static int loop_restart;             /* ループ時に曲を先頭から鳴らし直す */
static int panic_gate;               /* U2.ASM:830-849 のマーカー待ち */

/* 音楽オーダー → シーン (オリジナルのパート順)。
 * オリジナル (MAIN/U2.ASM) はパートごとに STARTMUS で曲を切り替える:
 *   冒頭       MUSIC0 (skaven) を先頭から
 *   BEGLOGO 後 MUSIC1 (purple motion) を order 0 から
 *   フィナーレ MUSIC0 を order 18 から
 * シーンのキューは各曲のオーダー/行で指定する。
 *   bit24-25 が音楽セクション (0=MUSIC0 / 1=MUSIC1 / 2=MUSIC0 再開)、
 *   上位バイトがオーダー、下位バイトが行 (0-63)。
 * 行番号は TIMELINE.js (ブラウザ移植) の tracker() キューに合わせた。 */
#define CUE_SEC1 0x01000000u
#define CUE_SEC2 0x02000000u

/* 音楽位置 → シーン番号。オリジナル (U2.ASM) のパート順:
 *   ALKU → U2A → PAM → BEGLOGO → GLENZ → TUNNELI → TECHNO → PANIC →
 *   FOREST → PLZPART → DOTS → WATER → SINUS → DDSTARS → END → SCROLL
 * PANIC は TECHNO の終了で入るため、音楽キューは持たない。 */
typedef struct {
    u32 cue;
    u16 scene;
} SceneCue;

/* MUSIC1 のキューは Modland 版 (`assets/music/MUSIC1_std.s3m`) の
 * オーダー番号で書く。オリジナル `MUSIC1.S3M` は 15 個の 0xFE
 * (スキップ) を含むため、オリジナルの order N は Modland 版では
 * N - (N より前の 0xFE の数) になる。
 * 例: WATER 86->74, SINUS 93->79, JPLOGO 95->81。 */
static const SceneCue scene_cues[] = {
    { 0x00000000,            0 },   /* ALKU (MUSIC0) */
    { 0x00000B2F,            1 },   /* U2A: order 11 row 47 */
    { 0x00000D01,            2 },   /* PAM: order 13 row 1 */
    { 0x00000D17,            3 },   /* BEGLOGO: タイトル (order 13 row 23) */
    { CUE_SEC1 | 0x0000022D, 4 },   /* GLENZ: MUSIC1 order 2 row 45 */
    { CUE_SEC1 | 0x00000C31, 5 },   /* TUNNELI: MUSIC1 order 12 row 49 */
    { CUE_SEC1 | 0x00001700, 6 },   /* TECHNO: MUSIC1 order 23 (前の安定版に戻す) */
    { CUE_SEC1 | 0x00002301, 8 },   /* FOREST: MUSIC1 35:01 */
    { CUE_SEC1 | 0x00002700, 9 },   /* LNS&ZOOM: MUSIC1 39:00 */
    { CUE_SEC1 | 0x00003400, 10 },  /* PLZPART: MUSIC1 52:00 */
    { CUE_SEC1 | 0x00004100, 11 },  /* DOTS: MUSIC1 65:00 */
    { CUE_SEC1 | 0x00004A3D, 12 },  /* WATER: MUSIC1 74:61 */
    { CUE_SEC1 | 0x00004F00, 13 },  /* SINUS: MUSIC1 79:00 */
    { CUE_SEC1 | 0x00005239, 14 },  /* JPLOGO: SINUS の次のマーカー手前 (COMAN_MAIN.cpp:252 の -8..-1)。以前は 81:00 で 7 秒早かった。
             * U2E is not ported yet, so go straight to END(16); the hidden
             * part DDSTARS(15) is only played when hidden.cfg exists. */
    { CUE_SEC2 | 0x00001916, 16 },  /* END: order 25 row 22 */
    { CUE_SEC2 | 0x00002B00, 17 },  /* SCROLL: order 43 row 0 */
};
#define SCENE_CUE_COUNT ((int)(sizeof(scene_cues) / sizeof(scene_cues[0])))

/* 曲の切り替え位置。
 * MUSIC1: BEGLOGO (タイトル) が出た直後。オリジナルは BEGLOGO.EXE 終了
 *   (約 2.3 秒) で MUSIC1 order 0 から鳴らし直す。order 13 は row 35 で
 *   パターンブレイクするため 13:42 は到達せず、実際は order 14 になる。
 * MUSIC0 再開: Jellypic (MUSIC1 96:57) の位置でフィナーレへ。
 *   オリジナルは U2E (Vector Part II) の直前で MUSIC0 order 18 に戻す。 */
#define MUSIC1_SWITCH_CUE 0x00000E00u
/* MUSIC1 -> MUSIC0 order 18 は JPLOGO の終了時に明示的に行う。
 * (order 0x7F は MUSIC1 の 100 オーダーには存在しない) */
#define MUSIC2_SWITCH_CUE 0x01007F00u

static int music_section;   /* 0=MUSIC0 / 1=MUSIC1 / 2=MUSIC0 再開 */

/* 個別の *_finished() で次へ移るシーン。汎用の音楽キュー切替は使わない。
 * (U2E/CREDITS は MUSIC0 section 2 にキューが無く、g=0 で ALKU に
 *  戻ってしまうため、明示的に除外する) */
static int scene_specific_end(int s) {
    return s == 1 || s == 2 || s == 5 || s == 6 || s == 7 || s == 8 ||
           s == 10 || s == 11 || s == 14 || s == 15 || s == 16 ||
           s == 17 || s == 18 || s == 19;
}

static u16 scene_for_order(u16 ord, u16 row, int section) {
    u32 cur = ((u32)section << 24) | ((u32)ord << 8) | (row & 0xff);
    u16 sc = 0;
    for (int i = 0; i < SCENE_CUE_COUNT; i++) {
        if (cur >= scene_cues[i].cue)
            sc = scene_cues[i].scene;
        else
            break;
    }
    return sc;
}


static void fill_plane_rect(u8 *p0, int x0, int y0, int w, int h) {
    int bx0 = x0 >> 3;
    int bw = (w + 7) >> 3;
    for (int y = y0; y < y0 + h && y < 256; y++) {
        u8 *row = p0 + y * DBG_ROWBYTES;
        for (int b = bx0; b < bx0 + bw && b < DBG_ROWBYTES; b++)
            row[b] = 0xFF;
    }
}

/* 矩形を 8 プレーンすべてでクリア (バーを常に白で見えるようにする) */
static void clear_all_planes_rect(u8 *planes, int x0, int y0, int w, int h) {
    int bx0 = x0 >> 3;
    int bw = (w + 7) >> 3;
    for (int p = 0; p < 8; p++) {
        u8 *pp = planes + p * DBG_PLANE_SIZE;
        for (int y = y0; y < y0 + h && y < 256; y++) {
            u8 *row = pp + y * DBG_ROWBYTES;
            for (int b = bx0; b < bx0 + bw && b < DBG_ROWBYTES; b++)
                row[b] = 0;
        }
    }
}

static void draw_debug(u8 *planes) {
    u8 *p0 = planes;                     /* プレーン 0 (色 1 = 白) */
    int h;
    /* バー領域を 8 プレーンすべてでクリア (シーンが全画面でもバーを白で表示) */
    clear_all_planes_rect(planes, 0, 64, 64, 16);           /* NG マーカー */
    clear_all_planes_rect(planes, 16, 96 - 31, 48, 31);     /* c2p 旧 */
    clear_all_planes_rect(planes, 80, 96 - 31, 48, 31);     /* c2p 新 */
    clear_all_planes_rect(planes, 16, 160 - 31, 48, 31);    /* tempo */
    clear_all_planes_rect(planes, 80, 160 - 31, 48, 31);    /* row */
    clear_all_planes_rect(planes, 144, 160 - 31, 48, 31);   /* speed */
    clear_all_planes_rect(planes, 208, 160 - 31, 48, 31);   /* 秒 */
    clear_all_planes_rect(planes, 272, 160 - 31, 48, 31);   /* tick */
    clear_all_planes_rect(planes, 8, 0, 320, 8);            /* ボイス */
    /* c2p 自己テスト: NG なら白ブロック、下に旧/新実装の所要時間 (1/8 スケール) */
    if (c2p_test_result)
        fill_plane_rect(p0, 0, 64, 64, 16);
    h = (int)(c2p_old_frames >> 3);
    if (h > 31) h = 31;
    fill_plane_rect(p0, 16, 96 - h, 48, h);
    h = (int)(c2p_new_frames >> 3);
    if (h > 31) h = 31;
    fill_plane_rect(p0, 80, 96 - h, 48, h);
    /* デバッグ: シーン番号 (x=0, 8px/シーン) と np_ord&31 (x=144) */
    clear_all_planes_rect(planes, 0, 64, 64, 16);
    h = cur_scene * 8;
    fill_plane_rect(p0, 0, 80 - h, 48, h);
    h = np_ord & 31;
    fill_plane_rect(p0, 144, 96 - h, 48, h);
    /* デバッグ: DIS sync (パート番号 0-10 → x3) */
    clear_all_planes_rect(planes, 208, 96 - 31, 48, 31);
    h = dis_sync() * 3;
    if (h > 31) h = 31;
    fill_plane_rect(p0, 208, 96 - h, 48, h);
    /* デバッグ: オーディオピーク (x=272) */
    clear_all_planes_rect(planes, 272, 96 - 31, 48, 31);
    h = audio_peak() >> 2;
    if (h > 31) h = 31;
    fill_plane_rect(p0, 272, 96 - h, 48, h);
    /* 中段: tempo / row / speed / 秒 / tick (高さ 1-31px, y=160 から上へ) */
    h = np_tempo >> 3;                          if (h > 31) h = 31;
    fill_plane_rect(p0, 16, 160 - h, 48, h);
    h = np_row >> 1;                            if (h > 31) h = 31;
    fill_plane_rect(p0, 80, 160 - h, 48, h);
    h = np_speed << 2;                          if (h > 31) h = 31;
    fill_plane_rect(p0, 144, 160 - h, 48, h);
    h = (int)((vblank_frames() / 50) & 31);     /* 秒 (1px/秒, 32 秒周期) */
    fill_plane_rect(p0, 208, 160 - h, 48, h);
    h = player_tickno() * 5;                    if (h > 31) h = 31;
    fill_plane_rect(p0, 272, 160 - h, 48, h);
    /* 上段: 8 ボイス音量 */
    for (int i = 0; i < 8; i++) {
        int w = mix_voices[i].active ? (mix_voices[i].gain >> 1) : 0;
        fill_plane_rect(p0, 8 + i * 40, 0, w, 8);
    }
}

#ifdef TRAP_DEBUG
/* 一時デバッグ: タスクの CPU 例外で、例外番号と例外フレームの PC を
 * 絶対番地 $3F0 / $3F4 に書いてから元のトラップハンドラへ渡す。
 * 入口: (sp) = 例外番号.l, 4(sp) = SR.w, 6(sp) = PC.l */
static void *old_trap;
void trap_rec(void);
__asm__(
    ".text\n"
    "trap_rec:\n"
    "    move.l (%sp),0x3f0\n"
    "    move.l 6(%sp),0x3f4\n"
    "    move.l #0x54524150,0x3f8\n"
    "    move.l %a0,-(%sp)\n"
    "    move.l %usp,%a0\n"
    "    move.l (%a0),0x3c0\n"
    "    move.l 4(%a0),0x3c4\n"
    "    move.l 8(%a0),0x3c8\n"
    "    move.l 12(%a0),0x3cc\n"
    "    move.l 16(%a0),0x3d0\n"
    "    move.l 20(%a0),0x3d4\n"
    "    move.l 24(%a0),0x3d8\n"
    "    move.l 28(%a0),0x3dc\n"
    "    move.l %a0,0x3e0\n"
    "    move.l #techno_render,0x3e4\n"
    "    move.l #main,0x3e8\n"
    "    move.l (%sp)+,%a0\n"
    "    move.l old_trap_ptr,-(%sp)\n"
    "    rts\n"
    ".data\n"
    ".globl old_trap_ptr\n"
    "old_trap_ptr: .long 0\n"
    ".text\n");
extern void *old_trap_ptr;
static void trap_install(void) {
    struct Task *t = FindTask(0);
    *(volatile u32 *)0x3f0 = 0;
    *(volatile u32 *)0x3f4 = 0;
    *(volatile u32 *)0x3f8 = 0;
    old_trap_ptr = t->tc_TrapCode;
    t->tc_TrapCode = (APTR)trap_rec;
    (void)old_trap;
}
#endif

/* 共通の c2p。ほとんどのパートは 320x200 を y=28..227 に置き、上下の
 * 黒帯は毎フレーム 0 のまま。帯の外に 0 以外の画素が無いフレームは
 * 200 行だけを変換する (チップ RAM への書き込みが 256 行の 78%)。
 * 帯の外に絵があるフレームと、その後の 2 フレーム (2 枚のバッファの
 * 両方を消すため) と、シーンが替わった直後は 256 行すべてを変換する。
 * 実機 (68060) の速度対策 (2026-11-15)。 */
/* ---- 実機用の簡易プロファイラ (data/prof.cfg があるときだけ) ----
 * パートごとに、計算 (render)、画面転送 (c2p / 写しの転送)、1 フレームの
 * 長さ (flip まで) を走査線 (1/15625 秒 = 64us) 単位で足し込み、終了時に
 * data/prof.txt へ平均と最大を書き出す。WinUAE では 68060 の速さを
 * 測れないため、実機で測るために使う (2026-11-15)。 */
int prof_on;
/* 32x32->64 の MULS.L が使えるか (framework/mul64.h)。68060 だけ無い。 */
int g_cpu_mul64;
#define PROF_SCENES 20
static u32 prof_n[PROF_SCENES], prof_r[PROF_SCENES], prof_c[PROF_SCENES],
           prof_f[PROF_SCENES], prof_rmax[PROF_SCENES], prof_fmax[PROF_SCENES];
static u32 prof_back_frame, prof_back_vpos, prof_back_last, prof_back_raw;
static u32 prof_back_prev_frame, prof_back_prev_vpos;
static int prof_back_seen;
static u32 prof_lines(void) {
    u32 f, f2;
    u16 high1, high2, low;
    do {
        f = vblank_frames();
        high1 = *(volatile u16 *)((u8 *)custom + 0x004);
        low = *(volatile u16 *)((u8 *)custom + 0x006);
        high2 = *(volatile u16 *)((u8 *)custom + 0x004);
        f2 = vblank_frames();
        /* VPOSR V8 can change between the two 16-bit custom-bus reads. */
    } while (f != f2 || ((high1 ^ high2) & 1));
    u32 vpos = ((u32)(high2 & 1) << 8) | (low >> 8);
    u32 t = f * 313u + vpos;
    /* 走査線が 0 に戻ってから VERTB 割り込みで frame_counter が増えるまでに
     * 遅れがあると (割り込みが待たされたとき)、t が 1 VBL 分戻る。
     * 実機の prof.txt で PLZPART の render 最大が 4294967250 (= -46) に
     * なった (2026-11-15)。時間は戻らないので、前回より小さければ 1 VBL 足す。
     * 同じ走査線を再び読んだ場合、補正済みの last との差はちょうど 313。 */
    static u32 last;
    static u32 prev_frame, prev_vpos;
    if (prof_on && !prof_back_seen && t < last && last - t > 313u) {
        prof_back_prev_frame = prev_frame;
        prof_back_prev_vpos = prev_vpos;
        prof_back_frame = f;
        prof_back_vpos = vpos;
        prof_back_last = last;
        prof_back_raw = t;
        prof_back_seen = 1;
    }
    if (t < last && last - t <= 313u)
        t += 313u;
    last = t;
    prev_frame = f;
    prev_vpos = vpos;
    return t;
}
/* 区切りの記録 (パートの中の待ちを抜けた時の音楽位置)。prof.txt に出す。 */
#define PROF_MARKS 48
static const char *pm_tag[PROF_MARKS];
static u16 pm_pos[PROF_MARKS];
static int pm_n;
/* パート内の段階別の時間 (U2E 用、2026-11-16 ユーザー承認)。
 * パートは prof_now() で走査線時計を読み、prof_stage_add(i, 差) で足し、
 * 1 描画ごとに prof_stage_frame() を呼ぶ。prof.txt に平均・最大を出す。 */
#define PROF_STAGES 8
static const char *const prof_st_name[PROF_STAGES] = {
    "U2E-clear", "U2E-transform", "U2E-sort", "U2E-polys", "U2E-anim", "U2E-other",
    "U2E-fill", "U2E-clip" };
static u32 prof_st_sum[PROF_STAGES], prof_st_cur[PROF_STAGES], prof_st_max[PROF_STAGES];
static u32 prof_st_frames;
u32 prof_now(void) { return prof_on ? prof_lines() : 0; }
void prof_stage_add(int i, u32 v) {
    if (prof_on && i >= 0 && i < PROF_STAGES)
        prof_st_cur[i] += v;
}
void prof_stage_frame(void) {
    if (!prof_on)
        return;
    for (int i = 0; i < PROF_STAGES; i++) {
        prof_st_sum[i] += prof_st_cur[i];
        if (prof_st_cur[i] > prof_st_max[i]) prof_st_max[i] = prof_st_cur[i];
        prof_st_cur[i] = 0;
    }
    prof_st_frames++;
}
void prof_mark(const char *tag) {
    if (!prof_on || pm_n >= PROF_MARKS)
        return;
    pm_tag[pm_n] = tag;
    pm_pos[pm_n] = (u16)((np_ord << 8) | (np_row & 0xff));
    pm_n++;
}
static void prof_add(int sc, u32 r, u32 c, u32 fr) {
    if (sc < 0 || sc >= PROF_SCENES)
        return;
    prof_n[sc]++;
    prof_r[sc] += r;
    prof_c[sc] += c;
    prof_f[sc] += fr;
    if (r > prof_rmax[sc]) prof_rmax[sc] = r;
    if (fr > prof_fmax[sc]) prof_fmax[sc] = fr;
}
static char *prof_num(char *p, u32 v) {
    char t[12];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
    while (n) *p++ = t[--n];
    return p;
}
static void prof_write(void) {
    static char buf[4096];
    static const char *names[PROF_SCENES] = {
        "ALKU", "U2A", "PAM", "BEGLOGO", "GLENZ", "TUNNELI", "TECHNO", "PANIC",
        "MNTSCRL", "LNS&ZOOM", "PLZPART", "MINVBALL", "RAYSCRL", "3DSINFLD",
        "JPLOGO", "DDSTARS", "ENDLOGO", "ENDSCRL", "U2E", "CRED" };
    char *p = buf;
    const char *h = "part frames | render avg max | c2p avg | frame avg max  (lines, 1 line = 64us, 313 = 1 VBL)\n";
    while (*h) *p++ = *h++;
    for (int i = 0; i < PROF_SCENES; i++) {
        if (!prof_n[i])
            continue;
        const char *nm = names[i];
        while (*nm) *p++ = *nm++;
        *p++ = ' '; p = prof_num(p, prof_n[i]);
        *p++ = ' '; *p++ = '|'; *p++ = ' '; p = prof_num(p, prof_r[i] / prof_n[i]);
        *p++ = ' '; p = prof_num(p, prof_rmax[i]);
        *p++ = ' '; *p++ = '|'; *p++ = ' '; p = prof_num(p, prof_c[i] / prof_n[i]);
        *p++ = ' '; *p++ = '|'; *p++ = ' '; p = prof_num(p, prof_f[i] / prof_n[i]);
        *p++ = ' '; p = prof_num(p, prof_fmax[i]);
        *p++ = '\n';
        if (p > buf + sizeof(buf) - 128)
            break;
    }
    for (int i = 0; i < PROF_STAGES && prof_st_frames && p < buf + sizeof(buf) - 64; i++) {
        const char *t = "stage ";
        while (*t) *p++ = *t++;
        t = prof_st_name[i];
        while (*t) *p++ = *t++;
        *p++ = ' '; p = prof_num(p, prof_st_sum[i] / prof_st_frames);
        *p++ = ' '; p = prof_num(p, prof_st_max[i]);
        *p++ = '\n';
    }
    if (prof_back_seen && p < buf + sizeof(buf) - 128) {
        const char *t = "clock-back prevframe prevvpos frame vpos last raw ";
        while (*t) *p++ = *t++;
        p = prof_num(p, prof_back_prev_frame);
        *p++ = ' '; p = prof_num(p, prof_back_prev_vpos);
        *p++ = ' '; p = prof_num(p, prof_back_frame);
        *p++ = ' '; p = prof_num(p, prof_back_vpos);
        *p++ = ' '; p = prof_num(p, prof_back_last);
        *p++ = ' '; p = prof_num(p, prof_back_raw);
        *p++ = '\n';
    }
    for (int i = 0; i < pm_n && p < buf + sizeof(buf) - 64; i++) {
        const char *t = "mark ";
        while (*t) *p++ = *t++;
        t = pm_tag[i];
        while (*t) *p++ = *t++;
        *p++ = ' ';
        p = prof_num(p, pm_pos[i] >> 8);
        *p++ = ':';
        p = prof_num(p, pm_pos[i] & 0xff);
        *p++ = '\n';
    }
    BPTR fh = Open((CONST_STRPTR)"PROGDIR:data/prof.txt", MODE_NEWFILE);
    if (fh) {
        Write(fh, buf, p - buf);
        Close(fh);
    }
}

/* ---- 実機用の転送速度テスト (data/bench.cfg があるときだけ) ----
 * c2p をアセンブラ化・Blitter 化する前に、実機 68060 で「チップ RAM へ
 * 書くだけの時間」と「Blitter の転送時間」を測る (2026-11-15 ユーザー
 * 提案)。8 プレーン FMODE=3 の画面表示と音楽を動かしたまま、各項目を
 * BENCH_N 回測り、data/bench.txt に平均・最小・最大 (走査線) を書いて
 * 終了する。どれも 64000 バイト (320x200x8 プレーン分) を扱う。 */
/* ---- 画面の使われ方の集計 (data/analyze.cfg があるときだけ) ----
 * パートごとに、32 ドット x 1 行のブロック単位で
 *   - 一度でも 0 以外だった列 (10 列) と行の範囲
 *   - 同じ裏画面の前回 (2 フレーム前) から変わったブロックの割合
 *   - 一度でも変わった列と行の範囲
 * を数え、最後のパート ENDSCRL に入ったところで終了し data/analyze.txt に書く。
 * c2p を「絵のある範囲」「変わった範囲」だけにできるかを、推測ではなく
 * 実測で判断するため (2026-11-15)。WinUAE で使う (重いので実機の速さは
 * 関係ない)。chunky で描くパートは chunky を、プレーンへ直接描くパートは
 * プレーン (8 枚の同じ位置の 32bit) を見る。 */
static int anl_on;
#define ANL_BYTES (8u * 40u * 256u)
static u8 *anl_prev[2];
static u32 anl_n[PROF_SCENES], anl_chg[PROF_SCENES];
static u16 anl_nzcol[PROF_SCENES], anl_chgcol[PROF_SCENES];
static s16 anl_nzr0[PROF_SCENES], anl_nzr1[PROF_SCENES];
static s16 anl_chr0[PROF_SCENES], anl_chr1[PROF_SCENES];
static u8 anl_kind[PROF_SCENES];
static u32 anl_frame_no;
static int anl_first_scene = -1;

static void anl_frame(int sc, const u8 *chunky, const u8 *planes) {
    if (sc < 0 || sc >= PROF_SCENES)
        return;
    if (!anl_prev[0]) {
        anl_prev[0] = (u8 *)mem_alloc(ANL_BYTES, MEMF_FAST | MEMF_CLEAR);
        anl_prev[1] = (u8 *)mem_alloc(ANL_BYTES, MEMF_FAST | MEMF_CLEAR);
        for (int i = 0; i < PROF_SCENES; i++) {
            anl_nzr0[i] = anl_chr0[i] = 999;
            anl_nzr1[i] = anl_chr1[i] = -1;
        }
        if (!anl_prev[1])
            return;
    }
    if (anl_first_scene < 0)
        anl_first_scene = sc;
    u8 *prev = anl_prev[anl_frame_no++ & 1];
    u32 chg = 0;
    anl_n[sc]++;
    anl_kind[sc] = chunky ? 1 : 2;
    for (int y = 0; y < 256; y++) {
        for (int bx = 0; bx < 10; bx++) {
            u32 nz = 0, df = 0;
            if (chunky) {
                const u32 *s = (const u32 *)(chunky + y * 320 + bx * 32);
                u32 *pv = (u32 *)(prev + y * 320 + bx * 32);
                for (int k = 0; k < 8; k++) {
                    nz |= s[k];
                    df |= s[k] ^ pv[k];
                    pv[k] = s[k];
                }
            } else {
                for (int pl = 0; pl < 8; pl++) {
                    u32 o = pl * 10240u + y * 40 + bx * 4;
                    u32 s = *(const u32 *)(planes + o);
                    u32 *pv = (u32 *)(prev + o);
                    nz |= s;
                    df |= s ^ *pv;
                    *pv = s;
                }
            }
            if (nz) {
                anl_nzcol[sc] |= (u16)(1u << bx);
                if (y < anl_nzr0[sc]) anl_nzr0[sc] = (s16)y;
                if (y > anl_nzr1[sc]) anl_nzr1[sc] = (s16)y;
            }
            if (df) {
                chg++;
                anl_chgcol[sc] |= (u16)(1u << bx);
                if (y < anl_chr0[sc]) anl_chr0[sc] = (s16)y;
                if (y > anl_chr1[sc]) anl_chr1[sc] = (s16)y;
            }
        }
    }
    anl_chg[sc] += chg;
}

static char anl_buf[4096];
static int anl_len;
static void anl_finish(void) {
    static const char *names[PROF_SCENES] = {
        "ALKU", "U2A", "PAM", "BEGLOGO", "GLENZ", "TUNNELI", "TECHNO", "PANIC",
        "MNTSCRL", "LNS&ZOOM", "PLZPART", "MINVBALL", "RAYSCRL", "3DSINFLD",
        "JPLOGO", "DDSTARS", "ENDLOGO", "ENDSCRL", "U2E", "CRED" };
    char *p = anl_buf;
    const char *h = "part frames src | nonzero cols(0..9) rows | changed blocks per frame (per mille) | changed cols rows\n";
    while (*h) *p++ = *h++;
    for (int i = 0; i < PROF_SCENES; i++) {
        if (!anl_n[i])
            continue;
        const char *nm = names[i];
        while (*nm) *p++ = *nm++;
        *p++ = ' '; p = prof_num(p, anl_n[i]);
        *p++ = ' '; *p++ = anl_kind[i] == 1 ? 'C' : 'P';
        *p++ = ' '; *p++ = '|'; *p++ = ' ';
        for (int b = 0; b < 10; b++) *p++ = (anl_nzcol[i] >> b) & 1 ? '#' : '.';
        *p++ = ' '; p = prof_num(p, anl_nzr0[i] < 0 || anl_nzr0[i] > 255 ? 0 : (u32)anl_nzr0[i]);
        *p++ = '-'; p = prof_num(p, anl_nzr1[i] < 0 ? 0 : (u32)anl_nzr1[i]);
        *p++ = ' '; *p++ = '|'; *p++ = ' ';
        p = prof_num(p, (u32)((anl_chg[i] * 1000u) / (anl_n[i] * 2560u)));
        *p++ = ' '; *p++ = '|'; *p++ = ' ';
        for (int b = 0; b < 10; b++) *p++ = (anl_chgcol[i] >> b) & 1 ? '#' : '.';
        *p++ = ' '; p = prof_num(p, anl_chr0[i] < 0 || anl_chr0[i] > 255 ? 0 : (u32)anl_chr0[i]);
        *p++ = '-'; p = prof_num(p, anl_chr1[i] < 0 ? 0 : (u32)anl_chr1[i]);
        *p++ = '\n';
        if (p > anl_buf + sizeof(anl_buf) - 128)
            break;
    }
    anl_len = (int)(p - anl_buf);
}
static void anl_write(void) {
    BPTR fh = Open((CONST_STRPTR)"PROGDIR:data/analyze.txt", MODE_NEWFILE);
    if (fh) {
        Write(fh, anl_buf, anl_len);
        Close(fh);
    }
}

static int bench_on;
#define BENCH_N 16
#define BENCH_BYTES 64000u
static char *bench_line(char *p, const char *name, u32 *v) {
    u32 sum = 0, mn = 0xffffffffu, mx = 0;
    for (int i = 0; i < BENCH_N; i++) {
        sum += v[i];
        if (v[i] < mn) mn = v[i];
        if (v[i] > mx) mx = v[i];
    }
    while (*name) *p++ = *name++;
    *p++ = ' '; p = prof_num(p, sum / BENCH_N);
    *p++ = ' '; p = prof_num(p, mn);
    *p++ = ' '; p = prof_num(p, mx);
    *p++ = '\n';
    return p;
}
static void blit_wait(void) {
    (void)custom->dmaconr;
    while (custom->dmaconr & (1 << 14)) { }
}
/* BLTSIZV/BLTSIZH (ECS 以降) で 40 ワード x 800 行 = 64000 バイトを 1 回で。 */
static void blit_run(u16 con0, u16 con1, void *a, void *b, void *c, void *d) {
    blit_wait();
    custom->bltcon0 = con0;
    custom->bltcon1 = con1;
    custom->bltafwm = 0xffff;
    custom->bltalwm = 0xffff;
    custom->bltamod = custom->bltbmod = custom->bltcmod = custom->bltdmod = 0;
    custom->bltapt = a;
    custom->bltbpt = b;
    custom->bltcpt = c;
    custom->bltdpt = d;
    custom->bltcdat = 0x0f0f;
    custom->bltsizv = 800;
    custom->bltsizh = 40;          /* ここで開始 */
    blit_wait();
}
static char bench_buf[4096];
static int bench_len;
static void bench_run(void) {
    char *buf = bench_buf;
    static u32 v[BENCH_N];
    u8 *chunky = video_chunky();
    u8 *chip = video_back_planes();                        /* 8*40*256 */
    u8 *fast = (u8 *)mem_alloc(8 * 40 * 256, MEMF_FAST | MEMF_CLEAR);
    u8 *ca = (u8 *)mem_alloc(BENCH_BYTES, MEMF_CHIP | MEMF_CLEAR);
    u8 *cb = (u8 *)mem_alloc(BENCH_BYTES, MEMF_CHIP | MEMF_CLEAR);
    u8 *cc = (u8 *)mem_alloc(BENCH_BYTES, MEMF_CHIP | MEMF_CLEAR);
    char *p = buf;
    const char *h = "bench avg min max  (lines, 1 line = 64us, 313 = 1 VBL; 64000 bytes or 64000 operations each)\n";
    while (*h) *p++ = *h++;
    if (!fast || !ca || !cb || !cc) {
        h = "alloc failed\n";
        while (*h) *p++ = *h++;
        goto out;
    }
    /* 0. アセンブラ版 c2p (c2p8_rect / c2p8_fast) と C 版の出力照合。
     *    乱数の chunky を両方でファスト RAM へ変換し 1 バイトずつ比べる。
     *    起動時の c2p_selftest (基準実装 c2p8 と c2p8_fast の比較) の
     *    結果も書く。 */
    {
        u8 *ref = (u8 *)mem_alloc(8 * 40 * 256, MEMF_FAST | MEMF_CLEAR);
        u32 r = 0x12345678u;
        for (u32 i = 0; i < SCREEN_W * SCREEN_H; i++) {
            r ^= r << 13; r ^= r >> 17; r ^= r << 5;
            chunky[i] = (u8)r;
        }
        if (!ref) {
            h = "c2p_asm_check alloc_failed\n";
            while (*h) *p++ = *h++;
        } else {
            u32 bad = 0;
            c2p8_rect(chunky + 28 * SCREEN_W, fast, SCREEN_W, SCREEN_H, 28, 200);
            c2p8_rect_c(chunky + 28 * SCREEN_W, ref, SCREEN_W, SCREEN_H, 28, 200);
            for (u32 i = 0; i < 8u * 40u * 256u && !bad; i++)
                if (fast[i] != ref[i]) bad = i + 1;
            c2p8_fast(chunky, fast, SCREEN_W, SCREEN_H);
            c2p8_rect_c(chunky, ref, SCREEN_W, SCREEN_H, 0, SCREEN_H);
            for (u32 i = 0; i < 8u * 40u * 256u && !bad; i++)
                if (fast[i] != ref[i]) bad = 0x100000u + i + 1;
            h = bad ? "c2p_asm_check MISMATCH " : "c2p_asm_check ok";
            while (*h) *p++ = *h++;
            if (bad)
                p = prof_num(p, bad);
            *p++ = '\n';
            mem_free(ref, 8 * 40 * 256);
        }
        h = c2p_test_result ? "c2p_selftest MISMATCH\n" : "c2p_selftest ok\n";
        while (*h) *p++ = *h++;
    }
    for (u32 i = 0; i < SCREEN_W * SCREEN_H; i++)
        chunky[i] = (u8)(i * 7 + (i >> 8));

    /* 1. CPU: チップ RAM へ 32bit で書くだけ (計算なし)。
     *    c2p8_rect と同じ範囲 (各プレーンの 28..227 行)。 */
    for (int n = 0; n < BENCH_N; n++) {
        u32 t0 = prof_lines();
        for (int pl = 0; pl < 8; pl++) {
            volatile u32 *d = (volatile u32 *)(chip + pl * 40 * 256 + 28 * 40);
            for (int i = 0; i < 200 * 40 / 4; i++)
                d[i] = (u32)i;
        }
        v[n] = prof_lines() - t0;
    }
    p = bench_line(p, "cpu_chip_write", v);

    /* 2. 今の c2p (C 版) でチップ RAM へ。prof の c2p と同じ処理。 */
    for (int n = 0; n < BENCH_N; n++) {
        u32 t0 = prof_lines();
        c2p8_rect(chunky + 28 * SCREEN_W, chip, SCREEN_W, SCREEN_H, 28, 200);
        v[n] = prof_lines() - t0;
    }
    p = bench_line(p, "c2p_to_chip", v);
    for (int n = 0; n < BENCH_N; n++) {
        u32 t0 = prof_lines();
        c2p8_rect_c(chunky + 28 * SCREEN_W, chip, SCREEN_W, SCREEN_H, 28, 200);
        v[n] = prof_lines() - t0;
    }
    p = bench_line(p, "c2p_C_to_chip", v);

    /* 3. 同じ c2p をファスト RAM へ (計算だけの時間)。 */
    for (int n = 0; n < BENCH_N; n++) {
        u32 t0 = prof_lines();
        c2p8_rect(chunky + 28 * SCREEN_W, fast, SCREEN_W, SCREEN_H, 28, 200);
        v[n] = prof_lines() - t0;
    }
    p = bench_line(p, "c2p_to_fast", v);

    /* 4. CPU: ファスト RAM -> チップ RAM へ 32bit で写す。 */
    for (int n = 0; n < BENCH_N; n++) {
        u32 t0 = prof_lines();
        for (int pl = 0; pl < 8; pl++) {
            const u32 *s = (const u32 *)(fast + pl * 40 * 256 + 28 * 40);
            volatile u32 *d = (volatile u32 *)(chip + pl * 40 * 256 + 28 * 40);
            for (int i = 0; i < 200 * 40 / 4; i++)
                d[i] = s[i];
        }
        v[n] = prof_lines() - t0;
    }
    p = bench_line(p, "cpu_fast_to_chip", v);

    /* 5. Blitter: D だけ (塗りつぶし、minterm 0xFF)。 */
    for (int n = 0; n < BENCH_N; n++) {
        u32 t0 = prof_lines();
        blit_run(0x01FF, 0, 0, 0, 0, ca);
        v[n] = prof_lines() - t0;
    }
    p = bench_line(p, "blit_D_fill", v);

    /* 6. Blitter: A -> D (チップ -> チップのコピー)。 */
    for (int n = 0; n < BENCH_N; n++) {
        u32 t0 = prof_lines();
        blit_run(0x09F0, 0, ca, 0, 0, cb);
        v[n] = prof_lines() - t0;
    }
    p = bench_line(p, "blit_AD_copy", v);

    /* 7. Blitter: A(4 ビットシフト), B, C(マスク) -> D。c2p の 1 段に相当
     *    (D = C ? A : B, minterm 0xCA と同じ形の 0xE4)。 */
    for (int n = 0; n < BENCH_N; n++) {
        u32 t0 = prof_lines();
        blit_run(0x4FE4, 0, ca, cb, cc, chip + 28 * 40);
        v[n] = prof_lines() - t0;
    }
    p = bench_line(p, "blit_ABCD_merge", v);

    /* 8. 7 と同じ Blitter を動かしながら、CPU はファスト RAM で c2p を
     *    する (並列にしたときの CPU 側の時間と、全体の時間)。 */
    for (int n = 0; n < BENCH_N; n++) {
        blit_wait();
        custom->bltcon0 = 0x4FE4;
        custom->bltcon1 = 0;
        custom->bltafwm = 0xffff;
        custom->bltalwm = 0xffff;
        custom->bltamod = custom->bltbmod = custom->bltcmod = custom->bltdmod = 0;
        custom->bltapt = ca;
        custom->bltbpt = cb;
        custom->bltcpt = cc;
        custom->bltdpt = chip + 28 * 40;
        u32 t0 = prof_lines();
        custom->bltsizv = 800;
        custom->bltsizh = 40;
        c2p8_rect(chunky + 28 * SCREEN_W, fast, SCREEN_W, SCREEN_H, 28, 200);
        blit_wait();
        v[n] = prof_lines() - t0;
    }
    p = bench_line(p, "blit_merge+cpu_c2p_fast_total", v);

    /* ---- ここから 2026-11-15 追加分 (改善の判断材料) ---- */
    /* チップ RAM への書き込み幅の違い (16bit / 8bit)。 */
    for (int n = 0; n < BENCH_N; n++) {
        volatile u16 *d = (volatile u16 *)(chip + 28 * 40);
        u32 t0 = prof_lines();
        for (int i = 0; i < (int)BENCH_BYTES / 2; i++)
            d[i] = (u16)i;
        v[n] = prof_lines() - t0;
    }
    p = bench_line(p, "cpu_chip_write16", v);
    for (int n = 0; n < BENCH_N; n++) {
        volatile u8 *d = chip + 28 * 40;
        u32 t0 = prof_lines();
        for (int i = 0; i < (int)BENCH_BYTES; i++)
            d[i] = (u8)i;
        v[n] = prof_lines() - t0;
    }
    p = bench_line(p, "cpu_chip_write8", v);
    /* チップ RAM からの読み出し (32bit)。 */
    {
        u32 sum = 0;
        for (int n = 0; n < BENCH_N; n++) {
            volatile const u32 *s = (volatile const u32 *)(chip + 28 * 40);
            u32 t0 = prof_lines();
            for (int i = 0; i < (int)BENCH_BYTES / 4; i++)
                sum += s[i];
            v[n] = prof_lines() - t0;
        }
        p = bench_line(p, "cpu_chip_read32", v);
        chunky[0] = (u8)sum;
    }
    /* ファスト RAM の書き込み・コピー (32bit)。 */
    for (int n = 0; n < BENCH_N; n++) {
        volatile u32 *d = (volatile u32 *)fast;
        u32 t0 = prof_lines();
        for (int i = 0; i < (int)BENCH_BYTES / 4; i++)
            d[i] = (u32)i;
        v[n] = prof_lines() - t0;
    }
    p = bench_line(p, "cpu_fast_write32", v);
    for (int n = 0; n < BENCH_N; n++) {
        const u32 *s = (const u32 *)chunky;
        volatile u32 *d = (volatile u32 *)fast;
        u32 t0 = prof_lines();
        for (int i = 0; i < (int)BENCH_BYTES / 4; i++)
            d[i] = s[i];
        v[n] = prof_lines() - t0;
    }
    p = bench_line(p, "cpu_fast_copy32", v);
    /* ファスト RAM の 1 バイト読み出し: 順番どおり と ばらばら
     * (絵の先頭 64KB の範囲を引く。TUNNELI などのテクスチャ参照に相当)。 */
    if (g_jplogo_pic) {
        const volatile u8 *tab = g_jplogo_pic;
        u32 sum = 0;
        for (int n = 0; n < BENCH_N; n++) {
            u32 t0 = prof_lines();
            for (u32 i = 0; i < BENCH_BYTES; i++)
                sum += tab[i];
            v[n] = prof_lines() - t0;
        }
        p = bench_line(p, "fast_seq_read8", v);
        for (int n = 0; n < BENCH_N; n++) {
            u32 r = 12345;
            u32 t0 = prof_lines();
            for (u32 i = 0; i < BENCH_BYTES; i++) {
                r = r * 1103515245u + 12345u;
                sum += tab[(r >> 15) & 0xffff];
            }
            v[n] = prof_lines() - t0;
        }
        p = bench_line(p, "fast_rand_read8(+lcg)", v);
        /* 同じ LCG だけ (上との差がメモリ待ちの分) */
        for (int n = 0; n < BENCH_N; n++) {
            u32 r = 12345;
            u32 t0 = prof_lines();
            for (u32 i = 0; i < BENCH_BYTES; i++) {
                r = r * 1103515245u + 12345u;
                sum += (r >> 15) & 0xffff;
            }
            v[n] = prof_lines() - t0;
        }
        p = bench_line(p, "lcg_only", v);
        chunky[1] = (u8)sum;
    }
    /* 演算 1 回のコスト (64000 回)。loop_empty(add) との差が演算の分。 */
    {
        volatile s32 va = 1234567, vb = 321;
        volatile long long vc = 1234567, vd = 321;
        s32 acc = 0;
        long long acc64 = 0;
        for (int n = 0; n < BENCH_N; n++) {
            u32 t0 = prof_lines();
            for (int i = 0; i < (int)BENCH_BYTES; i++)
                acc += va + vb;
            v[n] = prof_lines() - t0;
        }
        p = bench_line(p, "loop_empty(add)", v);
        for (int n = 0; n < BENCH_N; n++) {
            u32 t0 = prof_lines();
            for (int i = 0; i < (int)BENCH_BYTES; i++)
                acc += va * vb;
            v[n] = prof_lines() - t0;
        }
        p = bench_line(p, "mul32", v);
        for (int n = 0; n < BENCH_N; n++) {
            u32 t0 = prof_lines();
            for (int i = 0; i < (int)BENCH_BYTES; i++)
                acc += va / vb;
            v[n] = prof_lines() - t0;
        }
        p = bench_line(p, "div32", v);
        for (int n = 0; n < BENCH_N; n++) {
            u32 t0 = prof_lines();
            for (int i = 0; i < (int)BENCH_BYTES; i++)
                acc64 += vc * vd;
            v[n] = prof_lines() - t0;
        }
        p = bench_line(p, "mul64(muldi3)", v);
        /* framework/mul64.h: 16 ビット分割版と (68060 以外の) MULS.L 64 ビット形。
         * 結果の照合と、64000 回の時間 (2026-11-16)。 */
        {
            volatile int va2 = 1234567, vb2 = -7654321;
            long long sacc = 0;
            for (int n = 0; n < BENCH_N; n++) {
                u32 t0 = prof_lines();
                for (int i = 0; i < (int)BENCH_BYTES; i++)
                    sacc += smul64_split(va2, vb2);
                v[n] = prof_lines() - t0;
            }
            p = bench_line(p, "smul64_split", v);
            if (g_cpu_mul64) {
                u32 r = 0x2468ace1u, bad = 0;
                for (int i = 0; i < 100000; i++) {
                    r ^= r << 13; r ^= r >> 17; r ^= r << 5;
                    int a = (int)r;
                    r ^= r << 13; r ^= r >> 17; r ^= r << 5;
                    int b = (int)r;
                    if (smul64_hw(a, b) != smul64_split(a, b))
                        bad++;
                }
                h = bad ? "mul64_hw_check MISMATCH" + 0 : "mul64_hw_check ok";
                while (*h) *p++ = *h++;
                *p++ = '\n';
                for (int n = 0; n < BENCH_N; n++) {
                    u32 t0 = prof_lines();
                    for (int i = 0; i < (int)BENCH_BYTES; i++)
                        sacc += smul64_hw(va2, vb2);
                    v[n] = prof_lines() - t0;
                }
                p = bench_line(p, "smul64_hw(muls.l 64)", v);
            }
            chunky[3] = (u8)sacc;
        }
        chunky[2] = (u8)(acc + (s32)acc64);
    }
    /* Blitter (合成) が動いている間に CPU がチップ RAM に書く時間
     * (バスの取り合い)。 */
    for (int n = 0; n < BENCH_N; n++) {
        blit_wait();
        custom->bltcon0 = 0x4FE4;
        custom->bltcon1 = 0;
        custom->bltafwm = 0xffff;
        custom->bltalwm = 0xffff;
        custom->bltamod = custom->bltbmod = custom->bltcmod = custom->bltdmod = 0;
        custom->bltapt = ca;
        custom->bltbpt = cb;
        custom->bltcpt = cc;
        custom->bltdpt = ca;
        custom->bltsizv = 800;
        custom->bltsizh = 40;
        u32 t0 = prof_lines();
        for (int pl = 0; pl < 8; pl++) {
            volatile u32 *d = (volatile u32 *)(chip + pl * 40 * 256 + 28 * 40);
            for (int i = 0; i < 200 * 40 / 4; i++)
                d[i] = (u32)i;
        }
        v[n] = prof_lines() - t0;
        blit_wait();
    }
    p = bench_line(p, "cpu_chip_write_during_blit", v);
    /* BLITHOG (Blitter 優先) にした合成。 */
    custom->dmacon = DMAF_SETCLR | DMAF_BLITHOG;
    for (int n = 0; n < BENCH_N; n++) {
        u32 t0 = prof_lines();
        blit_run(0x4FE4, 0, ca, cb, cc, chip + 28 * 40);
        v[n] = prof_lines() - t0;
    }
    custom->dmacon = DMAF_BLITHOG;
    p = bench_line(p, "blit_ABCD_merge_hog", v);

    /* 表示窓を 200 行にしたとき (上下 56 行のビットプレーン DMA が止まる)
     * のチップ RAM 書き込みと c2p (2026-11-16 ユーザー提案の効果測定)。
     * 1 と 2 (256 行表示) と同じ処理。 */
    video_set_window200(1);
    vblank_wait();
    vblank_wait();
    for (int n = 0; n < BENCH_N; n++) {
        u32 t0 = prof_lines();
        for (int pl = 0; pl < 8; pl++) {
            volatile u32 *d = (volatile u32 *)(chip + pl * 40 * 256 + 28 * 40);
            for (int i = 0; i < 200 * 40 / 4; i++)
                d[i] = (u32)i;
        }
        v[n] = prof_lines() - t0;
    }
    p = bench_line(p, "cpu_chip_write_win200", v);
    for (int n = 0; n < BENCH_N; n++) {
        u32 t0 = prof_lines();
        c2p8_rect(chunky + 28 * SCREEN_W, chip, SCREEN_W, SCREEN_H, 28, 200);
        v[n] = prof_lines() - t0;
    }
    p = bench_line(p, "c2p_to_chip_win200", v);
    video_set_window200(0);

out:
    bench_len = (int)(p - buf);
}
/* DOS への書き込みは FreeSystem の後 (prof_write と同じ)。 */
static void bench_write(void) {
    BPTR fh = Open((CONST_STRPTR)"PROGDIR:data/bench.txt", MODE_NEWFILE);
    if (fh) {
        Write(fh, bench_buf, bench_len);
        Close(fh);
    }
}

static int c2p_full_frames = VIDEO_NBUF;
static int c2p_last_scene = -1;
/* 裏バッファごとの「上下の帯 (0..27, 228..255 行) に何か残っているかも」
 * フラグ。200 行だけ変換するときは、汚れているバッファの帯を先に 0 に
 * する。以前は前のシーンが 256 行書いた分が帯に残ることがあった
 * (2026-11-15 ユーザー報告)。 */
/* 画面バッファの番号 (トリプルバッファ、video_back_index) ごとに持つ */
static u8 lb_dirty[VIDEO_NBUF] = { 1, 1, 1 };
static int lb_index(u8 *planes) {
    (void)planes;
    return video_back_index();
}
static void lb_mark_all_dirty(void) {
    for (int i = 0; i < VIDEO_NBUF; i++)
        lb_dirty[i] = 1;
}
static void lb_clear(u8 *planes) {
    for (int p = 0; p < 8; p++) {
        u32 *top = (u32 *)(planes + p * PLANE_BYTES);
        u32 *bot = (u32 *)(planes + p * PLANE_BYTES + 228 * 40);
        for (int i = 0; i < 28 * 40 / 4; i++)
            top[i] = bot[i] = 0;
    }
}
static void c2p_scene(const u8 *chunky, u8 *planes, int scene) {
    const u32 *top = (const u32 *)chunky;
    const u32 *bot = (const u32 *)(chunky + 228 * SCREEN_W);
    int bi = lb_index(planes);
    u32 any = 0;
    for (int i = 0; i < 28 * SCREEN_W / 4; i++)
        any |= top[i] | bot[i];
    if (scene != c2p_last_scene) {
        c2p_last_scene = scene;
        c2p_full_frames = VIDEO_NBUF;
    }
    if (any)
        c2p_full_frames = VIDEO_NBUF;
    /* PANIC (TV の消灯) は前のパートの 256 行の絵を縦に潰していくので、
     * 帯の消し残しが出ないよう常に 256 行を変換する (計算は軽い。
     * 2026-11-15 ユーザー報告: 実機で範囲外が消えていなかった)。 */
    if (scene == 7)
        c2p_full_frames = 1;
    if (c2p_full_frames > 0) {
        c2p8_fast(chunky, planes, SCREEN_W, SCREEN_H);
        c2p_full_frames--;
        lb_dirty[bi] = any ? 1 : 0;
    } else {
        if (lb_dirty[bi]) {
            lb_clear(planes);
            lb_dirty[bi] = 0;
        }
        c2p8_rect(chunky + 28 * SCREEN_W, planes, SCREEN_W, SCREEN_H, 28, 200);
    }
}

int main(void) {
    SysBase = *((struct ExecBase **)4UL);
    /* 68060 (68060.library が AFF_68060 を立てる) 以外は MULS.L 64 ビット形を使う */
    g_cpu_mul64 = (SysBase->AttnFlags & AFF_68060) ? 0 : 1;
    /* Workbench (アイコン) から起動されたときは WBStartup メッセージを
     * 受け取り、終了時に返す。受け取らないと Workbench が待ち続ける。
     * PROGDIR: は Workbench 起動でも exe のフォルダを指す。 */
    struct WBStartup *wbmsg = 0;
    {
        struct Process *me = (struct Process *)FindTask(0);
        if (!me->pr_CLI) {
            WaitPort(&me->pr_MsgPort);
            wbmsg = (struct WBStartup *)GetMsg(&me->pr_MsgPort);
        }
    }
    custom = (struct Custom *)0xDFF000;
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 0);
    if (!GfxBase)
        { mem_free_all(); if (wbmsg) { Forbid(); ReplyMsg((struct Message *)wbmsg); return 20; } Exit(20); }
    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 0);


    /* アセットのロードは TakeSystem の前に行う (フロッピー I/O は割り込みが必要)。
     * 音楽は MUSIC0 を実行ファイルに埋め込んである (data/music0.srm)。
     * オリジナルと同じく前半は MUSIC0 を使う。MUSIC1 (PROGDIR:music1.srm) は
     * 後半パート用。MUSIC1 を最初から鳴らすと MUSIC0 の約 2 倍の速さ
     * (17 行/秒 vs 8 行/秒) になり、導入部のテンポが合わない。 */
    u32 img_size = 0;
    void *img_loaded = file_load_fast("PROGDIR:data/title.raw", 82704u, &img_size);
    g_image_data = (const u8 *)img_loaded;
    u32 end_size = 0;
    void *end_loaded = file_load_fast("PROGDIR:data/endpic.raw", 82704u, &end_size);
    g_endpic_data = (const u8 *)end_loaded;
    u32 hoi_size = 0;
    void *hoi_loaded = file_load_fast("PROGDIR:data/hoi.uh", 128784u, &hoi_size);
    g_hoi_data = (const u8 *)hoi_loaded;
    u32 fona_size = 0;
    void *fona_loaded = file_load_fast("PROGDIR:data/fona.uh", 45784u, &fona_size);
    g_fona_data = (const u8 *)fona_loaded;

    /* 後半パート用の MUSIC1 (720548 バイト) */
    u32 music1_size = 0;
    g_music1_data = (const u8 *)file_load_fast("PROGDIR:data/music1.srm", 720548u, &music1_size);

    /* std 版 S3M で削除された 0xFE マーカーの位置 (ROADMAP 12.28)。
     * 値は reference/SecondReality/MAIN/MUSIC0.S3M / MUSIC1.S3M の
     * オーダーリストの 0xFE を、std の「直後のオーダーの添字」に直したもの。
     *   MUSIC0.S3M: 27
     *   MUSIC1.S3M: 3,14,19,25,27,38,41,46,49,61,67,76,87,92,97
     *     → std: 3,13,17,22,23,33,35,39,41,52,57,65,75,79,83
     * dis_musplus() の待ち (END.C:110 など) はこのマーカーで動く。 */
    {
        static const u16 music0_markers[] = { 27 };
        static const u16 music1_markers[] = {
            3, 13, 17, 22, 23, 33, 35, 39, 41, 52, 57, 65, 75, 79, 83
        };
        zsetmarkers((const void *)music0_data, music0_markers, 1);
        if (g_music1_data)
            zsetmarkers((const void *)g_music1_data, music1_markers, 15);
    }

    /* デバッグ: PCM 直接再生 (PROGDIR:pcmtest.cfg があるときだけ) */
    {
        u32 cfg_size = 0;
        const u8 *cfg = (const u8 *)file_load_fast("PROGDIR:data/pcmtest.cfg", 8u, &cfg_size);
        if (cfg && cfg_size) {
            u32 pcm_size = 0;
            g_pcm_data = (const u8 *)file_load_fast("PROGDIR:data/music1_pcm.raw", 1875000u, &pcm_size);
        }
    }

    /* WATER (RAYSCRL) */
    u32 water_size = 0;
    /* CREDITS */
    g_credits_pics = (const u8 *)file_load_fast("PROGDIR:data/credits_pics.bin", 400000u, &water_size);
    g_credits_font = (const u8 *)file_load_fast("PROGDIR:data/credits_font.bin", 48000u, &water_size);
    /* ENDSCRL のテキスト (reference/SecondReality/MAIN/DATA/ENDSCROL.TXT) */
    g_endscrol_data = (const u8 *)file_load_fast("PROGDIR:data/endscrol.txt", 6200u, &water_size);
    g_endscrl_font = (const u8 *)file_load_fast("PROGDIR:data/endscrl_font.bin", 38750u, &water_size);
    /* U2E (Vector Part II) */
    /* u2e_obj.bin の実サイズは 67460 バイト (assets/u2e/u2e_obj.bin)。
     * 60000 では後半のオブジェクト (talot04 / plushouse など) が
     * ゼロのままになり、ビルが描かれない。 */
    g_u2e_obj = (const u8 *)file_load_fast("PROGDIR:data/u2e_obj.bin", 67460u, &water_size);
    g_u2e_inst = (const u8 *)file_load_fast("PROGDIR:data/u2e_inst.bin", 256u, &water_size);
    g_u2e_script = (const u8 *)file_load_fast("PROGDIR:data/u2e_script.bin", 78000u, &water_size);
    g_u2e_pal = (const u8 *)file_load_fast("PROGDIR:data/u2e_pal.bin", 768u, &water_size);
    /* 3D-Sinusfield (COMAN) wave tables */
    g_sinus_w1 = (const u8 *)file_load_fast("PROGDIR:data/w1dta.bin", 65536u, &water_size);
    g_sinus_w2 = (const u8 *)file_load_fast("PROGDIR:data/w2dta.bin", 65536u, &water_size);
    g_water_bg = (const u8 *)file_load_fast("PROGDIR:data/water_bg.bin", 64000u, &water_size);
    g_water_pal = (const u8 *)file_load_fast("PROGDIR:data/water_pal.bin", 768u, &water_size);
    g_water_font = (const u8 *)file_load_fast("PROGDIR:data/water_font.bin", 14000u, &water_size);
    g_water_wat[0] = (const u8 *)file_load_fast("PROGDIR:data/water1.bin", 23096u, &water_size);
    g_water_wat[1] = (const u8 *)file_load_fast("PROGDIR:data/water2.bin", 23034u, &water_size);
    g_water_wat[2] = (const u8 *)file_load_fast("PROGDIR:data/water3.bin", 27818u, &water_size);

    /* FOREST (MNTSCRL) */
    g_forest_bg = (const u8 *)file_load_fast("PROGDIR:data/forest_bg.bin", 64000u, &water_size);
    g_forest_pal = (const u8 *)file_load_fast("PROGDIR:data/forest_pal.bin", 768u, &water_size);
    g_forest_mask = (const u8 *)file_load_fast("PROGDIR:data/forest_mask.bin", 19840u, &water_size);
    g_forest_pos[0] = (const u8 *)file_load_fast("PROGDIR:data/forest1.bin", 25088u, &water_size);
    g_forest_pos[1] = (const u8 *)file_load_fast("PROGDIR:data/forest2.bin", 25064u, &water_size);
    g_forest_pos[2] = (const u8 *)file_load_fast("PROGDIR:data/forest3.bin", 25114u, &water_size);

    /* LNS&ZOOM (LENS) */
    g_lens_back = (const u8 *)file_load_fast("PROGDIR:data/lens_back.bin", 64000u, &water_size);
    g_lens_pal  = (const u8 *)file_load_fast("PROGDIR:data/lens_pal.bin", 768u, &water_size);
    g_lens_ex0  = (const u8 *)file_load_fast("PROGDIR:data/lens_ex0.bin", 13u, &water_size);
    g_lens_ex1  = (const u8 *)file_load_fast("PROGDIR:data/lens_ex1.bin", 19588u, &water_size);
    g_lens_ex2  = (const u8 *)file_load_fast("PROGDIR:data/lens_ex2.bin", 6860u, &water_size);
    g_lens_ex3  = (const u8 *)file_load_fast("PROGDIR:data/lens_ex3.bin", 1538u, &water_size);
    g_lens_ex4  = (const u8 *)file_load_fast("PROGDIR:data/lens_ex4.bin", 6252u, &water_size);
    g_lens_path = (const u8 *)file_load_fast("PROGDIR:data/lens_path.bin", 18862u, &water_size);

    /* JPLOGO */
    g_jplogo_pic = (const u8 *)file_load_fast("PROGDIR:data/jplogo_pic.bin", 128000u, &water_size);
    g_jplogo_pal = (const u8 *)file_load_fast("PROGDIR:data/jplogo_pal.bin", 768u, &water_size);
    g_jplogo_sin = (const u8 *)file_load_fast("PROGDIR:data/jplogo_sin.bin", 2048u, &water_size);

    /* DDSTARS */
    g_ddstars_text = (const u8 *)file_load_fast("PROGDIR:data/ddstars_text.raw", 64784u, &water_size);

    /* PAM */
    g_pam_ani = (const u8 *)file_load_fast("PROGDIR:data/pam.ani", 324625u, &g_pam_ani_size);
    g_pam_pal = (const u8 *)file_load_fast("PROGDIR:data/pam.pal", 768u, &water_size);

    /* PLZPART */
    g_plasma_psini = (const u8 *)file_load_fast("PROGDIR:data/psini.bin", 16384u, &water_size);
    g_plasma_l4 = (const u8 *)file_load_fast("PROGDIR:data/lsini4.bin", 16384u, &water_size);
    g_plasma_l16 = (const u8 *)file_load_fast("PROGDIR:data/lsini16.bin", 16384u, &water_size);

    /* PLZPART plasmacube */
    g_cube_tex = (const u8 *)file_load_fast("PROGDIR:data/cube_tex.bin", 98304u, &water_size);
    g_cube_pal = (const u8 *)file_load_fast("PROGDIR:data/cube_pal.bin", 768u, &water_size);
    g_cube_spline = (const u8 *)file_load_fast("PROGDIR:data/cube_spline.bin", 2192u, &water_size);
    g_cube_splcoef = (const u8 *)file_load_fast("PROGDIR:data/cube_splcoef.bin", 2048u, &water_size);
    g_cube_sin = (const u8 *)file_load_fast("PROGDIR:data/cube_sin.bin", 2048u, &water_size);

    /* TUNNELI */
    g_tunnel_pcalc = (const u8 *)file_load_fast("PROGDIR:data/pcalc.bin", 35328u, &water_size);
    /* TUNNELI の SINIT.DAT (SINGEN.PAS 生成: sinit 4097 + cosit 2049 words) */
    g_tunnel_sinit = (const u8 *)file_load_fast("PROGDIR:data/sinit.dat", 12292u, &water_size);

    /* GLENZ */
    g_glenz_bg = (const u8 *)file_load_fast("PROGDIR:data/glenz_bg.bin", 64000u, &water_size);
    g_glenz_pal = (const u8 *)file_load_fast("PROGDIR:data/glenz_pal.bin", 768u, &water_size);
    g_glenz_sin = (const u8 *)file_load_fast("PROGDIR:data/glenz_sin.bin", 7200u, &water_size);

    /* TECHNO */
    g_techno_circle0 = (const u8 *)file_load_fast("PROGDIR:data/circle0.bin", 8000u, &water_size);
    g_techno_circle123 = (const u8 *)file_load_fast("PROGDIR:data/circle123.bin", 24000u, &water_size);
    g_techno_sin = (const u8 *)file_load_fast("PROGDIR:data/sin1024.bin", 2048u, &water_size);
    g_techno_troll = (const u8 *)file_load_fast("PROGDIR:data/troll.bin", 128000u, &water_size);
    g_techno_troll_pal = (const u8 *)file_load_fast("PROGDIR:data/troll.pal", 768u, &water_size);

    /* PANIC (Panicfake) */
    g_panic_monster = (const u8 *)file_load_fast("PROGDIR:data/monster.bin", 64000u, &water_size);
    g_panic_pal = (const u8 *)file_load_fast("PROGDIR:data/monster.pal", 768u, &water_size);

    /* U2A (VISU) */
    g_u2a_pal = (const u8 *)file_load_fast("PROGDIR:data/u2a_pal.bin", 768u, &water_size);
    g_u2a_bg = (const u8 *)file_load_fast("PROGDIR:data/u2a_bg.bin", 64000u, &water_size);
    g_u2a_obj = (const u8 *)file_load_fast("PROGDIR:data/u2a_obj.bin", 13016u, &water_size);
    g_u2a_anim = (const u8 *)file_load_fast("PROGDIR:data/u2a_anim.bin", 16081u, &water_size);
    g_u2a_scene = (const u8 *)file_load_fast("PROGDIR:data/u2a_scene.bin", 12u, &water_size);

    load_tempo_cfg();                    /* TakeSystem の前 (DOS I/O) */
    load_glenz_cfg();
    load_music1_cfg();
    load_hidden_cfg();
    load_start_cfg();
    {
        u32 psz = 0;
        /* Windows で作ると拡張子 .txt が付きやすいので、それも受け付ける。
         * exe と同じフォルダに置いた場合も同じ。 */
        static const char *const pn[] = {
            "PROGDIR:data/prof.cfg", "PROGDIR:data/prof.cfg.txt",
            "PROGDIR:prof.cfg", "PROGDIR:prof.cfg.txt" };
        for (int i = 0; i < 4 && !prof_on; i++)
            if (file_load_fast(pn[i], 64u, &psz))
                prof_on = 1;
    }
    {
        /* data/triple.cfg (中身は何でも可) があればトリプルバッファ
         * (2026-11-17 ユーザー指示: 既定はダブル、好みで選べるように)。 */
        u32 tsz = 0;
        static const char *const tn[] = {
            "PROGDIR:data/triple.cfg", "PROGDIR:data/triple.cfg.txt",
            "PROGDIR:triple.cfg", "PROGDIR:triple.cfg.txt" };
        for (int i = 0; i < 4; i++)
            if (file_load_fast(tn[i], 64u, &tsz)) {
                g_triple = 1;
                break;
            }
        /* data/predict.cfg があればトリプルバッファ時に表示 VBL を予測して
         * 動きを合わせる (既定 OFF、2026-11-17 ユーザー指示) */
        static const char *const qn[] = {
            "PROGDIR:data/predict.cfg", "PROGDIR:data/predict.cfg.txt",
            "PROGDIR:predict.cfg", "PROGDIR:predict.cfg.txt" };
        for (int i = 0; i < 4; i++)
            if (file_load_fast(qn[i], 64u, &tsz)) {
                video_set_predict(1);
                break;
            }
    }
    {
        u32 bsz = 0;
        static const char *const bn[] = {
            "PROGDIR:data/bench.cfg", "PROGDIR:data/bench.cfg.txt",
            "PROGDIR:bench.cfg", "PROGDIR:bench.cfg.txt" };
        for (int i = 0; i < 4 && !bench_on; i++)
            if (file_load_fast(bn[i], 64u, &bsz))
                bench_on = 1;
    }
    {
        u32 asz = 0;
        static const char *const an[] = {
            "PROGDIR:data/analyze.cfg", "PROGDIR:data/analyze.cfg.txt" };
        for (int i = 0; i < 2 && !anl_on; i++)
            if (file_load_fast(an[i], 64u, &asz))
                anl_on = 1;
    }

#ifdef TRAP_DEBUG
    trap_install();
#endif
    techno_prepare();
    TakeSystem();

    if (!video_init()) {
        FreeSystem();
        { mem_free_all(); if (wbmsg) { Forbid(); ReplyMsg((struct Message *)wbmsg); return 20; } Exit(20); }
    }
    setup_palette();
    vblank_init();
    input_init();

    if (!audio_init()) {
        FreeSystem();
        { mem_free_all(); if (wbmsg) { Forbid(); ReplyMsg((struct Message *)wbmsg); return 20; } Exit(20); }
    }

    zinit();
    if (!zinitmodule((const void *)music0_data)) {
        FreeSystem();
        { mem_free_all(); if (wbmsg) { Forbid(); ReplyMsg((struct Message *)wbmsg); return 20; } Exit(20); }
    }
    zsoundon();
    zplaysong(0);

    /* c2p 高速版の自己テスト (表示が有効な状態で計測) */
    c2p_test_result = c2p_selftest(&c2p_old_frames, &c2p_new_frames);
    if (bench_on) {
        /* 転送速度テストだけをして終わる (結果は FreeSystem の後に書く)。 */
        bench_run();
        goto shutdown;
    }

    /* シーン切り替え (音楽オーダー同期): テストとして 4 オーダーごとに交互
     * 切り替え時はフェードアウト → シーン init → フェードイン */
    cur_scene = 0;
    u16 scene_group = 0xFFFF;
    u8 fade = 64;                        /* 64 = 通常輝度 */
    int fade_state = 0;                  /* 0: 表示中 / 1: フェードアウト / 2: フェードイン */
    int pending_scene = 0;
    int prev_snapped = 0;

#ifndef TEST_FIXED_SCENE
    /* 通常の先頭 (ALKU) から開始する。MUSIC0 order 0 は main の
     * zplaysong(0) で既に鳴っている。 */
    cur_scene = 0;
    if (start_scene > 0) {
        /* PROGDIR:start.cfg: 指定パートから、対応する音楽位置で始める */
        int sec = start_pos[start_scene].section;
        const void *mod = (sec == 1) ? (const void *)g_music1_data
                                     : (const void *)music0_data;
        if (mod && zinitmodule(mod)) {
            zplaysong(start_pos[start_scene].order);
            music_section = sec;
        }
        cur_scene = start_scene;
        scene_group = (u16)cur_scene;
    }
    pick_buffers(cur_scene);
    scene_list[cur_scene].init();
#endif

#ifdef TEST_FIXED_SCENE
    /* テスト: 指定シーンを固定表示 (切り替えなし) */
    cur_scene = TEST_FIXED_SCENE;
    pick_buffers(cur_scene);
    scene_list[cur_scene].init();
#endif

    dis_version();                       /* DIS 初期化 (パート開始時) */

    for (;;) {
        if (dis_exit())                  /* 終了要求 (左マウスボタン) */
            break;
        /* 集計: ENDSCRL (最後のパート、終わりが無い) に入ったら終わる */
        /* start.cfg でパートを指定したときは、そのパートが終わったら終わる */
        if (anl_on && (loop_restart || cur_scene == 17 ||
                       (start_scene > 0 && anl_first_scene >= 0 &&
                        cur_scene != anl_first_scene)))
            break;
        /* トリプルバッファでは VBL を待たずに次の絵を描き始める (待ちは
         * video_flip が、前の予約が表示されるまでの間だけ行う)。カスタム画面
         * (切り替えをしないパート) は従来どおり 1 VBL に 1 回。 */
        if (video_custom_screen_active())
            dis_waitb();                 /* VBL 待ち + 経過フレーム数 */
        music_frame();

        /* PROGDIR:mus1.cfg: 起動から約 2 秒後に MUSIC1 を先頭から再生 (テスト) */
        if (test_music1 && music_section == 0 && vblank_frames() > 100) {
            test_music1 = 0;
            if (g_music1_data && zinitmodule((const void *)g_music1_data)) {
                zplaysong(0);
                music_section = 1;
            }
        }

        /* 曲の切り替え (オリジナルの MAIN/U2.ASM restartmus と同じ位置)。
         * タイトル表示後は MUSIC1、フィナーレで MUSIC0 order 18 に戻す。 */
        {
            u32 cur_cue = ((u32)music_section << 24) |
                          ((u32)np_ord << 8) | (u32)(np_row & 0xff);
            /* MUSIC1 の開始は BEGLOGO の終了時 (下の image_finished)。
             * 以前は MUSIC0 order 14 で切り替えていたが、原作
             * U2.ASM:817-821 は BEGLOGO.EXE が終わった直後に
             * restartmus(MUSIC1, order 0) する。 */
            if (music_section == 1 && cur_cue >= MUSIC2_SWITCH_CUE) {
                if (zinitmodule((const void *)music0_data))
                    zplaysong(18);
                music_section = 2;
            }
        }

        u16 g = scene_for_order(np_ord, np_row, music_section);   /* 音楽オーダー → オリジナルのパート順 */
        int skip = input_next_pressed(); /* 右マウスボタン: 次のシーンへ (押した瞬間のみ) */
#ifdef TEST_FIXED_SCENE
        /* 固定シーンのテストでも、右クリックで次のシーンへ進める。
         * また TECHNO が終わったら次の実装済みパート (FOREST) へ繋ぐ。 */
        if (fade_state == 0 && skip) {
            pending_scene = (cur_scene + 1) % scene_count;
            fade_state = 1;
        }
#endif
#ifndef TEST_FIXED_SCENE
        /* 右クリック: どのパートでも次へスキップ (デバッグ用)。
         * JPLOGO は専用の音楽再開処理があるので下のブロックに任せる。 */
        if (fade_state == 0 && skip && cur_scene != 14) {
            pending_scene = (cur_scene + 1) % scene_count;
            if (pending_scene == 0)
                loop_restart = 1;
            scene_group = (u16)pending_scene;
            fade_state = 1;
        }
#endif
        if (fade_state == 0 && cur_scene == 6 && techno_finished()) {
            /* TECHNO の次は PANIC (Panicfake)。 */
            pending_scene = 7;
            fade_state = 1;
        }
        if (fade_state == 0 && cur_scene == 7 && panic_finished()) {
            /* PANIC の次は FOREST。U2.ASM:830-849: PANICEND のあと
             * musplus が負になるまで待ち (@@zh5)、次に正になるまで
             * 待ってから (@@zh4) MNTSCRL を始める。 */
            int mp = dis_musplus();
            if (panic_gate == 0 && mp < 0)
                panic_gate = 1;
            /* 描画が遅い実機では PANIC の終わりが遅れ、マーカー直後の窓
             * (musplus 1..31、std 35:01..35:31) を丸ごと見逃して次の
             * マーカーまで待っていた。そのせいで MNTSCRL の開始が大きく
             * 遅れ、始まるとすぐ終了の位置に来て終わっていた
             * (2026-11-15 ユーザー報告)。35:01 を過ぎていれば進む。 */
            int pos = ((int)np_ord << 8) | (np_row & 0xff);
            if ((panic_gate == 1 && mp > 0) || pos >= ((35 << 8) | 1)) {
                panic_gate = 0;
                prof_mark("MNTSCRL-start");
                pending_scene = 8;
                fade_state = 1;
            }
        }
        if (fade_state == 0 && cur_scene == 8 && forest_finished()) {
            /* MNTSCRL は Forest.cpp:154-212 の終了フェードが終わってから
             * LNS&ZOOM へ進む (音楽キューで途中終了させない)。 */
            pending_scene = 9;
            fade_state = 1;
        }
        if (fade_state == 0 && cur_scene == 10 && plzcub_finished()) {
            /* PLZPART (プラズマ+キューブ) のスプラインが終わったら DOTS へ。 */
            pending_scene = 11;
            fade_state = 1;
        }
        if (fade_state == 0 && cur_scene == 1 && u2a_finished()) {
            /* 原作 U2A.C はスクリプト終端 (0xFF 0xFF) で xit=1。
             * 次の PAM へは音楽キューではなくパート終了で移る。 */
            pending_scene = 2;
            fade_state = 1;
        }
        if (fade_state == 0 && cur_scene == 5 && tunnel_finished()) {
            /* 原作 TUN10.PAS:3,221: veke=1060 フレームで終了。
             * 次の TECHNO へは音楽キューではなくパート終了で移る。 */
            pending_scene = 6;
            fade_state = 1;
        }
        if (fade_state == 0 && cur_scene == 2 && pam_finished()) {
            /* PAM の FLI は 386 ではデコードが追いつかず、実測で
             * 45 フレームに約 4.5 秒かかる。音楽キューではなく
             * パートの終了で BEGLOGO へ切り替える。 */
            pending_scene = 3;
            fade_state = 1;
        }
        if (fade_state == 0 && cur_scene == 18 && u2e_finished()) {
            /* U2E (Vector Part II) -> ENDLOGO */
            pending_scene = 16;
            fade_state = 1;
        }
        if (fade_state == 0 && cur_scene == 16 && end_finished()) {
            /* ENDLOGO の次は CREDITS (原本の並び) */
            pending_scene = 19;
            fade_state = 1;
        }
        if (fade_state == 0 && cur_scene == 19 && credits_finished()) {
            /* CREDITS の次は ENDSCRL */
            pending_scene = 17;
            fade_state = 1;
        }
        if (fade_state == 0 && cur_scene == 15 && ddstars_finished()) {
            /* hidden part DDSTARS -> ENDLOGO */
            pending_scene = 16;
            fade_state = 1;
        }
        /* ---- 原作 U2.ASM:765-935 のパート順。各パートは自分の終了条件で
         * 終わり、次のパートへ進む (音楽位置による強制切り替えはしない)。 */
        if (fade_state == 0 && cur_scene == 0 && alku_finished()) {
            pending_scene = 1;          /* ALKU -> U2A */
            fade_state = 1;
        }
        if (fade_state == 0 && cur_scene == 3 && image_finished()) {
            /* BEGLOGO -> GLENZ。U2.ASM:817-821: BEGLOGO.EXE の直後に
             * restartmus で MUSIC1 を order 0 から鳴らす。 */
            if (music_section == 0) {
                if (g_music1_data &&
                    zinitmodule((const void *)g_music1_data))
                    zplaysong(0);
                music_section = 1;
            }
            pending_scene = 4;
            fade_state = 1;
        }
        if (fade_state == 0 && cur_scene == 4 && glenz_finished()) {
            pending_scene = 5;          /* GLENZ -> TUNNELI */
            fade_state = 1;
        }
        if (fade_state == 0 && cur_scene == 9 && lens_finished()) {
            pending_scene = 10;         /* LNS&ZOOM -> PLZPART */
            fade_state = 1;
        }
        if (fade_state == 0 && cur_scene == 12 && water_finished()) {
            pending_scene = 13;         /* RAYSCRL -> 3DSINFLD */
            fade_state = 1;
        }
        if (fade_state == 0 && cur_scene == 13 && sinus_finished()) {
            pending_scene = 14;         /* 3DSINFLD -> JPLOGO */
            fade_state = 1;
        }
        if (fade_state == 0 && cur_scene == 11 && dots_finished()) {
            /* MINVBALL (DOTS) は 2450 フレームで終わり。次は WATER。 */
            pending_scene = 12;
            fade_state = 1;
        }
#ifndef TEST_FIXED_SCENE
        if (fade_state == 0 && cur_scene == 14 && (jplogo_finished() || skip)) {
            /* JPLOGO の次は U2E (未実装のため暫定で DDSTARS=15)。
             * オリジナル U2.ASM と同じく MUSIC0 order 18 から再開する。 */
            pending_scene = play_hidden ? 15 : 18;
            scene_group = (u16)pending_scene;
            fade_state = 1;
            if (music_section == 1) {
                if (zinitmodule((const void *)music0_data))
                    zplaysong(18);
                music_section = 2;
            }
        }
#endif
#ifndef TEST_FIXED_SCENE
        /* TECHNO/PANIC は「終了」で次へ繋ぐので、音楽キューの切り替えは
         * この 2 パートの間は止める。 */
        /* 音楽キューが現在のシーンより先へ進んだ時だけ切り替える。
         * 右クリックで先へスキップした後、音楽が追いつくまで
         * 前のシーンへ戻らない (ループ防止)。 */
        /* 音楽キュー表による強制切り替えは廃止 (原作はパートごとの
         * 終了条件で進む)。scene_cues はもう使わない。 */
        if (0 && fade_state == 0 && !scene_specific_end(cur_scene) &&
            g > cur_scene &&
            (g != scene_group || skip)) {
            /* 音楽オーダーが進んだ場合はそのグループのシーンへ、
             * 右クリックの場合は現在のシーンの次へ */
            if (g != scene_group) {
                pending_scene = (int)(g % (u16)scene_count);
                if (pending_scene == 0 && cur_scene != 0)
                    loop_restart = 1;   /* 曲を先頭から鳴らし直す */
            } else {
                pending_scene = (cur_scene + 1) % scene_count;
            }
            scene_group = g;
            fade_state = 1;
        }
#endif
        /* フェードアウトを始めた時点の絵を控える (PANICEND が消灯する対象)。
         * 切替完了時だと、直前のパートが既に黒を描いた後になってしまう。 */
        if (fade_state == 1 && !prev_snapped) {
            u8 *ch = video_chunky();
            const u8 *cp = video_get_palette();
            u8 *pb = (u8 *)prev_frame_buf;
            for (u32 i = 0; i < (u32)SCREEN_W * SCREEN_H; i++)
                pb[i] = ch[i];
            for (u32 i = 0; i < 768; i++)
                prev_pal_buf[i] = cp[i];
            g_prev_frame = (const u8 *)prev_frame_buf;
            g_prev_pal = prev_pal_buf;
            prev_snapped = 1;
        }

        if (fade_state == 1) {
            /* 原作にシーン間のフェードは無い (パートはハードカット)。
             * 切替は即座に行い、フェードインもしない。 */
            /* ALKU -> U2A は ALKU の画面を残したまま U2A の最初のフレームを
             * 裏画面に描き、パレットと表示を同時に切り替える
             * (原作 U2A.C:226-247 は vid_init まで前の画面が残る)。 */
            /* BEGLOGO -> GLENZ も同じ: 原作 GLENZ はタイトル画面をそのまま
             * zoomer2 で閉じていく (GLENZ/ZOOMER.C)。画面を消すとタイトルの
             * フェードイン直後に黒が一瞬見えた (2026-11-14 ユーザー指摘)。 */
            /* JPLOGO -> U2E は前の画面を残したままホワイトアウトする。
             * U2E -> ENDLOGO も白フラッシュが先に効くよう前画面を残し、
             * ENDLOGO は非表示の裏面へ描画してから切り替える。 */
            int seamless = (cur_scene == 0 && pending_scene == 1) ||
                           (cur_scene == 3 && pending_scene == 4) ||
                           (cur_scene == 14 && pending_scene == 18) ||
                           (cur_scene == 18 && pending_scene == 16);
            if (!seamless) {
                video_set_custom_screen(0, 0);  /* ハードウェアスクロール解除 */
                video_clear_planes();
            }
            video_set_display_start(0);     /* PLZPART の縦シフト解除 */
            dis_setcopper(3, 0);            /* PLZPART の VBL コールバック解除 */
            /* chunky も消す: 前のパートが塗らなかった領域に
             * 残像 (前パートの絵) が出るのを防ぐ */
            {
                u8 *ch = video_chunky();
                for (u32 i = 0; i < (u32)SCREEN_W * SCREEN_H; i++)
                    ch[i] = 0;
            }
            pick_buffers(pending_scene);
            scene_list[pending_scene].init();
            cur_scene = pending_scene;
            if (seamless) {
                /* 先にカスタム画面をやめる (表示は次の flip まで前の画面の
                 * まま)。カスタム画面中に書いたパレットは表示中のリストへ
                 * すぐ入り、前の画面の色が変わって点滅した (ALKU->U2A、
                 * 2026-11-17 ユーザー指摘)。 */
                video_leave_custom_deferred();
                u8 *bp = video_back_planes();
                if (!scene_list[cur_scene].render(video_chunky(), bp,
                                                  SCREEN_W, SCREEN_H))
                    c2p8_fast(video_chunky(), bp, SCREEN_W, SCREEN_H);
                if (cur_scene == 1)
                    u2a_apply_palette();    /* GLENZ は render 内で設定 */
                video_flip();
                fade = 64;
                fade_state = 0;
                prev_snapped = 0;
                continue;
            }
            prev_snapped = 0;
            if (loop_restart) {
                /* ループ: MUSIC0 を先頭から */
                loop_restart = 0;
                scene_group = 0xFFFF;
                if (zinitmodule((const void *)music0_data)) {
                    zplaysong(0);
                    music_section = 0;
                }
            }
            fade = 64;
            fade_state = 0;
        }

        /* TUNNELI (5) / TECHNO (6) / MINVBALL (11) は 1 ドットずつ
         * 8 プレーンを読み書きするので、ファスト RAM の写しに描いてから
         * まとめてチップ RAM へ写す (実機 68060 の速度対策)。 */
        int use_shadow = (cur_scene == 5 || cur_scene == 6 || cur_scene == 11);
        u8 *planes = use_shadow ? video_back_shadow() : video_back_planes();
        u32 pt0 = prof_on ? prof_lines() : 0;
        int rendered = scene_list[cur_scene].render(video_chunky(), planes, SCREEN_W, SCREEN_H);
        u32 pt1 = prof_on ? prof_lines() : 0;
        if (anl_on)
            anl_frame(cur_scene, rendered ? 0 : video_chunky(), planes);
        if (!rendered)
            c2p_scene(video_chunky(), planes, cur_scene);
        else
            lb_mark_all_dirty();    /* パートが直接描いた (帯の中身は不明) */
        if (use_shadow)
            video_commit_back_shadow();
        u32 pt2 = prof_on ? prof_lines() : 0;
        /* デバッグ用バーは現在非表示 (描画もしない)。
         * 表示を戻すときは下の 2 行を有効にする。
         *   if (!video_custom_screen_active())
         *       draw_debug(planes);
         */
        (void)draw_debug;
        if (fade_state != 0)
            video_apply_fade(fade);
        video_flip();
        /* 終わったパートのチップ RAM の画面バッファを返す。表示が標準画面に
         * 切り替わってから (2 フレーム後) にする。 */
        {
            static int last_scene = -1, rel_scene = -1, rel_wait;
            if (last_scene != cur_scene) {
                if (last_scene >= 0) {
                    rel_scene = last_scene;
                    rel_wait = VIDEO_NBUF;
                }
                last_scene = cur_scene;
            } else if (rel_scene >= 0 && --rel_wait <= 0) {
                if (rel_scene == 0) alku_release();
                if (rel_scene == 8) forest_release();
                if (rel_scene == 12) water_release();
                if (rel_scene == 6) techno_release();
                if (rel_scene == 19) credits_release();
                rel_scene = -1;
            }
        }
        if (prof_on) {
            static u32 prof_prev;
            u32 pt3 = prof_lines();
            if (prof_prev)
                prof_add(cur_scene, pt1 - pt0, pt2 - pt1, pt3 - prof_prev);
            prof_prev = pt3;
        }
    }

shutdown:
    /* 終了した時点の音楽位置 (左クリックで止めたとき、どこにいたかを見る) */
    prof_mark("EXIT");
    zstopsong();
    audio_shutdown();
    vblank_shutdown();
    FreeSystem();
    if (prof_on)
        prof_write();
    if (anl_on) {
        anl_finish();
        anl_write();
    }
    if (bench_on)
        bench_write();
    /* 画面とオーディオを OS に返した後で、確保したメモリをすべて返す */
    mem_free_all();
    if (DOSBase)
        CloseLibrary((struct Library *)DOSBase);
    CloseLibrary((struct Library *)GfxBase);
    if (wbmsg) {
        Forbid();
        ReplyMsg((struct Message *)wbmsg);
        return 0;
    }
    Exit(0);
    return 0;
}
