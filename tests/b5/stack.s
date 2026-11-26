; Measurement only: paint unused cc65 software stack and hardware stack.
.export _b5_mark, _b5_used, _b5_hw_used
.importzp c_sp, ptr1
.bss
top: .res 2
hwtop: .res 1
.code
_b5_mark:
    lda c_sp
    sta top
    sta ptr1
    lda c_sp+1
    sta top+1
    sec
    sbc #8
    sta ptr1+1
    ldx #8
    ldy #0
    lda #$a5
@page:
    sta (ptr1),y
    iny
    bne @page
    inc ptr1+1
    dex
    bne @page
    tsx
    stx hwtop
@hw:
    sta $100,x
    dex
    cpx #$ff
    bne @hw
    rts
_b5_used:
    lda top
    sta ptr1
    lda top+1
    sec
    sbc #8
    sta ptr1+1
    ldy #0
@scan:
    lda (ptr1),y
    cmp #$a5
    bne @found
    inc ptr1
    bne @check
    inc ptr1+1
@check:
    lda ptr1
    cmp top
    bne @scan
    lda ptr1+1
    cmp top+1
    bne @scan
@found:
    sec
    lda top
    sbc ptr1
    pha
    lda top+1
    sbc ptr1+1
    tax
    pla
    rts
_b5_hw_used:
    ldx #0
@scan:
    lda $100,x
    cmp #$a5
    bne @found
    cpx hwtop
    beq @found
    inx
    bne @scan
@found:
    txa
    sta ptr1
    lda #0
    sec
    sbc ptr1
    ldx #0
    cmp #0
    bne @done
    inx                     ; all 256 bytes used
@done:
    rts
