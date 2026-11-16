/*
 * Second Reality A1200: STMIK 互換 S3M プレイヤー
 *
 * STMIK.300 (Sami Tammilehto) の逆アセンブル解析に基づく再実装。
 * - np_zplus: bit0 = 次のオーダーが 0xFE (スキップ)、bit1 = 現在のオーダーが
 *   0xFE を通過 (セクション先頭)。DIS の dis_musplus() がこの値を
 *   「符号付き行距離」に変換する。
 * - np_zinfo: S3M コマンド 'Z' (26) の info バイト。
 * - オーダーリスト: 0xFE = スキップ、0xFF = 終端 (ループ/停止)。
 */
#include "music.h"
#include "mod.h"
#include "../framework/framework.h"
#ifdef HOST_TRACE
#include <stdio.h>
#endif

/* ---- ノート→ピリオド (S3M/ST3 スケール、C-4 = 1712) ----
 * インデックスは「半音番号」(C-0 = 0, C#0 = 1, ... B-7 = 95)。
 * ST3 の C-4 ピリオド表 (1712,1616,1524,1440,1356,1280,1208,1140,
 * 1076,1016,960,907) を 1 オクターブごとに 1/2 していく。
 * ProTracker の表 (E-4=1359 など) とは最大約 4.5 セント異なり、
 * ProTracker の値だと S3M 本来よりわずかに低く鳴る。 */
static const u16 note_period[128] = {
    27392, 25856, 24384, 23040, 21696, 20480, 19328, 18240, 17216, 16256, 15360, 14512,
    13696, 12928, 12192, 11520, 10848, 10240,  9664,  9120,  8608,  8128,  7680,  7256,
     6848,  6464,  6096,  5760,  5424,  5120,  4832,  4560,  4304,  4064,  3840,  3628,
     3424,  3232,  3048,  2880,  2712,  2560,  2416,  2280,  2152,  2032,  1920,  1814,
     1712,  1616,  1524,  1440,  1356,  1280,  1208,  1140,  1076,  1016,   960,   907,
      856,   808,   762,   720,   678,   640,   604,   570,   538,   508,   480,   453,
      428,   404,   381,   360,   339,   320,   302,   285,   269,   254,   240,   226,
      214,   202,   190,   180,   169,   160,   151,   142,   134,   127,   120,   113,
      107,   101,    95,    90,    84,    80,    75,    71,    67,    63,    60,    56,
       53,    50,    47,    45,    42,    40,    37,    35,    33,    31,    30,    28,
       26,    25,    23,    22,    21,    20,    18,    17,
};

/* ---- 公開 np_* 変数 (STMIK 互換) ---- */
u16 np_ord, np_pat, np_row, np_speed, np_tempo, np_loop;
u16 np_zinfo, np_zplus, np_zframe;
u16 np_mastervol = 48, np_mastermul = 48, np_masterflags = 0;

/* ---- 0xFE マーカーの復元 (std 版 S3M 用) ----
 * Modland 版 MUSIC0_std.s3m / MUSIC1_std.s3m は、オリジナルの
 * reference/SecondReality/MAIN/MUSIC0.S3M / MUSIC1.S3M のオーダーリストから
 * 0xFE (スキップ) をすべて削除したもの。そのため np_zplus が立たず、
 * dis_musplus() が常に -32 を返す (ROADMAP 12.28)。
 * ここでは削除された 0xFE の位置を「0xFE の直後のオーダーの std 添字」で
 * 登録し、プレイヤーが 0xFE を通過したのと同じように np_zplus を立てる。 */
typedef struct {
    const void *module;
    const u16 *list;
    u16 count;
} MarkerSet;
#define MAX_MARKER_SETS 4
static MarkerSet marker_sets[MAX_MARKER_SETS];
static u16 marker_set_count;
static const u16 *marker_list;
static u16 marker_count;

