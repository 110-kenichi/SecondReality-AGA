#include "video.h"
#include "mem.h"
#include "vblank.h"
#include <proto/exec.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>

extern volatile struct Custom *custom;

#define COPPER_WORDS 1200

static u8 *screen[VIDEO_NBUF];          /* 各 8 プレーン連続 (p * PLANE_BYTES) */
/* screen[] のファスト RAM 側の写し。1 ドットずつ 8 プレーンを読み書きする
 * パートはこちらに描き、video_commit_back_shadow() で 32bit 単位で
 * チップ RAM へ写す (実機 68060 の速度対策、2026-11-15)。 */
static u8 *shadow[VIDEO_NBUF];
static u16 *copper[VIDEO_NBUF];
static u16 *palhi[VIDEO_NBUF][8];       /* 各コッパーリストのバンク別・上位 12bit 先頭 */
static u16 *pallo[VIDEO_NBUF][8];       /* 各コッパーリストのバンク別・下位 12bit 先頭 */
static u8 *chunky;
static int front, back;
/* トリプルバッファ (2026-11-17 ユーザー指示)。front = 表示中、pend = 次の VBL で
 * 表示される予定 (-1 = 無し)、back = 描画先。描き終わったらすぐ次の画面に
 * 描き始められるので、処理が 1 VBL を少し超えても 2 VBL 待たない。 */
static int pend = -1;
static u32 pend_vbl;
/* パレット: 描画先の Copper リストにだけ書き、表示はその絵と同時に変わる。
 * 他のリストは、描画先になったときに最新の値へ合わせる (pal_ver)。 */
static u8 pal_out[256 * 3];    /* 最後に書いたパレット (フェード込み) */
static u32 pal_ver, slot_pal_ver[VIDEO_NBUF];
extern u32 vblank_frames(void);
/* 予約中の画面があれば、それが表示されるまで待つ (カスタム画面に入るとき、
 * 表示中の Copper リストを確定させるため)。 */
static void settle_pending(void) {
    if (pend >= 0) {
        while (vblank_frames() == pend_vbl)
            ;
        front = pend;
        pend = -1;
    }
}
static u8 cur_pal[256 * 3];    /* 現在のパレット (フェード用) */
/* カスタム画面を抜けたあと、通常画面のポインタへ戻す必要があるリスト。
 * 表示中のリストをすぐ書き換えると、新しい絵が出る前に古い通常画面
 * (前のパートの残り) が 1 フレーム見える (ALKU->U2A の点滅、2026-11-17
 * ユーザー指摘)。描画先になったとき (video_flip) に戻す。 */
static u8 restore_needed[VIDEO_NBUF];

/* コッパーリスト内の BPLCON1 / BPLxMOD / BPLxPT / DDFSTOP データ位置 */
static u16 *bplcon1_slot[VIDEO_NBUF];
static u16 *bplmod_slot[VIDEO_NBUF][2];
static u16 *bplpt_slot[VIDEO_NBUF][8][2];
static u16 *ddfstop_slot[VIDEO_NBUF];
static u16 *fmode_slot[VIDEO_NBUF];
static u16 *ddfstrt_slot[VIDEO_NBUF];
static u16 *bplcon0_slot[VIDEO_NBUF];
static u16 *diwstrt_slot[VIDEO_NBUF];   /* DIWSTRT value word (vertical display start) */
static u16 *diwstop_slot[VIDEO_NBUF];   /* DIWSTOP value word */
static int win200;             /* 1 = 表示窓を 28..227 行の 200 行にする */

/* 画面分割 (CRED 用、2026-11-16)。表示の 128 行目で上の画面から下の画面へ
 * 切り替える Copper の命令列。各コッパーリストの最後に置き、使わないときは
 * 先頭を終了命令 (FFFF,FFFE) にしておく。
 *   WAIT 表示 127 行目の右端 -> BPLCON0 = 0 プレーン (128, 129 行目は
 *   ビットプレーン DMA 無し = 色 0、ポインタが進まない) -> 8 プレーンの
 *   ポインタ・モジュロ・BPLCON1・FMODE・DDFSTRT/STOP を下の画面用に書く ->
 *   WAIT 129 行目の右端 -> BPLCON0 = 8 プレーン (130 行目から下の画面)。
 * 切り替えの 2 行は黒 (色 0) になるので、そこが黒い画面でだけ使える。 */
#define SPLIT_ROW 128
static u16 *split_slot[VIDEO_NBUF];     /* 分割ブロックの先頭 (WAIT の 1 語目) */
static u16 *split_pt[VIDEO_NBUF][8][2];
static u16 *split_mod[VIDEO_NBUF][2], *split_con1[VIDEO_NBUF], *split_fmode[VIDEO_NBUF], *split_ddf[VIDEO_NBUF][2];

