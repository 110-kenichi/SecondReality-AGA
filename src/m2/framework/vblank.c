#include "vblank.h"
#include "dis.h"
#include <proto/exec.h>
#include <exec/interrupts.h>
#include <hardware/custom.h>
#include <hardware/intbits.h>

extern volatile struct Custom *custom;
extern void audio_vbl(void);     /* music/audio.c: VBL 同期の DMA 切替 */

static volatile u32 frame_counter;
static struct Interrupt vblank_isr;

/*
 * VERTB 割り込みハンドラ。
 * exec の割り込みサーバとして登録するため、通常の C 関数
 * (interrupt 属性は不要。exec 側が rte を行う)。
 */
static void vblank_handler(void) {
    custom->intreq = 1 << INTB_VERTB;
    custom->intreq = 1 << INTB_VERTB; /* A4000 対策で 2 回 */
    frame_counter++;
    dis_copper_tick();               /* DIS の帰線コールバック */
    audio_vbl();                     /* 2 VBL ごとに Paula バッファを切替 */
}

void vblank_init(void) {
    vblank_isr.is_Node.ln_Type = NT_INTERRUPT;
    vblank_isr.is_Node.ln_Pri = 0;
    vblank_isr.is_Node.ln_Name = (char *)"m1 vblank";
    vblank_isr.is_Data = 0;
    vblank_isr.is_Code = (VOID (*)())vblank_handler;

    AddIntServer(INTB_VERTB, &vblank_isr);
    custom->intreq = 1 << INTB_VERTB;
    custom->intena = INTF_SETCLR | INTF_INTEN | INTF_VERTB;
}

void vblank_shutdown(void) {
    custom->intena = INTF_VERTB; /* VERTB 停止 */
    RemIntServer(INTB_VERTB, &vblank_isr);
}

u32 vblank_wait(void) {
    u32 start = frame_counter;
    while (frame_counter == start) { }
    return frame_counter - start;
}

u32 vblank_frames(void) { return frame_counter; }

void wait_vbl_poll(void) {
    /*
     * VPOS の折り返し (値が減る瞬間) をフレーム境界とみなす。
     * PAL/NTSC どちらの表示モードでも動作する。
     */
    u32 prev;
    u32 v;
    prev = *(volatile u32 *)0xDFF004;
    prev = ((prev & 1) << 8) | ((prev >> 8) & 0xFF);
    for (;;) {
        v = *(volatile u32 *)0xDFF004;
        v = ((v & 1) << 8) | ((v >> 8) & 0xFF);
        if (v < prev)
            break;
        prev = v;
    }
}