void zsetmarkers(const void *module, const u16 *list, u16 count) {
    for (u16 i = 0; i < marker_set_count; i++) {
        if (marker_sets[i].module == module) {
            marker_sets[i].list = list;
            marker_sets[i].count = count;
            return;
        }
    }
    if (marker_set_count < MAX_MARKER_SETS) {
        marker_sets[marker_set_count].module = module;
        marker_sets[marker_set_count].list = list;
        marker_sets[marker_set_count].count = count;
        marker_set_count++;
    }
}

static int is_marker(u16 idx) {
    if (!marker_list)
        return 0;
    for (u16 i = 0; i < marker_count; i++)
        if (marker_list[i] == idx)
            return 1;
    return 0;
}

/* ---- ミキサーへ公開する 8 ボイス ---- */
MixVoice mix_voices[8];

/* ---- 内部状態 ---- */
typedef struct {
    u8 note, ins, vol, cmd, info;
    u8 last_ins;        /* 現在の楽器 (ノートなしの楽器指定でも差し替わる) */
    u8 last_note;       /* 最後に発音したノート (アルペジオ用) */
    u8 cur_vol;         /* 0-64 */
    u16 base_period;    /* ノート本来のピリオド (ポルタで変化) */
    u16 target_period;  /* トーンポルタ用 */
    u8 porta_speed;
    u8 ef_mem;          /* E/F の直前の非ゼロ値 (E00/F00 が使う。ST3 は E と F で共有) */
    u8 vib_speed, vib_depth, vib_pos;
    u8 vol_slide_mem;   /* 直前の非ゼロ Dxy パラメータ (D00 が継続に使う) */
    u8 retrig_speed;    /* Qxy の y (リトリガ間隔) */
    u8 retrig_vol;      /* Qxy の x (音量モディファイア) */
    u8 retrig_cnt;      /* Qxy のカウンタ */
    u8 note_delay;
    u8 pending_note;
    u8 pending_vol;     /* 遅延ノート用の音量 */
    u16 offset;         /* サンプルオフセット (サンプル単位) */
    u8 active;
} Channel;

static Channel ch[8];

static const u8 *mod;
static const ModHeader *mh;
static const ModInstrument *minst;
static const ModInstrument *minst_p(void) { return minst; }
static const u32 *pat_tbl;
static const u8 *orders;

static u16 order_index;
static u8 row, tick;
static u8 playing, loop_mode;       /* 0=停止, 1=ループ再生, 2=zgotosong 直後 */
static u16 loop_order;
static u8 force_advance;
static u8 force_row;
static u16 force_order;
static u8 force_order_valid;

static u8 vib_table[64];

static void setup_vib(void) {
    static const u8 t[32] = {
        0, 25, 50, 74, 98, 120, 141, 161, 180, 197, 212, 225, 235, 244, 250, 254,
        255, 254, 250, 244, 235, 225, 212, 197, 180, 161, 141, 120, 98, 74, 50, 25
    };
    for (int i = 0; i < 32; i++) {
        vib_table[i] = t[i];
        vib_table[i + 32] = 255 - t[i];
    }
}

/* ---- S3M ノート (octave<<4 | semitone) → 半音番号 ---- */
static int note_to_semitone(u8 note) {
    return (int)(note >> 4) * 12 + (int)(note & 0x0F);
}

/* ---- 半音インデックスからピリオドを求める ---- */
static u16 period_of_semitone(int semi) {
    if (semi < 0)
        semi = 0;
    if (semi > 127)
        semi = 127;
    return note_period[semi];
}

/* ST3 のピリオドは楽器の C4 速度 (c2spd) を含む: period * 8363 / c2spd。
 * スライド (E/F/G) とビブラートはこの単位で動くので、c2spd が大きい
 * 楽器ほど同じ Gxx でも音程の動きが大きい。以前は c2spd を含まない
 * ピリオドで滑らせていたため、例えば ENDSCRL の SonixLead (c2spd 14650)
 * の G03 が 5 半音を登り切らなかった (2026-11-15 ユーザー指摘、1:26 付近)。
 * 発音の速さは update_voice で 8363 * 7181 / period として同じになる。 */