/* PLZPART の下方向スライド用: 0 の 1 走査線。BPLxMOD=-40 で毎行同じ行を
 * 読ませ、スライド終端の残り 45 行を黒にする。 */
static u8 *zero_row;
static int display_black;

/* カスタムビットプレーン表示 (ハードウェア横スクロール) */
static u8 *custom_screen;      /* 0 = 通常のダブルバッファ表示 */
static int custom_stride;      /* 1 行のバイト数 */
static int custom_rows;        /* プレーン 1 枚の行数 (縦スクロール用に 256 超も可) */
static int custom_scroll_x;    /* 現在の横スクロール位置 (ピクセル) */
static int custom_scroll_y;    /* 現在の縦スクロール位置 (行) */
static int custom_slot;        /* 使用中のコッパーリスト */
static int custom_hires;       /* 1 = 640x256 hires 4 プレーン (ENDSCRL) */

/* コッパーリスト slot のビットプレーン設定 (ポインタ + モジュロ + ファインスクロール)
 * scroll_x: 表示開始ピクセル。コース = (x>>4)*2 バイト、ファイン = x&15
 * scroll_y: 表示開始行 (行単位のオフセット)
 * ファイン分だけ表示データが後ろへずれるため、DDFSTOP を 1 ワード広げて
 * 余分なワードをフェッチする (右端の欠け防止)。モジュロもそれに合わせる。 */
static void set_bitplane_slot(int slot, u8 *planes, int stride_bytes, int rows,
                              int scroll_x, int scroll_y) {
    int plane_size = stride_bytes * rows;
    /* HRM: BPLCON1 の delay は「画像を右へずらす」。
     * 左スクロールにするため fine は減算方向にし、コースで 16px 単位に補正する。
     *   実効オフセット = coarse*16 - fine = scroll_x                       */
    int coarse_bytes = ((scroll_x + 15) >> 4) * 2 + scroll_y * stride_bytes;
    int fine = (16 - (scroll_x & 15)) & 15;
    int hires = (planes == custom_screen) && custom_hires;
    int wide_fetch = !hires && (stride_bytes != SCREEN_W / 8);
    int fetch_bytes = hires ? 80 : (wide_fetch ? 42 : 40);

    for (int p = 0; p < SCREEN_PLANES; p++) {
        u32 addr = (u32)(planes + p * plane_size + coarse_bytes);
        *bplpt_slot[slot][p][0] = (u16)(addr >> 16);
        *bplpt_slot[slot][p][1] = (u16)(addr & 0xFFFF);
    }
    /* ワールド (画面より広い) を横スクロールする場合は、BPLCON1 の
     * delay 分だけ右端が欠けるため DDFSTOP を 1 ワード広げて余分な
     * ワードをフェッチする。DIW の右端でクリップされるので表示幅は
     * 320px のまま。 */
    *ddfstop_slot[slot] = wide_fetch ? 0x00D8 : 0x00D0;
    /* HRM: hires 640px の標準 DDF は 0x3C..0xD4、BPLCON0 = HIRES|4 planes|COLOR */
    *ddfstrt_slot[slot] = hires ? 0x003C : 0x0038;
    if (hires)
        *ddfstop_slot[slot] = 0x00D4;
    /* AGA の 64bit フェッチ (FMODE=3)。8 プレーン lores を 16bit フェッチ
     * (FMODE=0) で出すと、表示中のチップバスのほとんどをビットプレーン
     * DMA が使い、CPU のチップ RAM への書き込みが極端に遅くなる (実機
     * 68060 の測定で c2p 1 回に約 700 走査線 = 2.2 フレーム、2026-11-15)。
     * FMODE=3 なら同じ表示を 1/4 のバス時間で取れる。条件はポインタと
     * 1 行のフェッチが 8 バイト単位で、細かい横スクロールが無いこと。
     * 320px lores: DDFSTOP = 0x38 + (320/64 - 1) * 32 = 0xB8。 */
    {
        int fm3 = !hires && !wide_fetch && fine == 0 &&
                  ((coarse_bytes & 7) == 0) && (((u32)planes & 7) == 0) &&
                  ((plane_size & 7) == 0);
        *fmode_slot[slot] = fm3 ? 0x0003 : 0x0000;
        if (fm3)
            *ddfstop_slot[slot] = 0x00B8;
    }
    *bplcon0_slot[slot] = hires ? 0xC200 : 0x0210;
    *bplmod_slot[slot][0] = (u16)(stride_bytes - fetch_bytes);
    *bplmod_slot[slot][1] = (u16)(stride_bytes - fetch_bytes);
    *bplcon1_slot[slot] = (u16)(fine | (fine << 4));   /* PF1H/PF2H 同値 */
}

