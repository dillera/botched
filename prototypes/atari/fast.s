; AX pointer -> little-endian u32 in sreg:AX, the existing cc65 return ABI.
; No C argument stack, no shifts, 4 indexed loads. Foreground only like NIO.
.export _th_u32
.importzp ptr1, sreg
.code
_th_u32:
    sta ptr1
    stx ptr1+1
    ldy #3
    lda (ptr1),y
    sta sreg+1
    dey
    lda (ptr1),y
    sta sreg
    dey
    lda (ptr1),y
    tax
    dey
    lda (ptr1),y
    rts

; The C/memcmp scan dominated the full 100-crate event batch. Keep the compact
; seven-byte entries and scan their IDs directly; no extra 320-byte tile index.
.export _th_find
.import _th_w
.importzp ptr2
_th_find:
    sta ptr1
    stx ptr1+1
    lda #<(_th_w+423)
    sta ptr2
    lda #>(_th_w+423)
    sta ptr2+1
    ldx _th_w+1288
    beq @missing
@entry:
    ldy #0
@byte:
    lda (ptr1),y
    cmp (ptr2),y
    bne @next
    iny
    cpy #4
    bne @byte
    lda ptr2
    ldx ptr2+1
    rts
@next:
    clc
    lda ptr2
    adc #7
    sta ptr2
    bcc :+
    inc ptr2+1
:
    dex
    bne @entry
@missing:
    lda #0
    tax
    rts