static const ModInstrument *minst_p(void);
static u16 st3_period(const Channel *c, u16 raw) {
    u32 c2 = 8363;
    if (c->last_ins) {
        const ModInstrument *ip = &minst_p()[c->last_ins - 1];
        if (ip->c2spd)
            c2 = ip->c2spd;
    }
    u32 p = (u32)raw * 8363u / c2;
    if (p > 65535u)
        p = 65535u;
    if (p < 64u)
        p = 64u;
    return (u16)p;
}

/* ---- 実効ピリオド (ビブラート/アルペジオ適用後) ---- */
static u16 effective_period(Channel *c) {
    u16 p = c->base_period;
    if (c->cmd == CMD_VIBRATO || c->cmd == CMD_VIBVOL) {
        int delta = (c->vib_depth * vib_table[c->vib_pos & 63]) >> 7;
        if (c->vib_pos & 32)
            delta = -delta;
        int np = (int)p + delta;
        if (np < 64)
            np = 64;
        p = (u16)np;
    }
    if (c->cmd == CMD_ARPEGGIO && c->active && c->last_note < 128) {
        int semi = 0;
        switch (tick % 3) {
        case 1: semi = c->info >> 4; break;
        case 2: semi = c->info & 0x0F; break;
        default: break;
        }
        if (semi) {
            int base = (c->last_note >> 4) * 12 + (c->last_note & 15);
            p = st3_period(c, period_of_semitone(base + semi));
        }
    }
    return p;
}

/* ---- ミキサーへの反映 ---- */
static void update_voice(int i) {
    Channel *c = &ch[i];
    MixVoice *v = &mix_voices[i];
    if (!c->active || c->last_ins == 0) {
        v->active = 0;
        return;
    }
    /* 非ループサンプルが最後まで鳴り終わったら、tick ごとの update で
     * 再アクティブ化しない (pos=0 から無限にリピートしてしまう)。 */
    if (v->ended) {
        v->active = 0;
        return;
    }
    const ModInstrument *ip = &minst[c->last_ins - 1];
    if (ip->length == 0) {
        v->active = 0;
        return;
    }
    v->data = mod + ip->data_offset;
    v->length = ip->length;
    v->loop = (ip->flags & 1) && ip->loop_end > ip->loop_start;
    v->loop_start = ip->loop_start;
    v->loop_end = ip->loop_end;
    u32 period = effective_period(c);
    if (period < 64)
        period = 64;
    /* step (16.16) = 8363 * 7181 / period  (period は c2spd を含む ST3 単位。
     * C-4 = 1712 * 8363 / c2spd、mix rate 15625Hz)。 */
    v->step = (8363u * 7181u) / period;
    v->gain = c->cur_vol * 2;
    v->active = 1;
}

static void voice_trigger(int i, u16 period, u32 offset) {
    Channel *c = &ch[i];
    MixVoice *v = &mix_voices[i];
    c->base_period = period;
    c->target_period = period;
    c->active = 1;
    c->retrig_cnt = 0;      /* ST3: ノート開始で Q のカウンタをリセット */
    v->ended = 0;
#ifdef HOST_TRACE
    fprintf(stderr, "note t_ch%d note=%02X ins=%d period=%u tick=%d row=%d ord=%d\n",
            i, c->last_note, c->last_ins, period, tick, np_row, np_ord);
#endif
    update_voice(i);
    v->pos = (u32)offset << 16;
    if (v->length && v->pos >= (v->length << 16))
        v->pos = 0;
}