/* コッパーリスト構築 (320x256 PAL / 8 プレーン / AGA 256 色) */
static void build_copper(u16 *c, u8 *planes, int slot) {
    *c++ = 0x0100; bplcon0_slot[slot] = c; *c++ = 0x0210;   /* BPLCON0: 8 planes (bit4=BPU3) + color */
    *c++ = 0x0102; bplcon1_slot[slot] = c; *c++ = 0x0000;   /* BPLCON1 */
    *c++ = 0x0104; *c++ = 0x0000;   /* BPLCON2 */
    *c++ = 0x0108; bplmod_slot[slot][0] = c; *c++ = 0x0000; /* BPL1MOD */
    *c++ = 0x010A; bplmod_slot[slot][1] = c; *c++ = 0x0000; /* BPL2MOD */
    *c++ = 0x008E; diwstrt_slot[slot] = c; *c++ = 0x2C81;   /* DIWSTRT (line 44 = PAL 標準) */
    *c++ = 0x0090; diwstop_slot[slot] = c; *c++ = 0x2CC1;   /* DIWSTOP (line 300 = 44+256) */
    *c++ = 0x0092; ddfstrt_slot[slot] = c; *c++ = 0x0038;   /* DDFSTRT */
    *c++ = 0x0094; ddfstop_slot[slot] = c; *c++ = 0x00B8;   /* DDFSTOP (FMODE=3) */
    *c++ = 0x01FC; fmode_slot[slot] = c; *c++ = 0x0003;     /* FMODE: 64bit フェッチ */

    for (int p = 0; p < SCREEN_PLANES; p++) {
        u32 addr = (u32)(planes + p * PLANE_BYTES);
        *c++ = (u16)(0x00E0 + p * 4); bplpt_slot[slot][p][0] = c; *c++ = (u16)(addr >> 16);
        *c++ = (u16)(0x00E2 + p * 4); bplpt_slot[slot][p][1] = c; *c++ = (u16)(addr & 0xFFFF);
    }

    /*
     * AGA 256 色パレット: 8 バンク × 32 色。
     * BPLCON3 (0x106): BANK = bits 15-13, LOCT = bit 9。
     * バンクを選び、0x180-0x1BE に上位 12bit → 下位 12bit の順に書く。
     * 注意: コッパーの MOVE 命令はレジスタ番地が 9bit にマスクされるため、
     *       0x200 以降の命令は 0x000-0x1FE にミラーされ COP1LC 等を破壊する。
     *       ここでは 0x106 (BPLCON3) と 0x180-0x1BE のみを使うこと。
     */
    for (int b = 0; b < 8; b++) {
        *c++ = 0x0106; *c++ = (u16)(b << 13);   /* BPLCON3: BANK=b, LOCT=0 (上位) */
        palhi[slot][b] = c + 1;
        for (int i = 0; i < 32; i++) {
            *c++ = (u16)(0x0180 + i * 2);      /* COLORxx */
            *c++ = 0x0000;
        }
        *c++ = 0x0106; *c++ = (u16)((b << 13) | 0x0200); /* BPLCON3: BANK=b, LOCT=1 (下位) */
        pallo[slot][b] = c + 1;
        for (int i = 0; i < 32; i++) {
            *c++ = (u16)(0x0180 + i * 2);      /* COLORxx */
            *c++ = 0x0000;
        }
    }
    *c++ = 0x0106; *c++ = 0x0000;       /* BPLCON3 復帰 */

    /* 画面分割ブロック (video_set_split)。最初は無効 (終了命令)。 */
    split_slot[slot] = c;
    *c++ = 0xFFFF; *c++ = 0xFFFE;       /* 有効時は WAIT (0x2C+127 行, H=0xE0) */
    *c++ = 0x0100; *c++ = 0x0200;       /* BPLCON0: 0 プレーン */
    for (int p = 0; p < SCREEN_PLANES; p++) {
        *c++ = (u16)(0x00E0 + p * 4); split_pt[slot][p][0] = c; *c++ = 0;
        *c++ = (u16)(0x00E2 + p * 4); split_pt[slot][p][1] = c; *c++ = 0;
    }
    *c++ = 0x0108; split_mod[slot][0] = c; *c++ = 0;
    *c++ = 0x010A; split_mod[slot][1] = c; *c++ = 0;
    *c++ = 0x0102; split_con1[slot] = c; *c++ = 0;
    *c++ = 0x01FC; split_fmode[slot] = c; *c++ = 0x0003;
    *c++ = 0x0092; split_ddf[slot][0] = c; *c++ = 0x0038;
    *c++ = 0x0094; split_ddf[slot][1] = c; *c++ = 0x00B8;
    *c++ = (u16)(((0x2C + SPLIT_ROW + 1) << 8) | 0x00E1); *c++ = 0xFFFE;  /* WAIT 129 行目の右端 */
    *c++ = 0x0100; *c++ = 0x0210;       /* BPLCON0: 8 プレーン */
    *c++ = 0xFFFF; *c++ = 0xFFFE;       /* end */
}

