/*
 * M4 シーン: TUNNELI (ドットトンネル) — オリジナル TUNNELI/TUN10.PAS + TUNNELI.OBJ の移植
 *
 * - 101 個のリング (putki[]) がトンネルの中心線を作る。毎フレーム先頭リングを
 *   sin/cos の合成で動かし、全体を 1 つずつ手前へずらす。
 * - 描画はリング x=80..4 の 64 点 (円周) をドットで打つ。半径は
 *   pcalc[sade[x]][i] (オリジナルの TUNNEL.DAT をそのまま使う)。
 *   pcalc は 320x200 画面の中心 (160,100) を基準にした楕円で、
 *   手前のリングほど画面より大きくなる (縁の弧だけが見える)。
 * - 元は Mode 13h のドット描画なので、Amiga では planar 直接描画に置き換えた
 *   (背景は黒なので、前フレームの点を色 0 で塗れば消える)。
 * - パレット: 64-127 のグレー階調 (奥行きで暗くなる)。
 */
#include "scenes.h"
#include "../framework/video.h"
#include "../framework/vblank.h"

#define NRINGS      101
#define NPOINTS     64
#define PCALC_ROWS  138
#define PLANE_ROWBYTES 40
#define PLANE_SIZE  (PLANE_ROWBYTES * 256)
#define PCALC_COLS  64
#define PCX         160         /* pcalc の中心 x */
#define WIN_Y0      28          /* 320x200 の窓の先頭行 (他パートと同じ) */
#define PCY         (WIN_Y0 + 100)  /* pcalc の中心 y (原作 320x200 の 100) */
#define PCY_SRC     100         /* pcalc の中心 y (320x200 画面) */
/* 2026-11-16 ユーザー指示: 原作の 320x200 をそのまま (縦の換算なし) 28 行目
 * から表示する。以前は縦を 256/200 = 1.28 倍にして 256 行全体に描いていた。
 * 原作 TUN10.PAS:179-187 は x だけを 0..319 で判定し、y は rows[] (0..200)
 * で引く。こちらは窓 (原作の 0..199 行) の外には描かない。 */
#define PCALC_SX    256         /* 1.0 (8.8 固定小数点) */
#define TUN_MAXOFF  80          /* リング中心のずれの上限 (px) */

static int sin1024[1024];
static int sinit[4096];
static int cosit[2048];
static int sade[NRINGS];
static s16 pcalc_y[PCALC_ROWS][PCALC_COLS];

/* オリジナルの pcalc[138][64] (TUNNEL.DAT)。x は 160、y は 100 中心。 */
extern const u8 *g_tunnel_pcalc;
extern const u8 *g_tunnel_sinit;
static const s16 *pcalc;

static int ring_x[NRINGS], ring_y[NRINGS], ring_c[NRINGS];
/* ダブルバッファのため、描画先のバックバッファには 2 フレーム前の点が
 * 残っている。フレーム番号のパリティで 2 世代を保持し、正しい世代を消す。 */
static s16 px[2][NRINGS][NPOINTS], py[2][NRINGS][NPOINTS];

static int sx_pos, sy_pos, frame;
static u32 tunnel_start_vbl;          /* 読み込み待ち (TUNNEL_LOAD_VBL) の起点 */
static u32 tunnel_last_vbl;
static int tunnel_acc, tunnel_renders, tunnel_done;
static u8 palette[256 * 3];

static int isin(int deg) { return sin1024[deg & 1023]; }
static int icos(int deg) { return sin1024[(deg + 256) & 1023]; }