/* S3M の Dxy (音量スライド)。
 *
 * MUSIC0/MUSIC1 のヘッダは cwtv = 0x1300 (ST3.00) なので、OpenMPT の
 * SONG_FASTVOLSLIDES と同じ規則になる: 通常のスライドも tick 0 を含む
 * 全 tick で適用される。
 *   low nibble != 0 かつ high != 0xF : low 分下げる (D0F は 15)
 *   low == 0xF かつ high != 0        : high 分上げる   (DFF は 15)
 *   low == 0xF かつ high == 0        : 15 下げる
 *   high == 0xF かつ low == 0        : 15 上げる
 *   low == 0, high != 0              : high 分上げる
 *   D00                              : 直前の非ゼロ Dxy (メモリ)
 * Kxy (vibrato+volslide) は ST3 でも tick 0 では動かないので、呼び出し側で
 * tick 0 を渡さない。
 *
 * 以前 tick 0 を除外したため、ST3.00 の曲 (MUSIC1 のフェード等) で
 * 音量が下がりきらず、本来消える小節で音が残っていた。 */
static void apply_vol_slide(Channel *c, int tick) {
    int up = c->vol_slide_mem >> 4;
    int dn = c->vol_slide_mem & 0x0F;
    int nv = c->cur_vol;

    (void)tick;

    if (dn == 0x0F && up != 0)
        nv += up;                       /* DxF: ファインアップ */
    else if (up == 0x0F && dn == 0)
        nv += 15;                       /* DF0: +15 */
    else if (dn)
        nv -= dn;                       /* D0F: -15 / Dxy: y 下げ */
    else if (up)
        nv += up;                       /* Dx0 */
    else
        return;                         /* メモリ無し (D00 で保持) */

    if (nv < 0) nv = 0;
    if (nv > 64) nv = 64;
    c->cur_vol = (u8)nv;
}

/* S3M の Qxy 音量モディファイア (x)。ST3.21 の表。 */
static void apply_retrig_vol(Channel *c) {
    static const s8 add[16] = {
        0, -1, -2, -4, -8, -16, 0, 0, 0, 1, 2, 4, 8, 16, 0, 0
    };
    int v = c->cur_vol;
    switch (c->retrig_vol) {
    case 0x6: v = v * 10 / 16; break;   /* ST3 retrigTable1[6] = 10/16 */
    case 0x7: v = v / 2; break;
    case 0xE: v = v * 3 / 2; break;
    case 0xF: v = v * 2; break;
    default:  v += add[c->retrig_vol]; break;
    }
    if (v < 0) v = 0;
    if (v > 64) v = 64;
    c->cur_vol = (u8)v;
}

/* S3M の Qxy (ST3.00) のリトリガ。
 * ST3 は nRetrigCount を tick ごとに +1 し、
 *   count != 0 && count % y == 0
 * の tick でリトリガする。Qxx の無い行ではカウンタは進まず、
 * 新しいノートが鳴ったときだけ 0 に戻る (NoteChange)。
 * そのため y=3 / speed=3 では「次の行の tick 0」でリトリガされる。
 * 以前は「毎回 0 から数えて y 回目の tick」で鳴らしていたため、
 * リトリガが 1 tick 早く、Qxx の無い行でカウンタが消えていた。 */
static void do_retrig(int i) {
    Channel *c = &ch[i];
    int speed = c->retrig_speed ? c->retrig_speed : 1;
    if (c->retrig_cnt && (c->retrig_cnt % speed) == 0) {
        if (c->active)
            mix_voices[i].pos = 0;
        apply_retrig_vol(c);
    }
    c->retrig_cnt++;                /* u8 (ST3 と同じ 8bit) */
}

