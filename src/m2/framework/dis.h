#ifndef DIS_H
#define DIS_H

#include "framework.h"

/*
 * DIS (Demo Interrupt Server) 互換 API
 *
 * オリジナル: reference/SecondReality/DIS/ (DIS.H / DISINT.ASM / DISC.ASM, V1.0)
 * パート側のコードをほぼそのまま使えるようにするための互換レイヤ。
 *
 * オリジナルとの対応:
 *   dis_muscode(code) → AX = np_zinfo        (DISC.ASM _dis_muscode)
 *   dis_musplus()     → DX = 次のマーカーまでの行数 (DISC.ASM _dis_musplus)
 *   dis_musrow()      → BX = np_row          (DISC.ASM _dis_musrow)
 *   dis_sync()        → AX = パート番号      (ordersync1 テーブル)
 */

int dis_version(void);       /* 0x100 (V1.0) を返し、状態を初期化 */
int dis_indemo(void);        /* 1 = デモ実行中 */
int dis_waitb(void);         /* VBL 待ち。前回からの経過フレーム数を返す */
int dis_exit(void);          /* 1 = パート終了要求 (左マウスボタン) */
void dis_partstart(void);    /* dis_version を呼ぶ (互換用) */
void *dis_msgarea(int n);    /* 64 バイトの共用エリア (n = 0..3) */
int dis_muscode(int code);   /* np_zinfo */
int dis_musplus(void);       /* 次のマーカーまでの符号付き行数 (負 = まだ) */
/* 原作の「musplus が lo より大きく 0 未満になったら」の待ちを、描画が遅くて
 * その窓を丸ごと見逃しても通るようにしたもの。窓の手前 (<= lo) を一度見て
 * いれば、窓を過ぎて 0 以上になっていても 1 を返す。armed はパートの
 * 開始時に 0 にしておく。 */
int dis_mus_reached(int *armed, int lo);
int dis_musrow(void);        /* np_row */
void dis_setcopper(int n, void (*routine)(void));
                             /* 1=表示開始後 2=帰線前 3=帰線中 (現状 3 のみ VBL から呼ぶ) */
void dis_setmframe(int frame);
int dis_getmframe(void);
int dis_sync(void);          /* (np_ord, np_row) → パート番号 (ordersync1) */

/* VBL ハンドラから呼ばれる (DIS の帰線コールバック) */
void dis_copper_tick(void);

#endif
