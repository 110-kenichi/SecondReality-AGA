;-------------------------------------------------------------------------------
; __muldi3: 64 x 64 -> 64 bit multiply (libgcc helper; we link with -nostdlib).
;
; -m68020-60 keeps gcc from using the 32x32->64 MULS.L/MULU.L Dh:Dl forms,
; which the 68060 does not implement (they trap into 68060.library and are
; very slow).  gcc then calls __muldi3 for "long long" products instead.
; Only 16x16 MULU.W and 32-bit adds are used here, so it runs on 020..060.
;
; In:  4(sp)=a.hi 8(sp)=a.lo 12(sp)=b.hi 16(sp)=b.lo
; Out: d0=hi d1=lo
;-------------------------------------------------------------------------------
        section .text,code
        xdef    __muldi3
        cnop    0,4
__muldi3:
        movem.l d2-d5,-(sp)
        ; 16 bytes saved -> args at 20(sp)
        move.l  20+4(sp),d2             ; a.lo
        move.l  20+12(sp),d3            ; b.lo
        ; hi part from cross terms: a.hi*b.lo + a.lo*b.hi (low 32 bits only)
        move.l  20+0(sp),d0             ; a.hi
        move.l  d3,d1
        bsr.s   mul32lo                 ; d0 = a.hi*b.lo (mod 2^32)
        move.l  d0,d5
        move.l  20+8(sp),d0             ; b.hi
        move.l  d2,d1
        bsr.s   mul32lo                 ; d0 = b.hi*a.lo
        add.l   d0,d5
        ; full a.lo * b.lo
        move.w  d2,d0
        mulu.w  d3,d0                   ; p00 = a0*b0
        move.l  d2,d1
        swap    d1
        mulu.w  d3,d1                   ; p10 = a1*b0
        move.l  d3,d4
        swap    d4
        mulu.w  d2,d4                   ; p01 = a0*b1
        swap    d2
        swap    d3
        mulu.w  d3,d2                   ; p11 = a1*b1
        ; mid = p10 + p01 (33 bits)
        moveq   #0,d3
        add.l   d4,d1
        addx.l  d3,d3                   ; carry of mid -> d3 (0/1)
        swap    d3                      ; carry * 2^16
        ; lo = p00 + (mid << 16) ; hi += (mid >> 16) + carry
        move.l  d1,d4
        swap    d4
        clr.w   d4                      ; mid << 16
        add.l   d4,d0                   ; lo
        swap    d1
        and.l   #$FFFF,d1               ; mid >> 16
        addx.l  d1,d2                   ; p11 + (mid>>16) + carry(lo)
        add.l   d3,d2
        add.l   d5,d2                   ; + cross terms
        move.l  d0,d1                   ; d1 = lo
        move.l  d2,d0                   ; d0 = hi
        movem.l (sp)+,d2-d5
        rts

; d0 = (d0 * d1) mod 2^32, uses 16-bit multiplies only. d1 is kept.
mul32lo:
        movem.l d6-d7,-(sp)
        move.l  d0,d6
        swap    d6
        mulu.w  d1,d6                   ; a1*b0
        move.l  d1,d7
        swap    d7
        mulu.w  d0,d7                   ; b1*a0
        add.w   d7,d6
        swap    d6
        clr.w   d6                      ; (a1*b0 + a0*b1) << 16
        mulu.w  d1,d0                   ; a0*b0
        add.l   d6,d0
        movem.l (sp)+,d6-d7
        rts