static inline void plot(u8 *planes, int x, int y, u8 color) {
    if ((unsigned)x >= 320u || (unsigned)(y - WIN_Y0) >= 200u)
        return;
    u8 *p = planes + y * PLANE_ROWBYTES + (x >> 3);
    u8 bit = (u8)(0x80u >> (x & 7));
    u8 nbit = (u8)~bit;
    if (color & 0x01) p[0 * PLANE_SIZE] |= bit; else p[0 * PLANE_SIZE] &= nbit;
    if (color & 0x02) p[1 * PLANE_SIZE] |= bit; else p[1 * PLANE_SIZE] &= nbit;
    if (color & 0x04) p[2 * PLANE_SIZE] |= bit; else p[2 * PLANE_SIZE] &= nbit;
    if (color & 0x08) p[3 * PLANE_SIZE] |= bit; else p[3 * PLANE_SIZE] &= nbit;
    if (color & 0x10) p[4 * PLANE_SIZE] |= bit; else p[4 * PLANE_SIZE] &= nbit;
    if (color & 0x20) p[5 * PLANE_SIZE] |= bit; else p[5 * PLANE_SIZE] &= nbit;
    /* プレーン 6, 7 は常に 0 (色番号は 0..63、下の make_palette) */
}

/* 6 プレーン化 (2026-11-16 ユーザー承認): 原作のパレットは 6bit (0..63) の
 * グレーだけなので、色番号 = 6bit の明るさ (0..63) に付け替える。
 *   原作 TUN10.PAS:121  64+x  = (64-x)         (x=0 の 64 は VGA の下位
 *                                                6bit で 0 = 黒)
 *   原作 TUN10.PAS:122  128+x = (64-x)*3 div 4
 *   原作 TUN10.PAS:124-125  68, 132 = 黒
 * 描画のときは原作の色番号 (ring_c + 奥行き) を col_idx で引く。
 * 8bit の値は 6bit を 4 倍 (以前の 64+x 番と同じ換算)。 */
static u8 col_idx[256];

static void make_palette(void) {
    for (int i = 0; i < 256 * 3; i++)
        palette[i] = 0;
    for (int i = 0; i < 64; i++) {
        palette[i * 3 + 0] = (u8)(i * 4);
        palette[i * 3 + 1] = (u8)(i * 4);
        palette[i * 3 + 2] = (u8)(i * 4);
    }
    for (int i = 0; i < 256; i++)
        col_idx[i] = 0;
    /* 原作は 121 行のループを終えてから 122 行のループを回すので、両方が
     * 書く 128 番は 122 行の x=0 (= 48) になる。以前の移植は 1 つのループで
     * 交互に書いていたため、128 番が 121 行の x=64 (= 黒) になっていた。 */
    for (int x = 0; x <= 64; x++)
        col_idx[64 + x] = (u8)((64 - x) & 63);          /* 64 -> 0 (黒) */
    for (int x = 0; x <= 64; x++)
        col_idx[128 + x] = (u8)((64 - x) * 3 / 4);
    col_idx[68] = 0;
    col_idx[132] = 0;
    video_set_palette(palette);
}

