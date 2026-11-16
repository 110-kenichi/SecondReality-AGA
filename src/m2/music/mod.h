#ifndef MOD_H
#define MOD_H

/*
 * Second Reality A1200: Amiga モジュール形式 (.srm)
 * tools/s3m_convert.py が標準 S3M から生成する。
 * すべてリトルエンディアン。
 */
#include "../framework/framework.h"

#define MOD_MAGIC 0x53524D31u  /* 'SRM1' (ビッグエンディアン読み) */

typedef struct {
    u32 magic;
    u16 num_orders;
    u16 num_instruments;
    u16 num_patterns;
    u8 num_channels;
    u8 init_speed;
    u8 init_tempo;
    u8 mastervol;
    u16 pad;
    u32 order_list_off;     /* u16[num_orders] */
    u32 instruments_off;    /* ModInstrument[num_instruments] */
    u32 patterns_tbl_off;   /* u32[num_patterns] (絶対オフセット) */
    u32 samples_off;        /* サンプルデータ先頭 */
} ModHeader;                /* 32 バイト */

typedef struct {
    char name[28];
    u8 volume;
    u8 flags;               /* bit0: ループ */
    u16 c2spd;              /* C-4 再生周波数 */
    u32 length;
    u32 loop_start;
    u32 loop_end;
    u32 data_offset;        /* モジュール先頭からの絶対オフセット */
} ModInstrument;

/* 展開済みセル: 5 バイト (note, ins, vol, cmd, info) */
#define CELL_SIZE 5
#define CELLS_PER_ROW 8
#define PATTERN_SIZE (64 * CELLS_PER_ROW * CELL_SIZE)

#define MOD_NOTE_NONE 0xFF
#define MOD_NOTE_CUT  0xFE
#define MOD_VOL_NONE  0xFF
#define MOD_INS_NONE  0x00

/* S3M コマンド */
#define CMD_NONE 0
#define CMD_SPEED 1      /* A */
#define CMD_ORDERJUMP 2  /* B */
#define CMD_PATBREAK 3   /* C */
#define CMD_VOLSLIDE 4   /* D */
#define CMD_PORTADOWN 5  /* E */
#define CMD_PORTAUP 6    /* F */
#define CMD_TONEPORTA 7  /* G */
#define CMD_VIBRATO 8    /* H */
#define CMD_ARPEGGIO 10  /* J */
#define CMD_VIBVOL 11    /* K */
#define CMD_OFFSET 15    /* O */
#define CMD_RETRIG 17    /* Q */
#define CMD_SPECIAL 19   /* S */
#define CMD_TEMPO 20     /* T */
#define CMD_ZINFO 26     /* Z (STMIK 拡張: np_zinfo = info) */

#endif