/* ---- セル処理 (行頭) ---- */
static void process_cell(int i) {
    Channel *c = &ch[i];
    u8 note = c->note, ins = c->ins, vol = c->vol, cmd = c->cmd, info = c->info;
    u8 note_delay = 0;
    u8 old_vol = c->cur_vol;
    int delayed = 0;

    /* 行頭で遅延ノートをクリアする。速度 >= 遅延値の行では tick が
     * 遅延値に到達せず、古い pending_note が次以降の行で鳴ってしまう。 */
    c->note_delay = 0;

    if (note == MOD_NOTE_CUT) {
        /* S3M の 0xFE は Note Cut。音量 0 にするだけだとループサンプルが
         * 裏で回り続け、後で音量が戻ると鳴り出す。ST3 どおりボイスを止める。 */
        c->cur_vol = 0;
        c->active = 0;
        mix_voices[i].active = 0;
        note = MOD_NOTE_NONE;
    }

    if (ins != 0 && ins <= mh->num_instruments) {
        c->last_ins = ins;
        /* 仕様 (ST3.21 Playback Notes):
         *   Note w/o sample  -> ノートをリトリガ。音量はリセットしない。
         *   Sample w/o note  -> 音量をリセットし、サンプルだけ差し替える
         *                       (ノートはリトリガしない)。
         * つまり楽器番号が書かれたセルは、ノートの有無にかかわらず音量が
         * 楽器の既定音量に戻る。音量カラムがあればそちらが優先される。 */
        if (minst[ins - 1].length)
            c->cur_vol = minst[ins - 1].volume;
    }

    if (vol != MOD_VOL_NONE && vol <= 64)
        c->cur_vol = vol;

    /* コマンド準備 */
    switch (cmd) {
    case CMD_VOLSLIDE:
        if (info)
            c->vol_slide_mem = info;
        apply_vol_slide(c, 0);
        break;
    case CMD_VIBRATO:
        if (info) {
            c->vib_speed = info >> 4;
            c->vib_depth = info & 0x0F;
        }
        break;
    case CMD_VIBVOL:
        /* Kxy = H00 + Dxy。最初の tick はスライドしない (仕様書)。 */
        if (info)
            c->vol_slide_mem = info;
        break;
    case CMD_TONEPORTA:
        if (info)
            c->porta_speed = info;
        break;
    case CMD_PORTADOWN:
    case CMD_PORTAUP: {
        /* Exx/Fxx: 00 は直前の値を使う (ST3 は E と F で記憶を共有)。
         * 以前はこの記憶が無く、E00/F00 (MUSIC0 だけで
         * 約 1800 箇所) で音程が止まり、ポルタメントが上がり切って
         * いなかった (2026-11-15 ユーザー指摘、ENDSCRL の BGM)。 */
        if (info)
            c->ef_mem = info;
        else
            info = c->ef_mem;
        c->info = info;     /* tick 1 以降はこの値で滑らせる */
        /* 細かいスライド (E0..FF) はノート発音の後で処理する (下)。 */
        break;
    }
    case CMD_OFFSET:
        /* S3M の Oxx は xx*256 サンプル */
        c->offset = (u16)info << 8;
        break;
    case CMD_RETRIG:
        c->retrig_vol = info >> 4;
        c->retrig_speed = info & 0x0F;
        break;
    case CMD_SPECIAL:
        if ((info & 0xF0) == 0xD0)
            note_delay = info & 0x0F;
        break;
    case CMD_ZINFO:
        np_zinfo = info;
        break;
    case CMD_SPEED:
        /* S3M 規格: Axx は info<0x20 ならスピード、0x20 以上ならテンポ。
         * MUSIC1 (UnreaL ][) は A93/A49/AF3 などのテンポ命令を使って
         * いるため、全部スピードとして扱うと行長が異常になり、
         * テンポが不安定に聞こえる。 */
        if (info < 0x20) {
            if (info)
                np_speed = info;
        } else {
            np_tempo = info;
        }
        break;
    case CMD_TEMPO:
        /* 仕様: 33 未満は無視 (T0x/T1x は今回適用しない) */
        if (info >= 33)
            np_tempo = info;
        break;
    case CMD_ORDERJUMP:
        force_order = info;
        force_order_valid = 1;
        force_row = 0;
        force_advance = 1;
        break;
    case CMD_PATBREAK:
        force_row = (u8)((info >> 4) * 10 + (info & 0x0F));
        if (force_row > 63)
            force_row = 0;
        force_advance = 1;
        break;
    default:
        break;
    }

    /* ノートトリガ */
    if (note != MOD_NOTE_NONE && note < 128) {
        c->last_note = note;
        u16 period = st3_period(c, note_period[note_to_semitone(note)]);
        if (cmd == CMD_TONEPORTA && mix_voices[i].active) {
            /* トーンポルタメント: サンプルを再スタートせず、目標周期だけ
             * 更新して次の tick から滑らせる (Gxx)。以前は voice_trigger
             * していたため毎回アタックが鳴っていた。 */
            c->target_period = period;
        } else if (note_delay) {
            /* 遅延ノート: 楽器・音量の変更は遅延発音の時点まで適用しない。
             * ここで update_voice すると、鳴っている音のサンプルだけが
             * 先に切り替わって「音が飛ぶ」。 */
            c->note_delay = note_delay;
            c->pending_note = note;
            c->pending_vol = c->cur_vol;
            c->cur_vol = old_vol;
            delayed = 1;
        } else {
            voice_trigger(i, period, (cmd == CMD_OFFSET) ? c->offset : 0);
        }
    } else if (cmd == CMD_OFFSET && c->active) {
        /* オフセットのみ: 現在のノートをオフセットから再開 */
        MixVoice *v = &mix_voices[i];
        v->pos = (u32)c->offset << 16;
        if (v->length && v->pos >= (v->length << 16))
            v->pos = 0;
    }

    /* ファイン/エクストラファイン・ポルタメント (Fxx/EEx/EFx) は
     * 行頭 (tick 0) にだけ小さく適用する。連続スライド扱いにすると
     * 1 行で 1 オクターブ以上動いてしまう。 */
    if ((cmd == CMD_PORTAUP || cmd == CMD_PORTADOWN) && info >= 0xE0) {
        int amt = info & 0x0F;
        if ((info & 0xF0) == 0xF0)
            amt *= 4;               /* fine */
        if (cmd == CMD_PORTAUP) {
            if (c->base_period > amt)
                c->base_period -= amt;
        } else {
            c->base_period += amt;
            if (c->base_period > 32000)
                c->base_period = 32000;
        }
        c->info = 0;                /* tick 側の連続スライドを止める */
    }

    /* Qxy は tick 0 も処理する (仕様書)。新しいノートを鳴らした直後に
     * 音量モディファイアを適用する。 */
    if (cmd == CMD_RETRIG)
        do_retrig(i);

    if (!delayed)
        update_voice(i);
}

