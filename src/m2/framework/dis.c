#include "dis.h"
#include "vblank.h"
#include "../music/music.h"
#include <hardware/custom.h>

extern volatile struct Custom *custom;

/* ---- 状態 ---- */
static u8 exit_flag;
static u8 msg_areas[4][64];
static void (*copper_routines[3])(void);

/* ---- パート遷移テーブル (オリジナル DISINT.ASM の ordersync1 をそのまま使用)
 * { (order << 8) | row, パート番号 } — MUSIC0 用のデータ */
typedef struct {
    u16 orderrow;
    u16 part;
} OrderSync;

static const OrderSync ordersync1[] = {
    { 0x0000, 0 },
    { 0x0200, 1 },
    { 0x0300, 2 },
    { 0x032f, 3 },
    { 0x042f, 4 },
    { 0x052f, 5 },
    { 0x062f, 6 },
    { 0x072f, 7 },
    { 0x082f, 8 },
    { 0x0900, 9 },
    { 0x0d00, 10 },
    { 0x3d00, 1 },
    { 0x3f00, 2 },
    { 0x4100, 3 },
    { 0x4200, 4 },
};
#define ORDERSYNC_COUNT (int)(sizeof(ordersync1) / sizeof(ordersync1[0]))

/* ---- サービス ---- */

int dis_version(void) {
    /* オリジナル version_0: indemo 設定 + exitflag/passmuscode クリア */
    exit_flag = 0;
    return 0x100;
}

int dis_indemo(void) {
    return 1;
}

int dis_waitb(void) {
    /* オリジナル waitb_1: ボーダー待ち + 前回からの経過フレーム数を返す */
    return (int)vblank_wait();
}

int dis_exit(void) {
    /* 左マウスボタン (CIA-A PRA bit6, 0 で押下) を終了キーとする */
    if (!(*(volatile u8 *)0xBFE001 & (1 << 6)))
        exit_flag = 1;
    return exit_flag;
}

void dis_partstart(void) {
    dis_version();
}

void *dis_msgarea(int n) {
    return msg_areas[n & 3];
}

int dis_muscode(int code) {
    (void)code;                 /* オリジナルは待っているコードを渡す (スキップ用) */
    return np_zinfo;
}

int dis_musrow(void) {
    return np_row;
}

int dis_musplus(void) {
    /* オリジナル muscode_6 の DX 計算 (np_zplus と np_row から) */
    int bx = np_row;
    int dx;
    if (np_zplus == 0) {
        dx = -32;
    } else if (np_zplus == 1) {
        dx = bx - 64;
        if (dx < -32)
            dx = -32;
    } else if (np_zplus == 2) {
        dx = bx;
        if (dx >= 32)
            dx = -32;
    } else {
        if (bx > 32)
            dx = bx - 64;
        else
            dx = bx;
        if (dx >= 32 || dx < -32)
            dx = -32;
    }
    return dx;
}

void dis_setcopper(int n, void (*routine)(void)) {
    if (n >= 1 && n <= 3)
        copper_routines[n - 1] = routine;
}

void dis_copper_tick(void) {
    /* 現状は帰線中 (スロット 3) のみ VBL から呼ぶ */
    if (copper_routines[2])
        copper_routines[2]();
}

void dis_setmframe(int frame) {
    np_zframe = (u16)frame;
}

int dis_getmframe(void) {
    return np_zframe;
}

int dis_sync(void) {
    /* 原作 DIS/DISINT.ASM:449-462 の sync_10 と同じ:
     *   dx <= ordersync1[i] なら 1 つ前のエントリのパート番号を返す。
     * つまりパート N は「エントリ N の order/row を過ぎた次」から始まる。
     * 以前は境界でそのパート番号を返していたため、ALKU のテキストが
     * 1 行 (MUSIC0 で約 125ms) 早く出ていた。 */
    u16 dx = (u16)((np_ord << 8) | (np_row & 0xFF));
    for (int i = 0; i < ORDERSYNC_COUNT; i++) {
        if (dx <= ordersync1[i].orderrow)
            return (i == 0) ? 0 : (int)ordersync1[i - 1].part;
    }
    return (int)ordersync1[ORDERSYNC_COUNT - 1].part;
}

int dis_mus_reached(int *armed, int lo) {
    int a = dis_musplus();
    if (a <= lo) {
        *armed = 1;
        return 0;
    }
    if (a < 0)
        return 1;
    return *armed;
}