/* 表示の 130 行目から下に、planes (8 プレーン連続、1 行 40 バイト、
 * プレーン 1 枚 plane_rows 行) の first_row 行目以降を出す。FMODE=3、
 * 横スクロール無し。planes = 0 で分割をやめる。両方のコッパーリストに書く。 */
static void split_off(int slot) {
    split_slot[slot][0] = 0xFFFF;
    split_slot[slot][1] = 0xFFFE;
}

void video_set_split(u8 *planes, int plane_rows, int first_row) {
    for (int slot = 0; slot < VIDEO_NBUF; slot++) {
        if (!planes) {
            split_slot[slot][0] = 0xFFFF;
            split_slot[slot][1] = 0xFFFE;
            continue;
        }
        for (int p = 0; p < SCREEN_PLANES; p++) {
            /* 128 行目 (DMA 無し) に書いた値は 128, 129 行目で進まないので、
             * そのまま 130 行目の読み出し開始になる。 */
            u32 addr = (u32)(planes + p * 40 * plane_rows + first_row * 40);
            *split_pt[slot][p][0] = (u16)(addr >> 16);
            *split_pt[slot][p][1] = (u16)(addr & 0xFFFF);
        }
        *split_mod[slot][0] = 0;
        *split_mod[slot][1] = 0;
        *split_con1[slot] = 0;
        *split_fmode[slot] = 0x0003;
        *split_ddf[slot][0] = 0x0038;
        *split_ddf[slot][1] = 0x00B8;
        split_slot[slot][1] = 0xFFFE;
        split_slot[slot][0] = (u16)(((0x2C + SPLIT_ROW - 1) << 8) | 0x00E1);
    }
}

int video_init(void) {
    front = 0; back = 1; pend = -1;

    /* AROS 等が設定したプログラマブルビデオモードを解除して標準 PAL に戻す。
     * BEAMCON0 の PAL ビット (0x0020) を落として 0 を書くと NTSC (約 60Hz)
     * になり、VBL 周期を 50Hz 前提にしている音楽・タイミングが 1.2 倍速く
     * なる。さらにオーディオのリングバッファが DMA より速く進んで内容が
     * 追い越され、「連打のように速く鳴る」症状が出る。 */
    *(volatile u16 *)((u8 *)custom + 0x1DC) = 0x0020; /* BEAMCON0: PAL */
    *(volatile u16 *)((u8 *)custom + 0x1FC) = 0; /* FMODE */
    custom->bplcon3 = 0;                          /* BPLCON3 リセット (BANK=0, LOCT=0) */

    for (int i = 0; i < VIDEO_NBUF; i++) {
        screen[i] = (u8 *)mem_alloc(SCREEN_PLANES * PLANE_BYTES, MEMF_CHIP | MEMF_CLEAR);
        copper[i] = (u16 *)mem_alloc(COPPER_WORDS * 2, MEMF_CHIP | MEMF_CLEAR);
        if (!screen[i] || !copper[i])
            return 0;
    }
    zero_row = (u8 *)mem_alloc(64, MEMF_CHIP | MEMF_CLEAR);
    /* chunky バッファは CPU 専用 → Fast RAM を優先 (無ければ任意のメモリ) */
    chunky = (u8 *)mem_alloc(SCREEN_W * SCREEN_H, MEMF_FAST | MEMF_CLEAR);
    if (!chunky)
        chunky = (u8 *)mem_alloc(SCREEN_W * SCREEN_H, MEMF_ANY | MEMF_CLEAR);
    if (!zero_row || !chunky)
        return 0;

    for (int i = 0; i < VIDEO_NBUF; i++)
        build_copper(copper[i], screen[i], i);

    custom->cop1lc = (u32)copper[front];
    custom->copjmp1 = 0x7fff;
    custom->dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_COPPER | DMAF_RASTER | DMAF_BLITTER;
    return 1;
}