void tunnel_init(void) {
    pcalc = (const s16 *)g_tunnel_pcalc;
    /* pcalc の y を中心からの差にしておく (縦の換算は無し) */
    for (int r = 0; r < PCALC_ROWS; r++)
        for (int i = 0; i < PCALC_COLS; i++)
            pcalc_y[r][i] = (s16)(pcalc[r * PCALC_COLS * 2 + i * 2 + 1] - PCY_SRC);

    for (int i = 0; i < 1024; i++) {
        int deg = i & 1023;
        int sign = 1;
        if (deg >= 512) { deg -= 512; sign = -1; }
        int t = deg;
        int u = t * (512 - t);
        sin1024[i] = sign * (16 * u * 256 / (5 * 262144 - 4 * u));
    }
    /* 原作は SINIT.DAT をそのまま使う (TUN10.PAS:102-103:
     *   move(mem[seg(sini)], sinit, 4097*2);
     *   move(mem[seg(sini)+4097*2], cosit, 2048*2);
     * 中身は SINGEN.PAS の
     *   sinit[x]=round(sin(x/128*pi)*((x*3) div 128))
     *   cosit[x]=round(cos(x/128*pi)*((x*4) div 64))
     * TUN10.PAS のコメント式 (x*3 div 16 / x/256*pi) は使われていない。 */
    if (g_tunnel_sinit) {
        const u8 *t = g_tunnel_sinit;
        for (int x = 0; x < 4096; x++)
            sinit[x] = (s16)(u16)(t[x * 2] | (t[x * 2 + 1] << 8));
        for (int x = 0; x < 2048; x++) {
            int o = (4097 + x) * 2;
            cosit[x] = (s16)(u16)(t[o] | (t[o + 1] << 8));
        }
    } else {
        for (int x = 0; x < 4096; x++) {
            int a = (x * 4) & 1023;
            int amp = (x * 3) / 128;
            sinit[x] = isin(a) * amp / 256;
        }
        for (int x = 0; x < 2048; x++) {
            int a = (x * 2) & 1023;
            int amp = (x * 4) / 64;
            cosit[x] = icos(a) * amp / 256;
        }
    }

    for (int z = 0; z < NRINGS; z++) {
        sade[z] = 16384 / (z * 7 + 95);
        ring_x[z] = 0;
        ring_y[z] = 0;
        ring_c[z] = 0;
    }
    for (int b = 0; b < 2; b++)
        for (int z = 0; z < NRINGS; z++)
            for (int i = 0; i < NPOINTS; i++)
                px[b][z][i] = py[b][z][i] = -1;
    sx_pos = 0;
    sy_pos = 0;
    frame = 0;
    tunnel_acc = 0;
    tunnel_renders = 0;
    tunnel_done = 0;
    tunnel_last_vbl = vblank_frames();
    tunnel_start_vbl = tunnel_last_vbl;
    make_palette();
}

/* GLENZ → TUNNELI の間の実機の読み込み時間 (U2.ASM partexecute で
 * TUNNELI.EXE を読み込み・展開する時間)。ソースに値は無いので実測。
 * 方法 (2026-11-14): 原作の動画 (59.94fps) の音声と、移植版の
 *   WinUAE ループバック録音を、音の立ち上がり (10ms) の相互相関で
 *   位置合わせ (オフセット 127.56 秒、20 秒窓 4 か所で ±0.06 秒) し、
 *   同じ音楽位置で映像のイベント時刻を比べた。
 *   L = 1.18 秒 (59 VBL) の移植版: トンネルの点が出る +0.71 秒遅い、
 *   TECHNO の白フラッシュ +0.46 秒遅い。
 *   両方の誤差が最小になる 1.18 - (0.71+0.46)/2 = 0.595 秒 → 30 VBL
 *   (トンネル +0.12 秒 / フラッシュ -0.13 秒の見込み)。
 *   以前の 1.18 秒は音楽データからの理論時刻で出したもので、移植版の
 *   実際の音楽位置とは 0.5 秒前後ずれていた。 */
#define TUNNEL_LOAD_VBL 30

int tunnel_finished(void) {
    return tunnel_done;
}

