#ifndef INPUT_H
#define INPUT_H

/* 入力初期化 */
void input_init(void);

/* 左マウスボタン: デモ終了要求 */
int input_exit_pressed(void);

/* 右マウスボタン: シーン送り (押した瞬間のみ 1) */
int input_next_pressed(void);

#endif