/* ---- 毎ティック処理 ---- */
static void process_tick_effects(void) {
    for (int i = 0; i < 8; i++) {
        Channel *c = &ch[i];
        u8 cmd = c->cmd;
        u8 info = c->info;

        /* S Dx 遅延ノートの発音 */
        if (c->note_delay && tick == c->note_delay) {
            u16 period = st3_period(c, note_period[note_to_semitone(c->pending_note)]);
            c->cur_vol = c->pending_vol;
            if (cmd == CMD_TONEPORTA && mix_voices[i].active)
                c->target_period = period;
            else
                voice_trigger(i, period, (cmd == CMD_OFFSET) ? c->offset : 0);
            c->note_delay = 0;
        }

        /* 音量スライド (D / K)。Dxy の細かい規則は apply_vol_slide() 参照。
         * Kxy は tick 0 を処理しない (仕様書) ので、ここ (tick > 0) だけ。 */
        if (cmd == CMD_VOLSLIDE || cmd == CMD_VIBVOL)
            apply_vol_slide(c, tick);

        /* Qxy: リトリガ (音量モディファイア付き) */
        if (cmd == CMD_RETRIG)
            do_retrig(i);

        /* ピッチ効果 */
        switch (cmd) {
        case CMD_PORTADOWN:
            /* S3M のピリオドは ProTracker の 4 倍の単位なので、
             * スライド量も 4 倍にする (ST3 と同じ)。細かいスライド
             * (E0..FF) は tick 0 だけなので、ここでは動かさない。 */
            if (info < 0xE0) {
                c->base_period += (u16)(info << 2);
                if (c->base_period > 32000)
                    c->base_period = 32000;
            }
            break;
        case CMD_PORTAUP:
            if (info < 0xE0) {
                if (c->base_period > (u16)(info << 2))
                    c->base_period -= (u16)(info << 2);
                else
                    c->base_period = 64;
            }
            break;
        case CMD_TONEPORTA: {
            int spd = (int)c->porta_speed << 2;
            if (spd == 0)
                spd = 4;
            if (c->base_period < c->target_period) {
                c->base_period += spd;
                if (c->base_period > c->target_period)
                    c->base_period = c->target_period;
            } else if (c->base_period > c->target_period) {
                if (c->base_period < (u16)(spd + c->target_period))
                    c->base_period = c->target_period;
                else
                    c->base_period -= spd;
            }
            break;
        }
        case CMD_RETRIG:
            /* リトリガは do_retrig() で処理する */
            break;
        default:
            break;
        }

        update_voice(i);

        /* ビブラート位置は適用後に進める */
        if (cmd == CMD_VIBRATO || cmd == CMD_VIBVOL)
            c->vib_pos = (c->vib_pos + c->vib_speed) & 63;
    }
}