void video_shutdown(void) {
    /* DMA/画面の復元は FreeSystem 側で行う */
}

u8 *video_chunky(void) { return chunky; }
u8 *video_back_planes(void) { return screen[back]; }
u8 *video_front_planes(void) { return screen[front]; }
u8 *video_planes(int i) { return screen[i]; }
int video_back_index(void) { return back; }

/* 両画面の 8 プレーンを 0 クリア (シーン切り替え用) */
void video_clear_planes(void) {
    for (int k = 0; k < VIDEO_NBUF; k++) {
        u32 *a = (u32 *)screen[k];
        for (int i = 0; i < SCREEN_PLANES * PLANE_BYTES / 4; i++)
            a[i] = 0;
    }
    for (int k = 0; k < VIDEO_NBUF; k++)
        if (shadow[k]) {
            u32 *c = (u32 *)shadow[k];
            for (int i = 0; i < SCREEN_PLANES * PLANE_BYTES / 4; i++)
                c[i] = 0;
        }
}

/* 裏画面のファスト RAM の写し (無ければチップ RAM の裏画面そのもの)。 */
u8 *video_back_shadow(void) {
    if (!shadow[back]) {
        shadow[back] = (u8 *)mem_alloc(SCREEN_PLANES * PLANE_BYTES,
                                      MEMF_FAST | MEMF_CLEAR);
        if (!shadow[back])
            return screen[back];
        /* 今のチップ RAM の内容から始める */
        const u32 *s = (const u32 *)screen[back];
        u32 *d = (u32 *)shadow[back];
        for (int i = 0; i < SCREEN_PLANES * PLANE_BYTES / 4; i++)
            d[i] = s[i];
    }
    return shadow[back];
}

/* 次の video_commit_back_shadow() で写す範囲 (プレーン 0..n-1、行 y0..)。
 * 4 プレーンしか使わない場面 (TECHNO の干渉円など) で転送量を減らす。
 * 写した後は全体 (8 プレーン x 256 行) に戻る。 */
static int commit_np = SCREEN_PLANES, commit_y0 = 0, commit_rows = 256;
void video_set_commit_area(int nplanes, int y0, int rows) {
    commit_np = nplanes;
    commit_y0 = y0;
    commit_rows = rows;
}

/* 裏画面の写しをチップ RAM へ 32bit 単位で写す。 */
void video_commit_back_shadow(void) {
    if (!shadow[back])
        return;
    for (int p = 0; p < commit_np; p++) {
        const u32 *s = (const u32 *)(shadow[back] + p * PLANE_BYTES + commit_y0 * 40);
        u32 *d = (u32 *)(screen[back] + p * PLANE_BYTES + commit_y0 * 40);
        for (int i = 0; i < commit_rows * 40 / 4; i++)
            d[i] = s[i];
    }
    commit_np = SCREEN_PLANES;
    commit_y0 = 0;
    commit_rows = 256;
}

/* 標準画面のプレーン先頭を通常画像 / 黒 (zero_row) に切り替える。 */
static void set_standard_pointers(int slot, int black) {
    for (int p = 0; p < SCREEN_PLANES; p++) {
        u32 addr = black ? (u32)zero_row
                         : (u32)(screen[slot] + p * PLANE_BYTES);
        *bplpt_slot[slot][p][0] = (u16)(addr >> 16);
        *bplpt_slot[slot][p][1] = (u16)(addr & 0xFFFF);
    }
    /* 黒の 1 行を毎走査線で読み直す (BPLxMOD = -1 行) */
    *bplmod_slot[slot][0] = (u16)(black ? -40 : 0);
    *bplmod_slot[slot][1] = (u16)(black ? -40 : 0);
}

/* 表示窓を 200 行 (画面の 28..227 行) にする / 256 行に戻す。
 * 2026-11-16 ユーザー提案: 縦 200 ドットのパートは、上下 28 行の黒帯も
 * ビットプレーン DMA で読んでいる。表示窓 (DIWSTRT/DIWSTOP の縦) を
 * 200 行にすると、その 56 行は DMA が止まりチップバスが空く。
 * 表示位置は変えない: 窓の開始を 0x2C+28 行目、終わりを 0x2C+228 行目
 * (DIWSTOP の縦は 8 ビットで、0x80 未満なら 256 を足した行。0x110 -> 0x10)
 * にし、プレーンの先頭も 28 行分進める。標準のダブルバッファ画面だけが
 * 対象 (カスタム画面・PLZPART の縦シフトとは同時に使わない)。 */
