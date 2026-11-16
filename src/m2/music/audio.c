/*
 * Second Reality A1200: Paula 4ch ソフトウェアミキサー
 *
 * S3M 8 ボイスを 4 つの Paula チャンネルへ 2 ボイスずつミックスする。
 *   Paula ch0 (L) = voice 0, 2
 *   Paula ch1 (R) = voice 1, 3
 *   Paula ch2 (L) = voice 4, 6
 *   Paula ch3 (R) = voice 5, 7
 *
 * ミックスレートは 1 サンプル/走査線 = 15625Hz。PAL の VBL は 49.92Hz
 * (313 ライン) なので 1 VBL = 313 サンプル、2 VBL = 626 サンプル (偶数)。
 * これは Paula の AUDxLEN (ワード単位) と正確に一致する。
 * 以前は 625 サンプルだけミックスして 626 個目を直前サンプルの繰り返しで
 * 埋めていたため、バッファ境界ごとに 1 サンプルの段差が入り、約 25Hz の
 * 周期的なクリック (フラックスのピーク 20.8Hz) が出ていた。
 * 実音だけオンセット数が 2 倍になり「テンポが速い」と聞こえる原因だった。
 *
 * 8 個のリングバッファを VBL 割り込みで 2 VBL ごとに切り替える。
 * メインループが数フレーム遅れても 40ms x 8 の余裕で DMA が途切れにくい。
 */
#include "music.h"
#include "../framework/mem.h"
#include "mod.h"
#include "../framework/framework.h"
#include "../framework/vblank.h"
#include <proto/exec.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>

extern volatile struct Custom *custom;
extern const u8 *g_pcm_data;      /* デバッグ: PCM 直接再生 (NULL なら通常) */
#define PCM_LEN 1875000u

/* PAL の VBL は 49.92Hz (313 ライン) なので、15625Hz では 1 VBL = 313 サンプル。
 * 2 VBL = 626 サンプル (偶数) が Paula の AUDxLEN (ワード単位) と正確に一致する。
 * 以前は 625 サンプルだけミックスし 626 個目を「直前サンプルの繰り返し」で
 * 埋めていたため、バッファ境界ごとに 1 サンプルの段差が入り、約 25Hz の
 * 周期的なクリック (スペクトルフラックスのピーク 20.8Hz) が出ていた。
 * これが実音だけオンセット数が 2 倍に増え「テンポが速い」と聞こえる原因。 */
#define FRAME_SAMPLES 313
#define AUDIO_FRAMES  2
#define MIX_SAMPLES   (FRAME_SAMPLES * AUDIO_FRAMES)  /* 626 (有効サンプル) */
#define BUF_SAMPLES   MIX_SAMPLES                     /* 626 (DMA 長) */
#define NBUF          16
#define NCH           4
#define PAULA_PERIOD  227
#define PAULA_VOL     64

static u8 *buf[NCH][NBUF];
static u8 *bufmem[NCH];         /* 連続領域 (DMA が全体をループする) */
static volatile int play_idx;   /* 現在 DMA が再生しているバッファ */
static volatile int filled;     /* play_idx の後ろに積まれているバッファ数 */
static int vbl_phase;
static int started;
static int last_peak;

/* ---- tick を「サンプル単位」で進める ----
 * S3M の 1 tick = 2.5/tempo 秒。PAL 50Hz では 1 VBL(313 サンプル) あたり
 * tempo/124.8 tick。tick_acc は「Q16 tick x 313 サンプル」の単位で持ち、
 * 1 サンプルごとに tempo*525 ずつ増える。
 *
 * 以前は 626 サンプル (= 40.06ms) をまとめてミックスし、その時点の
 * プレイヤー状態 1 つで埋めていた。MUSIC1 (tempo 130 / speed 3 = 行 57.7ms)
 * は 1 行がバッファ 1.44 個ぶんなので、行頭が 40ms 格子に丸められて
 * 「40ms と 80ms の交互」になり、平均は合っているのに走って聞こえていた。
 * ここではバッファを tick 境界で分割してミックスする。 */
static u32 tempo_pct = 100;
static u32 tick_acc;            /* 0 .. TICK_UNIT-1 */
#define TICK_UNIT ((u32)65536 * (u32)FRAME_SAMPLES)

void audio_set_tempo_pct(u32 pct) {
    if (pct >= 25 && pct <= 400)
        tempo_pct = pct;
}

