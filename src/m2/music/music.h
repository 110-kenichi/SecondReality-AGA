#ifndef MUSIC_H
#define MUSIC_H

#include "../framework/framework.h"

/* ミキサーが扱う 8 ボイス (S3M チャンネル) */
typedef struct {
    const u8 *data;     /* 8bit signed サンプル */
    u32 pos;            /* 16.16 固定小数点 */
    u32 step;           /* 16.16 固定小数点 */
    u32 length;
    u32 loop_start;
    u32 loop_end;
    u8 loop;
    u8 active;
    u8 ended;           /* 非ループサンプルが最後まで鳴り終わった */
    u8 gain;            /* 0-128 */
} MixVoice;

extern MixVoice mix_voices[8];

/* STMIK 互換 API */
int zinit(void);
int zinitmodule(const void *module);
int zsoundon(void);
int zsoundoff(void);
int zplaysong(int order);
int zgotosong(int order, int row);
int zstopsong(void);
/* std 版 S3M で削除された 0xFE マーカーの位置を登録する。
 * list は「0xFE の直後のオーダーの std 添字」の配列。 */
void zsetmarkers(const void *module, const u16 *list, u16 count);
int zshutup(void);
void zpollme(void);
void player_tick(void);

/* np_* 変数 */
extern u16 np_ord, np_pat, np_row, np_speed, np_tempo, np_loop;
extern u16 np_zinfo, np_zplus, np_zframe;
extern u16 np_mastervol, np_mastermul, np_masterflags;

/* デバッグ用アクセサ */
int player_playing(void);
int player_tickno(void);

/* オーディオ (Paula 4ch) */
int audio_init(void);
void audio_frame(int frames);
/* テンポ倍率 (パーセント, 100 = 原本どおり) */
void audio_set_tempo_pct(u32 pct);

/* 直前フレームのピーク振幅 (0-127, デバッグ表示用) */
int audio_peak(void);
void audio_shutdown(void);

#endif