void video_set_window200(int on) {
    win200 = on ? 1 : 0;
    for (int slot = 0; slot < VIDEO_NBUF; slot++) {
        *diwstrt_slot[slot] = win200 ? 0x4881 : 0x2C81;
        *diwstop_slot[slot] = win200 ? 0x10C1 : 0x2CC1;
        for (int p = 0; p < SCREEN_PLANES; p++) {
            u32 addr = (u32)(screen[slot] + p * PLANE_BYTES + (win200 ? 28 * 40 : 0));
            *bplpt_slot[slot][p][0] = (u16)(addr >> 16);
            *bplpt_slot[slot][p][1] = (u16)(addr & 0xFFFF);
        }
    }
}

/* PLZPART: 表示開始走査線を y 行下げる (ハードウェア縦シフト)。
 * 0 で通常。y が 212 以上になると DIWSTRT の 8bit 範囲を超えるので、
 * 表示窓の残り 45 行は zero_row で黒にする。 */
void video_set_display_start(int y) {
    if (y < 0)
        y = 0;
    if (y > SCREEN_H)
        y = SCREEN_H;
    int v = 0x2C + y;
    int black = 0;
    if (v > 0xFF) {
        v = 0xFF;
        black = 1;
    }
    u16 strt = (u16)((v << 8) | 0x0081);
    for (int k = 0; k < VIDEO_NBUF; k++)
        *diwstrt_slot[k] = strt;
    if (black != display_black) {
        display_black = black;
        for (int k = 0; k < VIDEO_NBUF; k++)
            set_standard_pointers(k, black);
    }
}

/* デバッグ用: 背景色 (COLOR00 の上位 12bit) を両コッパーリストへ設定 */
void video_set_bg(u16 color) {
    for (int k = 0; k < VIDEO_NBUF; k++)
        palhi[k][0][0] = color;
}

static void write_palette_slot(int s, const u8 *rgb);

/* 描き終わった back を次の VBL で表示するよう予約し、次の描画先を返す。
 * トリプルバッファ: 表示中 (front)・予約中 (pend)・描画先 (back) の 3 枚。
 * 前の予約がまだ表示されていない (その後 VBL が来ていない) ときだけ、
 * VBL を待ってから予約する (描画が 1 VBL より速いとき、1 VBL に 1 枚)。
 * COP1LC は VBL で Copper が読み直すので、書いた後の VBL から表示される。 */
/* 表示時刻の予測 (2026-11-17 ユーザー承認、案 A)。
 * トリプルバッファでは描き始めが VBL の途中になり、描き始めから表示までの
 * VBL 数が毎回変わる。描き始めの時刻で動きを決めると、表示の間隔と動きの
 * 量がずれて揺れる (ROTAZOOM / PLASMA など、ユーザー指摘)。
 * そこで、この絵が表示される VBL を予測して返し、video_flip はその VBL
 * より早く表示しない。予測には実行中に測った「描き始めから描き終わりの
 * 次の VBL まで」の VBL 数 (直近 FR_HIST 枚の最大) を使う。固定値や
 * prof の値は使わないので CPU の速さに依らない。 */
#define FR_HIST 4
static int fr_open;                 /* 1 = この絵の予測を返した */
static u32 fr_start, fr_target, fr_last_disp;
static u8 fr_hist[FR_HIST];
static int fr_hi;

u32 video_frame_vbl(void) {
    if (custom_screen)
        return vblank_frames();     /* カスタム画面は表示中のリストへ直接書く */
    if (!fr_open) {
        u32 lat = 1;
        for (int i = 0; i < FR_HIST; i++)
            if (fr_hist[i] > lat)
                lat = fr_hist[i];
        fr_start = vblank_frames();
        fr_target = fr_start + lat;
        if ((s32)(fr_target - (fr_last_disp + 1)) < 0)
            fr_target = fr_last_disp + 1;   /* 前の絵より後にしか出せない */
        fr_open = 1;
    }
    return fr_target;
}