#define AUD_LEN(ch) (*(volatile u16 *)((u8 *)custom + 0xA4 + (ch) * 0x10))
#define AUD_PER(ch) (*(volatile u16 *)((u8 *)custom + 0xA6 + (ch) * 0x10))
#define AUD_VOL(ch) (*(volatile u16 *)((u8 *)custom + 0xA8 + (ch) * 0x10))

/* AUDxLC は 16bit レジスタ 2 つ。上位ワード→下位ワードの順で書く。 */
static inline void aud_ptr(int ch, u32 a) {
    *(volatile u16 *)((u8 *)custom + 0xA0 + ch * 0x10) = (u16)(a >> 16);
    *(volatile u16 *)((u8 *)custom + 0xA2 + ch * 0x10) = (u16)(a & 0xFFFF);
}

int audio_init(void) {
    for (int c = 0; c < NCH; c++) {
        /* DMA が全体をループできるよう、チャンネルごとに連続領域を確保する。
         * 以前は 16 個を別々に確保し、VBL ごとに Paula の開始ポインタを
         * 書き換えていた。境界が数サンプルずれると毎回波形が切れて
         * 「プチプチ」になるため、ポインタは開始時に一度だけ書く。 */
        bufmem[c] = (u8 *)mem_alloc(NBUF * BUF_SAMPLES,
                                   MEMF_CHIP | MEMF_CLEAR);
        if (!bufmem[c])
            return 0;
        for (int i = 0; i < NBUF; i++)
            buf[c][i] = bufmem[c] + i * BUF_SAMPLES;
    }
    play_idx = 0;
    filled = 0;
    vbl_phase = 0;
    started = 0;
    last_peak = 0;
    for (int c = 0; c < NCH; c++) {
        AUD_PER(c) = PAULA_PERIOD;
        /* PCM テストでは 4ch に同じモノラルを流すので 1/2 音量にする */
        AUD_VOL(c) = g_pcm_data ? (PAULA_VOL / 2) : PAULA_VOL;
    }
    return 1;
}

void audio_shutdown(void) {
    custom->dmacon = DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3;
}

/* idx 番のバッファの start から count サンプルをミックスする。
 * ノートの発音位置をサンプル単位で正確にするため、tick 境界で
 * バッファを分割して呼べるようにしてある。 */
static int mix_range(int idx, int start, int count) {
    int peak = 0;
    for (int c = 0; c < NCH; c++) {
        /* Paula ch0=L: voice 0+2, ch1=R: 1+3, ch2=L: 4+6, ch3=R: 5+7 */
        static const u8 pair0[NCH] = { 0, 1, 4, 5 };
        static const u8 pair1[NCH] = { 2, 3, 6, 7 };
        MixVoice *v0 = &mix_voices[pair0[c]];
        MixVoice *v1 = &mix_voices[pair1[c]];
        u8 *out = buf[c][idx] + start;

        u32 pos0 = v0->pos, step0 = v0->step;
        u32 end0 = v0->loop ? v0->loop_end : v0->length;
        const u8 *d0 = v0->data;
        int gain0 = v0->gain;
        int act0 = v0->active && d0;
        u32 ls0 = v0->loop_start;
        int lp0 = v0->loop;

        u32 pos1 = v1->pos, step1 = v1->step;
        u32 end1 = v1->loop ? v1->loop_end : v1->length;
        const u8 *d1 = v1->data;
        int gain1 = v1->gain;
        int act1 = v1->active && d1;
        u32 ls1 = v1->loop_start;
        int lp1 = v1->loop;

        u8 last = 0;
        int ended0 = v0->ended, ended1 = v1->ended;
        for (int i = 0; i < count; i++) {
            int acc = 0;
            if (act0) {
                acc += (int)(s8)d0[pos0 >> 16] * gain0;
                pos0 += step0;
                if ((pos0 >> 16) >= end0) {
                    if (lp0)
                        pos0 = ls0 << 16;
                    else {
                        act0 = 0;
                        pos0 = 0;
                        ended0 = 1;
                    }
                }
            }
            if (act1) {
                acc += (int)(s8)d1[pos1 >> 16] * gain1;
                pos1 += step1;
                if ((pos1 >> 16) >= end1) {
                    if (lp1)
                        pos1 = ls1 << 16;
                    else {
                        act1 = 0;
                        pos1 = 0;
                        ended1 = 1;
                    }
                }
            }
            int s = acc >> 8;
            if (s > 127) s = 127;
            if (s < -128) s = -128;
            int a = s < 0 ? -s : s;
            if (a > peak) peak = a;
            last = (u8)(s8)s;
            *out++ = last;
        }
        v0->pos = pos0; v0->active = act0; v0->ended = (u8)ended0;
        v1->pos = pos1; v1->active = act1; v1->ended = (u8)ended1;
    }
    return peak;
}

