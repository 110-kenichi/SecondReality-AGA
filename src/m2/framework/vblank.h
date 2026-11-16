#ifndef VBLANK_H
#define VBLANK_H

#include "framework.h"

/* VBlank (VERTB) 割り込み初期化。経過フレーム数のカウントを開始 */
void vblank_init(void);
void vblank_shutdown(void);

/* 次のフレームまで待ち、経過フレーム数を返す (オリジナル DIS waitb 相当) */
u32 vblank_wait(void);

/* VBlank 割り込みが発行したフレーム番号 (自由走行) */
u32 vblank_frames(void);

/* VPOS ポーリングによる垂直帰線待ち (割り込み初期化前でも使える) */
void wait_vbl_poll(void);

#endif