void video_flip(void) {
    if (custom_screen) {
        fr_open = 0;
        return;                     /* カスタム画面中は切り替えない */
    }
    u32 ready = vblank_frames();    /* 描き終わった時刻 */
    if (slot_pal_ver[back] != pal_ver) {    /* この絵のパレットを最新に */
        write_palette_slot(back, pal_out);
        slot_pal_ver[back] = pal_ver;
    }
    if (pend >= 0) {
        while (vblank_frames() == pend_vbl)
            ;                       /* 前の予約が表示されるのを待つ */
        front = pend;
    }
    if (fr_open)                    /* 予測した VBL より早く出さない */
        while ((s32)(vblank_frames() + 1 - fr_target) < 0)
            ;
    custom->cop1lc = (u32)copper[back];
    /* 書いた後に読む: その間に VBL が来ても「まだ表示されていない」と
     * 見なすだけ (1 VBL 余分に待つ) で、表示中の画面に描くことはない */
    pend_vbl = vblank_frames();
    pend = back;
    back = (VIDEO_NBUF * (VIDEO_NBUF - 1) / 2) - front - pend;   /* 残りの 1 枚 */
    fr_last_disp = pend_vbl + 1;    /* この絵が出る VBL */
    if (fr_open) {
        u32 l = ready + 1 - fr_start;
        if ((s32)l < 1) l = 1;
        if (l > 8) l = 8;
        fr_hist[fr_hi] = (u8)l;
        fr_hi = (fr_hi + 1) % FR_HIST;
        fr_open = 0;
    }
    if (restore_needed[back]) {     /* カスタム画面だったリストを通常に戻す */
        set_bitplane_slot(back, screen[back], SCREEN_W / 8, SCREEN_H, 0, 0);
        split_off(back);
        restore_needed[back] = 0;
    }
}

/* カスタム画面に入るとき: 表示中のリストを使う。そのリストのパレットが
 * 古ければ合わせる (入る直前に書いたパレットは描画先のリストにしか
 * 入っていない。ENDSCRL の文字が暗かった、2026-11-17 ユーザー指摘)。 */
static void enter_custom_slot(void) {
    settle_pending();
    custom_slot = front;
    if (slot_pal_ver[front] != pal_ver) {
        write_palette_slot(front, pal_out);
        slot_pal_ver[front] = pal_ver;
    }
}

/* カスタム画面をやめるが、表示はすぐには変えない。次の video_flip で
 * 描いた絵へ切り替わる (前の画面を残したままつなぐ場面用)。
 * カスタム画面のメモリは、その絵が表示されるまで解放しないこと。 */
void video_leave_custom_deferred(void) {
    if (!custom_screen)
        return;
    custom_hires = 0;
    custom_screen = 0;
    custom_scroll_x = 0;
    custom_scroll_y = 0;
    restore_needed[custom_slot] = 1;
    for (int s = 0; s < VIDEO_NBUF; s++)
        if (s != custom_slot)
            split_off(s);
}

/* カスタムビットプレーン表示 (ハードウェア横スクロール用)
 * planes: 8 プレーン連続のチップメモリ / stride_bytes: 1 行のバイト数
 * planes=0 で通常のダブルバッファ表示に戻す */
void video_set_custom_screen(u8 *planes, int stride_bytes) {
    video_set_custom_screen_rows(planes, stride_bytes, SCREEN_H);
}

/* プレーン高さを指定する版 (縦スクロール用に 256 行超のバッファを表示できる) */
void video_set_custom_screen_rows(u8 *planes, int stride_bytes, int rows) {
    custom_hires = 0;
    custom_screen = planes;
    custom_stride = stride_bytes;
    custom_rows = rows;
    custom_scroll_x = 0;
    custom_scroll_y = 0;
    if (planes) {
        enter_custom_slot();
        set_bitplane_slot(custom_slot, planes, stride_bytes, rows, 0, 0);
    } else {
        set_bitplane_slot(front, screen[front], SCREEN_W / 8, SCREEN_H, 0, 0);
        restore_needed[front] = 0;
        video_set_split(0, 0, 0);       /* 通常画面に戻すときは分割もやめる */
    }
}

/* カスタム画面の切り替え (表示バッファ + スクロール位置を同時に設定)
 * ダブルバッファしたワールドを切り替えるときに使う。
 * 1 回の set_bitplane_slot で全レジスタを更新するので、
 * 途中の状態が 1 フレーム表示されることがない。 */
void video_switch_custom_screen(u8 *planes, int stride_bytes, int scroll_x) {
    custom_hires = 0;
    custom_screen = planes;
    custom_stride = stride_bytes;
    custom_rows = SCREEN_H;
    custom_scroll_x = scroll_x;
    custom_scroll_y = 0;
    enter_custom_slot();
    set_bitplane_slot(custom_slot, planes, stride_bytes, SCREEN_H, scroll_x, 0);
}

/* 640x256 hires 4 プレーンのカスタム画面 (ENDSCRL 用)。
 * planes: 4 プレーン連続 (各 stride_bytes * rows)、stride_bytes は 80 以上。
 * 縦スクロールは video_set_scroll_y。planes=0 で通常表示へ戻す
 * (video_set_custom_screen(0, 0) でも戻る)。 */