/* ---- オーダー進行 (STMIK 0x442d 相当) ---- */
static void advance_order(void) {
    int wrapped = 0;
    np_zplus = 0;
    for (;;) {
        order_index++;
        if (order_index >= mh->num_orders) {
            /* 曲の終端では止める (先頭へ戻らない)。原作の動画では
             * MUSIC1 が JPLOGO の途中 (475.0 s) で終わり、U2E が
             * MUSIC0 を読み直す (U2.ASM:881-886 restartmus) 479.5 s まで
             * 無音だった (2026-11-15 計測)。以前は先頭へ 1 回ループして
             * いたため、JPLOGO の最後に MUSIC1 の頭が鳴っていた。 */
            (void)wrapped;
            for (int i = 0; i < 8; i++)
                mix_voices[i].active = 0;   /* 動画でも完全な無音 (rms 0) */
            playing = 0;
            return;
        }
        u16 pat = orders[order_index];
        u16 next = (order_index + 1 < mh->num_orders) ? orders[order_index + 1] : 0xFF;
        if (next == 0xFE || is_marker((u16)(order_index + 1)))
            np_zplus |= 1;
        if (pat == 0xFE || is_marker(order_index)) {
            np_zplus |= 2;
            if (pat == 0xFE)
                continue;
        }
        if (pat == 0xFF) {
            /* 0xFF はセクション区切り。以前はここで先頭へループしていたため、
             * MUSIC0 がオーダー 16 で最初のセクションを繰り返していた。
             * 次の有効なオーダーへ進む。 */
            continue;
        }
        np_ord = order_index;
        np_pat = pat;
        row = 0;
        tick = 0;
        force_advance = 0;
        force_order_valid = 0;
        if (loop_mode == 2)
            loop_mode = 1;
        return;
    }
}

/* ---- 行処理 (ティック 0) ---- */
static void process_row(void) {
    u32 poff = pat_tbl[np_pat];
    const u8 *p = mod + poff + (u32)row * CELLS_PER_ROW * CELL_SIZE;
    for (int i = 0; i < 8; i++) {
        Channel *c = &ch[i];
        c->note = p[0];
        c->ins = p[1];
        c->vol = p[2];
        c->cmd = p[3];
        c->info = p[4];
        p += CELL_SIZE;
        process_cell(i);
    }
    np_row = row;
}

