;*******************************************************************************
; X816 shims for the small subset of X16 KERNAL behavior needed before the
; editor has been moved to the native X816 ABI.
;*******************************************************************************

;KEY_SPECIAL, KEY_CTRL, KEY_ALT and the kernel entry addresses all come from
;the contract include pulled in by common.inc. They used to be hand-copied
;here, and the duplicate was a live drift hazard: the console layer's key
;classes were wrong in two places at once once already (see console.h).

X816_KEY_INS   = 75
X816_KEY_LEFT  = 79
X816_KEY_HOME  = 80
X816_KEY_END   = 81
X816_KEY_UP    = 83
X816_KEY_DOWN  = 84
X816_KEY_PGUP  = 85
X816_KEY_PGDN  = 86
X816_KEY_RIGHT = 89
X816_KEY_F1    = 112
X816_KEY_F2    = 113
X816_KEY_F3    = 114
X816_KEY_F4    = 115
X816_KEY_F5    = 116
X816_KEY_F6    = 117
X816_KEY_F7    = 118
X816_KEY_F8    = 119

.proc x816_kernal_getin
    rep #$20
    .a16
    jsl X816_CALL_CON_GETKEY
    sta x816_key_event
    beq none

    and #KEY_CTRL
    beq not_ctrl
    sep #$20
    .a8
    lda #KBD_MODIFIER_CTRL
    sta scancode_modifiers
    bra classify

not_ctrl:
    sep #$20
    .a8
    stz scancode_modifiers

classify:
    rep #$20
    .a16
    lda x816_key_event
    and #KEY_SPECIAL
    bne special

    sep #$20
    .a8
    lda x816_key_event
    cmp #'x'
    bne :+
    ldy x816_smoke_exit
    beq :+
    stz x816_smoke_exit
    lda #KBD_MODIFIER_CTRL
    sta scancode_modifiers
    lda #'x'
:   
    rts

special:
    lda x816_key_event
    and #$00ff
    cmp #X816_KEY_LEFT
    beq left
    cmp #X816_KEY_RIGHT
    beq right
    cmp #X816_KEY_UP
    beq up
    cmp #X816_KEY_DOWN
    beq down
    cmp #X816_KEY_HOME
    beq home
    cmp #X816_KEY_END
    beq end
    cmp #X816_KEY_PGUP
    beq pgup
    cmp #X816_KEY_PGDN
    beq pgdn
    cmp #X816_KEY_INS
    beq ins
    cmp #X816_KEY_F1
    beq f1
    cmp #X816_KEY_F2
    beq f2
    cmp #X816_KEY_F3
    beq f3
    cmp #X816_KEY_F4
    beq f4
    cmp #X816_KEY_F5
    beq f5
    cmp #X816_KEY_F6
    beq f6
    cmp #X816_KEY_F7
    beq f7
    cmp #X816_KEY_F8
    beq f8

none:
    sep #$20
    .a8
    lda #0
    rts

left:
    sep #$20
    .a8
    lda #KEYVAL_LEFT
    rts
right:
    sep #$20
    .a8
    lda #KEYVAL_RIGHT
    rts
up:
    sep #$20
    .a8
    lda #KEYVAL_UP
    rts
down:
    sep #$20
    .a8
    lda #KEYVAL_DOWN
    rts
home:
    sep #$20
    .a8
    lda #KEYVAL_HOME
    rts
end:
    sep #$20
    .a8
    lda #KEYVAL_END
    rts
pgup:
    sep #$20
    .a8
    lda #KEYVAL_PGUP
    rts
pgdn:
    sep #$20
    .a8
    lda #KEYVAL_PGDN
    rts
ins:
    sep #$20
    .a8
    lda #KEYVAL_INSERT
    rts
f1:
    sep #$20
    .a8
    lda #KEYVAL_F1
    rts
f2:
    sep #$20
    .a8
    lda #KEYVAL_F2
    rts
f3:
    sep #$20
    .a8
    lda #KEYVAL_F3
    rts
f4:
    sep #$20
    .a8
    lda #KEYVAL_F4
    rts
f5:
    sep #$20
    .a8
    lda #KEYVAL_F5
    rts
f6:
    sep #$20
    .a8
    lda #KEYVAL_F6
    rts
f7:
    sep #$20
    .a8
    lda #KEYVAL_F7
    rts
f8:
    sep #$20
    .a8
    lda #KEYVAL_F8
    rts
.endproc

.proc x816_kernal_chrout
    cmp #147
    beq cls
    rep #$20
    .a16
    and #$00ff
    jsl X816_CALL_CON_PUTC
    sep #$20
    .a8
    clc
    rts
cls:
    rep #$20
    .a16
    jsl X816_CALL_CON_CLS
    sep #$20
    .a8
    clc
    rts
.endproc

.proc x816_kernal_screen_mode
    ldx #SCREEN_WIDTH
    ldy #SCREEN_HEIGHT
    clc
    rts
.endproc

.proc x816_kernal_memtop
    lda #0
    clc
    rts
.endproc

.proc x816_kernal_notfound
    lda #2
    sec
    rts
.endproc

.proc x816_kernal_noop
    lda #0
    clc
    rts
.endproc

.proc x816_smoke_delay
    lda #40
    sta x816_smoke_delay_count
outer:
    ldx #0
mid:
    ldy #0
inner:
    dey
    bne inner
    dex
    bne mid
    dec x816_smoke_delay_count
    bne outer
    rts
.endproc

.segment "VARS"
    x816_key_event: .res 2
    x816_smoke_delay_count: .res 1
.CODE