void video_set_custom_hires(u8 *planes, int stride_bytes, int rows) {
    custom_hires = planes != 0;
    custom_screen = planes;
    custom_stride = stride_bytes;
    custom_rows = rows;
    custom_scroll_x = 0;
    custom_scroll_y = 0;
    if (planes) {
        enter_custom_slot();
        set_bitplane_slot(custom_slot, planes, stride_bytes, rows, 0, 0);
    } else {
        set_bitplane_slot(front, screen[front], SCREEN_W / 8, SCREEN_H, 0, 0);
        restore_needed[front] = 0;
    }
}

/* カスタム画面 (ハードウェアスクロール) 表示中か */
int video_custom_screen_active(void) {
    return custom_screen != 0;
}

/* カスタム画面の横スクロール位置 (ピクセル単位、次のフレームから反映) */
void video_set_scroll(int x) {
    if (!custom_screen || x == custom_scroll_x)
        return;
    custom_scroll_x = x;
    set_bitplane_slot(custom_slot, custom_screen, custom_stride, custom_rows,
                      x, custom_scroll_y);
}

/* カスタム画面の縦スクロール位置 (行単位、次のフレームから反映) */
void video_set_scroll_y(int y) {
    if (!custom_screen || y == custom_scroll_y)
        return;
    custom_scroll_y = y;
    set_bitplane_slot(custom_slot, custom_screen, custom_stride, custom_rows,
                      custom_scroll_x, y);
}

/* パレットを 1 つのコッパーリストへ書く */
static void write_palette_slot(int s, const u8 *rgb) {
    {
        for (int b = 0; b < 8; b++) {
            u16 *ph = palhi[s][b];
            u16 *pl = pallo[s][b];
            for (int i = 0; i < 32; i++) {
                int ci = b * 32 + i;
                u8 r = rgb[ci * 3 + 0], g = rgb[ci * 3 + 1], bl = rgb[ci * 3 + 2];
                ph[i * 2] = (u16)(((r >> 4) << 8) | ((g >> 4) << 4) | (bl >> 4));
                pl[i * 2] = (u16)(((r & 0xF) << 8) | ((g & 0xF) << 4) | (bl & 0xF));
            }
        }
    }
}

/* パレットを書く (cur_pal は更新しない)。カスタム画面中は表示中のリストへ
 * すぐ書く。通常は描画先 (back) のリストだけに書き、その絵と一緒に表示が
 * 変わる。他のリストは video_flip で描画先になったときに合わせる。 */
static void write_palette(const u8 *rgb) {
    if (rgb != pal_out)
        for (int i = 0; i < 256 * 3; i++)
            pal_out[i] = rgb[i];
    pal_ver++;
    int s = custom_screen ? custom_slot : back;
    write_palette_slot(s, pal_out);
    slot_pal_ver[s] = pal_ver;
}

void video_set_palette(const u8 *rgb) {
    /*
     * AGA 256 色 (24bit) パレットをコッパーリストへ書く。
     * 上位 12bit はバンクごとの上位セクション、下位 12bit は LOCT=1 セクション。
     * CPU レジスタへの直接書き込みは行わない (コッパーが毎フレーム反映する)。
     */
    for (int i = 0; i < 256 * 3; i++)
        cur_pal[i] = rgb[i];
    write_palette(rgb);
}

/* パレットをすべてのコッパーリストへすぐ書く (表示中の絵にもすぐ効く)。
 * 表示位置などをすぐ変える場面で、古い絵が古いパレットで見えないように
 * する (PLZPART の場面切り替え、2026-11-17 ユーザー指摘)。 */
void video_set_palette_now(const u8 *rgb) {
    for (int i = 0; i < 256 * 3; i++) {
        cur_pal[i] = rgb[i];
        pal_out[i] = rgb[i];
    }
    pal_ver++;
    for (int s = 0; s < VIDEO_NBUF; s++) {
        write_palette_slot(s, pal_out);
        slot_pal_ver[s] = pal_ver;
    }
}

/* 現在のパレットにフェード係数 (0-64) を掛けてコッパーへ書く (シーン遷移用)
 * 注意: cur_pal は変更しない (変更するとフェードの度に暗くなる) */
const u8 *video_get_palette(void) { return cur_pal; }

void video_apply_fade(u8 level) {
    static u8 faded[256 * 3];
    if (level >= 64) {
        write_palette(cur_pal);
        return;
    }
    for (int i = 0; i < 256 * 3; i++)
        faded[i] = (u8)((cur_pal[i] * level) >> 6);
    write_palette(faded);
}