/* 1 VBL (313 サンプル) あたりの tick 増分 (Q16)。
 * tempo/124.8 tick/VBL = tempo * 525 / 65536。 */
static u32 tick_inc_q16(void) {
    u32 tempo = np_tempo ? (u32)np_tempo : 125u;
    return tempo * 525u * tempo_pct / 100u;
}

/* idx 番のバッファを、tick 境界で分割しながらミックスする。
 * tick が来たサンプル位置で player_tick() を呼ぶので、ノートの発音位置は
 * サンプル単位 (1/15625 秒) で正確になる。 */
static void fill_buffer(int idx) {
    int pos = 0;

    /* デバッグ: ホストでレンダリングした PCM をそのまま流す。
     * これで音が壊れるなら Paula/DMA 側、壊れないならプレイヤー側。 */
    if (g_pcm_data) {
        static u32 pcm_pos;
        for (int c = 0; c < NCH; c++) {
            u8 *out = buf[c][idx];
            u32 p = pcm_pos;
            for (int i = 0; i < MIX_SAMPLES; i++) {
                out[i] = g_pcm_data[p];
                if (++p >= PCM_LEN)
                    p = 0;
            }
        }
        pcm_pos += MIX_SAMPLES;
        if (pcm_pos >= PCM_LEN)
            pcm_pos %= PCM_LEN;
        return;
    }
    while (pos < MIX_SAMPLES) {
        u32 inc = tick_inc_q16();
        int n;

        if (inc == 0) {
            n = MIX_SAMPLES - pos;
        } else {
            u32 need = TICK_UNIT - tick_acc;      /* 次の tick までの残り */
            n = (int)((need + inc - 1) / inc);    /* 切り上げでサンプル数に */
            if (n > MIX_SAMPLES - pos)
                n = MIX_SAMPLES - pos;
        }
        if (n < 1)
            n = 1;
        last_peak = mix_range(idx, pos, n);
        pos += n;
        if (inc == 0)
            continue;
        tick_acc += (u32)n * inc;
        while (tick_acc >= TICK_UNIT) {
            tick_acc -= TICK_UNIT;
            player_tick();
        }
    }
}

/* VBL ハンドラから毎 VBL 呼ばれる。2 VBL ごとに次のバッファへ切り替える。
 * AUDxLEN は開始時に一度書けば保持されるので、ここではポインタだけ更新する
 * (毎回 LEN を書くと Paula の長さカウンタが再ロードされ、バッファ境界で
 * 波形が途切れて周期的なクリックになる)。 */
void audio_vbl(void) {
    if (!started)
        return;
    if (++vbl_phase < AUDIO_FRAMES)
        return;
    vbl_phase = 0;

    /* DMA は NBUF 個の連続バッファ全体をループしている。
     * ここでは「今どのバッファを再生中か」を進めるだけで、
     * Paula のポインタは書き換えない (境界クリック防止)。 */
    play_idx = (play_idx + 1) % NBUF;
    if (filled > 0)
        filled--;
}

/* 引数は互換性のため残している (VBL 数は使わない)。
 * DMA が消費した分だけバッファを補充する。補充量は「キューの空き」で
 * 決まるため、画面リフレッシュが 50Hz からずれても音声は Paula の
 * クロック (15625Hz) で一定に流れる。 */
void audio_frame(int frames) {
    (void)frames;
    if (!started) {
        /* 最初の 2 バッファ (80ms) を埋めて DMA を開始する */
        fill_buffer(0);
        fill_buffer(1);
        play_idx = 0;
        filled = 1;
        vbl_phase = 0;
        for (int c = 0; c < NCH; c++) {
            aud_ptr(c, (u32)buf[c][0]);
            AUD_LEN(c) = (NBUF * BUF_SAMPLES) / 2;
        }
        custom->dmacon = DMAF_SETCLR | DMAF_MASTER |
                         DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3;
        started = 1;
        return;
    }

    while (filled < NBUF - 2) {
        int idx;
        Disable();
        if (filled < NBUF - 1)
            idx = (play_idx + filled + 1) % NBUF;
        else
            idx = -1;
        Enable();
        if (idx < 0)
            break;
        fill_buffer(idx);
        Disable();
        filled++;
        Enable();
    }
}

int audio_peak(void) { return last_peak; }
