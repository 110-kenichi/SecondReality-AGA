#include "input.h"
#include "framework.h"
#include <hardware/custom.h>

extern volatile struct Custom *custom;

/* CIA-A PRA (0xBFE001): bit6 = 左マウスボタン (0 で押下) */
#define CIAAPRA (*(volatile u8 *)0xBFE001)

static int last_right;

void input_init(void) {
    /* 起動時に既に押されていても「押した瞬間」と誤検出しないよう、
     * 現在の状態を初期値にする。 */
    last_right = !(custom->potinp & (1 << 10));
}

int input_exit_pressed(void) {
    /* 左ボタン: CIA-A PRA bit6 (0 で押下) */
    return !(CIAAPRA & (1 << 6));
}

int input_next_pressed(void) {
    int r = !(custom->potinp & (1 << 10));
    int edge = r && !last_right;
    last_right = r;
    return edge;
}