int tunnel_render(u8 *chunky, u8 *planes, int width, int height) {
    (void)chunky;
    (void)width;
    (void)height;
    /* プレーン 6, 7 と窓の外の行は切り替え時に 0 にされ (main の
     * video_clear_planes)、以後も描かないので、チップ RAM へは 6 プレーン
     * x 200 行だけ写す。 */
    video_set_commit_area(6, WIN_Y0, 200);


    /* 原作 TUN10.PAS:200-225: dis_waitb() が返した 70Hz フレーム数だけ
     * アニメを進める。PAL では 1 VBL = 1.4 フレーム。 */
    u32 now = vblank_frames();
    if (now - tunnel_start_vbl < TUNNEL_LOAD_VBL) {
        tunnel_last_vbl = now;          /* 読み込み中は黒のまま */
        return 1;
    }
    u32 el = now - tunnel_last_vbl;
    if (el > 10)
        el = 10;
    tunnel_last_vbl = now;
    tunnel_acc += (int)el * 7;          /* 7/5 = 1.4 */
    int steps = tunnel_acc / 5;
    tunnel_acc -= steps * 5;

    /* バックバッファには 2 描画前の点が残っている。その世代を消す。 */
    int b = tunnel_renders & 1;
    for (int r = 4; r <= 80; r++)
        for (int i = 0; i < NPOINTS; i++)
            if (px[b][r][i] >= 0)
                plot(planes, px[b][r][i], py[b][r][i], 0);

    for (int s = 0; s < steps && !tunnel_done; s++) {
        /* リング更新: 先頭を動かし、全体を 1 つずらす */
        ring_x[100] = cosit[sy_pos & 2047] - sinit[(sy_pos * 3) & 4095] - cosit[sx_pos & 2047];
        ring_y[100] = sinit[(sx_pos * 2) & 4095] - cosit[sx_pos & 2047];
        for (int r = 1; r <= 100; r++) {
            ring_x[r - 1] = ring_x[r];
            ring_y[r - 1] = ring_y[r];
            ring_c[r - 1] = ring_c[r];
        }
        sx_pos++;
        sy_pos++;
        if ((sy_pos & 15) > 7)
            ring_c[99] = 128;
        else
            ring_c[99] = 64;
        /* 原作 TUN10.PAS:220: if frame >= veke-102 then putki[99].c := 0;
         * 最後の 102 フレームは新しいリングを色 0 (bbc<64 で描かない) にし、
         * トンネルが奥から消えていく。以前はこれが無く、最後まで
         * リングが出続けてフェードアウトしなかった (2026-11-14 指摘)。 */
        if (frame >= 1060 - 102)
            ring_c[99] = 0;
        frame++;
        /* 原作 TUN10.PAS:3,221: veke=1060 フレームで終了 */
        if (frame >= 1060)
            tunnel_done = 1;
    }

    /* 描画 */
    int base_x = ring_x[5];
    int base_y = ring_y[5];

    /* オリジナルの putki[x] の軌道は振幅が時間とともに増え、そのまま使うと
     * リングが画面外へ飛んでしまう (リング同士の間隔が開きすぎる)。
     * 形は保ったまま、ずれの最大値が PLZ 以下になるよう全体を縮小する。 */
    int dxs[NRINGS], dys[NRINGS];
    int md = 0;
    for (int r = 80; r >= 4; r--) {
        int dx = ring_x[r] - base_x;
        int dy = ring_y[r] - base_y;
        dxs[r] = dx;
        dys[r] = dy;
        int ax = dx < 0 ? -dx : dx;
        int ay = dy < 0 ? -dy : dy;
        if (ax > md) md = ax;
        if (ay > md) md = ay;
    }
    int sc = (md <= TUN_MAXOFF) ? 256 : (TUN_MAXOFF * 256) / md;

    for (int r = 80; r >= 4; r--) {
        int dx = (dxs[r] * sc) >> 8;
        int dy = (dys[r] * sc) >> 8;
        /* オリジナル: bbc = putki[x].c + round(x/1.3) で、
         * bbc < 64 のリングはまだ描かない (最初は全ドット不可視)。
         * 色 c は奥 (r=99) から手前へ伝播するので、ドットは
         * 奥から徐々に現れる。 */
        int col = ring_c[r] + (r * 10) / 13;
        if (col < 64) {
            for (int i = 0; i < NPOINTS; i++)
                px[b][r][i] = -1;
            continue;
        }
        if (col > 191) col = 191;
        u8 c = col_idx[col];       /* 原作の色番号 -> 6bit の明るさ */
        /* オリジナル: pcalc[br][i] + (_bx, by)。br は sade[r]。 */
        const s16 *pc = pcalc + sade[r] * PCALC_COLS * 2;
        for (int i = 0; i < NPOINTS; i++) {
            int gx = PCX + (((pc[i * 2 + 0] - PCX) * PCALC_SX) >> 8) + dx;
            int gy = PCY + pcalc_y[sade[r]][i] + dy;
            px[b][r][i] = (s16)gx;
            py[b][r][i] = (s16)gy;
            plot(planes, gx, gy, c);
        }
    }

    tunnel_renders++;
    return 1;                   /* planar 直接描画 */
}