/* ---- ティック進行 ---- */
void player_tick(void) {
    if (!playing)
        return;
    /* 原作 STMIK.300 のプレイヤー IRQ (0x4358: inc word ptr [0x08FB])
     * と同じく、np_zframe は tick ごとに進む。GLENZ は
     * dis_setmframe(0) からの 300/333 tick を待つ
     * (GLENZ/MAIN.C:353,400)。row ごとに進めると speed 倍
     * (約 3 倍 = 19 秒) になり、最初のオブジェクトが遅すぎた。 */
    np_zframe++;
    if (tick == 0) {
        process_row();
    } else {
        process_tick_effects();
    }
    tick++;
    if (tick >= np_speed) {
        tick = 0;
        if (force_advance) {
            u8 fr = force_row;
            if (force_order_valid) {
                order_index = force_order;
                if (order_index >= mh->num_orders)
                    order_index = 0;
                np_ord = order_index;
                np_pat = orders[order_index];
                row = 0;
                np_row = 0;
                force_advance = 0;
                force_order_valid = 0;
                return;
            }
            force_advance = 0;
            advance_order();
            if (playing) {
                row = fr;
                np_row = row;
            }
            return;
        }
        row++;
        if (row >= 64) {
            advance_order();
        }
        np_row = row;
    }
}

/* ---- STMIK 互換 API ---- */
int zinit(void) { return 1; }

int zinitmodule(const void *module) {
    mod = (const u8 *)module;
    mh = (const ModHeader *)module;
    if (mh->magic != MOD_MAGIC)
        return 0;
    marker_list = 0;
    marker_count = 0;
    for (u16 i = 0; i < marker_set_count; i++) {
        if (marker_sets[i].module == module) {
            marker_list = marker_sets[i].list;
            marker_count = marker_sets[i].count;
            break;
        }
    }
    orders = (const u8 *)(mod + mh->order_list_off);
    minst = (const ModInstrument *)(mod + mh->instruments_off);
    pat_tbl = (const u32 *)(mod + mh->patterns_tbl_off);
    setup_vib();
    return 1;
}

int zsoundon(void) { return 1; }
int zsoundoff(void) { return 1; }

int zplaysong(int order) {
    for (int i = 0; i < 8; i++) {
        ch[i].active = 0;
        ch[i].cur_vol = 0;
        ch[i].last_ins = 0;
        ch[i].vol_slide_mem = 0;
        ch[i].retrig_cnt = 0;
        ch[i].retrig_speed = 0;
        mix_voices[i].active = 0;
    }
    np_speed = mh->init_speed;
    np_tempo = mh->init_tempo;
    np_zinfo = 0;
    np_zplus = 0;
    np_zframe = 0;
    order_index = (u16)order & 0x1FF;
    if (order_index >= mh->num_orders)
        order_index = 0;
    order_index--;      /* advance_order が +1 する */
    loop_order = 0;
    loop_mode = 2;
    playing = 1;
    force_advance = 0;
    force_order_valid = 0;
    advance_order();
    return 1;
}

int zgotosong(int order, int r) {
    np_speed = mh->init_speed;
    np_tempo = mh->init_tempo;
    np_zplus = 0;
    order_index = (u16)order & 0x1FF;
    if (order_index >= mh->num_orders)
        order_index = 0;
    np_ord = order_index;
    np_pat = orders[order_index];
    row = (u8)r & 0x3F;
    tick = 0;
    force_advance = 0;
    force_order_valid = 0;
    loop_mode = 2;
    playing = 1;
    np_row = row;
    return 1;
}

int zstopsong(void) {
    loop_mode = 0;
    playing = 0;
    return 1;
}

int zshutup(void) {
    for (int i = 0; i < 8; i++) {
        ch[i].active = 0;
        mix_voices[i].active = 0;
    }
    return 1;
}

void zpollme(void) { player_tick(); }

/* ---- デバッグ用アクセサ ---- */
int player_playing(void) { return playing; }
int player_tickno(void) { return tick; }
