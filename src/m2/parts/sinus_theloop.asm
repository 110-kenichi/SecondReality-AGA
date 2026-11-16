;-------------------------------------------------------------------------------
; SINUS (3D-Sinusfield) voxel ray march -- 68k version of COMAN/THELOOP.INC
;
; The original x86 loop keeps "eax = (fraction<<16) | height" and uses add/adc
; pairs.  The 68k X flag maps directly onto that carry chain, so this is a
; compact loop over the 128-step block table instead of the 5200-line C
; translation of the unrolled code.
;
; Entry state (C globals, set by sinus.c):
;   tl_vbuf   frame buffer (64K, 16-bit offsets wrap)
;   tl_wave1  wave table X (32768 u16)
;   tl_wave2  wave table Y
;   tl_si tl_di   wave positions
;   tl_cx tl_dy   per-step increments (already masked to even)
;   tl_bp     bottom of the current column
;
;   d0.l eax   d1.l ecx   d2.w bx   d3.w colour
;   d4.w si    d5.w di    d6.l scratch   d7.w bp
;   a0 vbuf    a1 wave1   a2 wave2   a3 block   a5 end of blocks
;   a6 frame: 0=xs1 2=ys1 4=xs2 6=ys2
;-------------------------------------------------------------------------------

        section .text,code
        xdef    sinus_theloop_run
        xref    tl_vbuf,tl_wave1,tl_wave2
        xref    tl_si,tl_di,tl_cx,tl_dy,tl_bp
        xref    tl_blocks
        cnop    0,4

sinus_theloop_run:
        movem.l d0-d7/a0-a6,-(sp)
        link    a6,#-8

        move.l  tl_vbuf,a0
        move.l  tl_wave1,a1
        move.l  tl_wave2,a2
        lea     tl_blocks,a3
        lea     tl_blocks+1536,a5       ; 128 blocks * 12 bytes

        moveq   #0,d0
        move.l  #$EC00FFFA,d1
        move.w  tl_si,d4
        move.w  tl_di,d5
        move.w  tl_bp,d7

        move.w  tl_cx,d6
        move.w  d6,(a6)                 ; xs1
        add.w   d6,d6
        move.w  d6,4(a6)                ; xs2
        move.w  tl_dy,d6
        move.w  d6,2(a6)                ; ys1
        add.w   d6,d6
        move.w  d6,6(a6)                ; ys2

blk:
        tst.w   8(a3)
        beq   m2
        move.w  (a6),d6
        add.w   d6,d4
        move.w  2(a6),d6
        add.w   d6,d5
        bra   mok
m2:
        move.w  4(a6),d6
        add.w   d6,d4
        move.w  6(a6),d6
        add.w   d6,d5
mok:
        move.l  d4,d6
        lsr.l   #1,d6
        and.l   #$7FFF,d6
        move.w  (a1,d6.w*2),d2
        move.l  d5,d6
        lsr.l   #1,d6
        and.l   #$7FFF,d6
        add.w   (a2,d6.w*2),d2
        add.w   4(a3),d2                ; bx_add

        cmp.w   d2,d0
        bge   seeko

        move.w  d2,d3
        add.w   6(a3),d3                ; dx_add
        and.w   #$00FF,d3
        lsr.w   #1,d3

hit:
        add.l   (a3),d0
        moveq   #0,d6
        addx.w  d6,d0                   ; adc ax,0
        move.l  d7,d6
        and.l   #$FFFF,d6
        move.b  d3,(a0,d6.l)

        moveq   #0,d6
        cmp.w   d2,d0
        scs.b   d6
        bge   seek1a

        add.l   (a3),d0
        moveq   #0,d6
        addx.w  d6,d0
        move.l  d7,d6
        sub.l   #160,d6
        and.l   #$FFFF,d6
        move.b  d3,(a0,d6.l)

        moveq   #0,d6
        cmp.w   d2,d0
        scs.b   d6
        bge   seek2a

        add.l   (a3),d0
        moveq   #0,d6
        addx.w  d6,d0
        move.l  d7,d6
        sub.l   #320,d6
        and.l   #$FFFF,d6
        move.b  d3,(a0,d6.l)

        add.l   #$1E000000,d1
        moveq   #0,d6
        addx.w  d6,d1

        sub.w   #480,d7
        cmp.w   d2,d0
        bge   seeko
        bra   hit

; the CF from the cmp is the carry-in of the following adc
seek1a:
        and.b   #$10,d6
        move    d6,ccr
        move.l  #$0A000000,d6
        addx.l  d6,d1                   ; adc ecx,0x0A000000
        moveq   #0,d6
        addx.w  d6,d1                   ; adc cx,0
        sub.w   #160,d7
        bra   seeko

seek2a:
        sub.w   #160,d7
        move.l  #$0A000000,d6
        add.l   #$0A000000,d1
        addx.l  d6,d1
        moveq   #0,d6
        addx.w  d6,d1
        sub.w   #160,d7

seeko:
        moveq   #-1,d6
        cmpi.w  #1,8(a3)
        beq   seeko1
        add.l   d1,d0                   ; mode 2 advances ecx twice
        addx.l  d1,d0
        addx.w  d6,d0                   ; adc ax,-1
        bra   next
seeko1:
        add.l   d1,d0
        addx.w  d6,d0

next:
        lea     12(a3),a3
        cmpa.l  a5,a3
        bne   blk

        move.w  d7,tl_bp
        move.w  d4,tl_si
        move.w  d5,tl_di

        unlk    a6
        movem.l (sp)+,d0-d7/a0-a6
        rts
