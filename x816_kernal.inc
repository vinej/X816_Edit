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
    ;REP #$30, NOT #$20: the kernel ABI is M=0/X=0, sixteen bits in BOTH the
    ;accumulator and the index registers, and the index half is not optional.
    ;runtime/smc.s's smc_getkey_raw opens with `phy` `phx` and only THEN does
    ;`sep #0x30` for the I2C bit-banging, so those two pushes take the CALLER's
    ;width -- one byte each if we arrive 8-bit -- while the matching `plx` `ply`
    ;at the end run after `rep #0x30` and take two. The stack unwinds two bytes
    ;short, the closing `rtl` reads its return address from the wrong place, and
    ;the machine executes whatever is there. Observed exactly that: the editor
    ;drew its screen, polled the keyboard once, and ran away into $00:61F1 with
    ;interrupts off -- no typing, no exit, from the console, SuperBasic and
    ;durexForth alike, because all three arrive through this one shim.
    ;
    ;The index goes back to 8 bits immediately after the call rather than at
    ;each exit: this routine has fifteen of them and they all narrow the
    ;accumulator only.
    rep #$30
    .a16
    .i16
    jsl X816_CALL_CON_GETKEY
    sep #$10
    .i8
    sta x816_key_event

    ;CMP #0, and it is not redundant. `sta` sets no flags, so the `beq` below
    ;used to test whatever Z the kernel call left -- and KLEAVE ends `pld` /
    ;`plb` / `rtl`, so that Z is "the CALLER's data bank register is zero".
    ;The editor sets DBR=$00 before its main loop, which made the branch
    ;ALWAYS taken: every poll reported an empty keyboard, keyboard_read_and_
    ;dispatch fell straight into mem_run_defrag, and the editor sat on a
    ;correctly rendered file ignoring every key. Carry is the ABI's status
    ;flag here; N and Z belong to whatever KLEAVE popped last and mean
    ;nothing. Test the value, never the leftover.
    ;`jmp`, not `beq`: `none` is at the far end of the special-key ladder and
    ;the three bytes above put it out of branch range.
    cmp #0
    bne :+
    jmp none

:   and #KEY_CTRL
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

    ;The console's characters are ASCII; the editor's key tables are the X16
    ;Kernal's PETSCII-shaped values. Tab ($09), Enter ($0D) and ESC ($1B)
    ;happen to be the same number in both, which is why every OTHER key worked
    ;and hid these two: ASCII Backspace ($08) missed default_keyval and fell
    ;through to default_insert, putting a control byte in the buffer that the
    ;CP437 font draws blank -- Backspace looked like it TYPED A SPACE -- and
    ;Delete ($7F) would have inserted a glyph the same way. Translate here,
    ;the one place every editor input path shares. NAMED labels: this proc has
    ;already paid once for an anonymous label re-pointing an earlier branch.
    cmp #$08
    bne not_bs
    lda #KEYVAL_BACKSPACE
not_bs:
    cmp #$7f
    bne not_del
    lda #KEYVAL_DELETE
not_del:
    cmp #'x'
    bne :+
    ldy x816_smoke_exit
    beq :+
    stz x816_smoke_exit
    lda #KBD_MODIFIER_CTRL
    sta scancode_modifiers
    lda #'x'

    ;Same rule as above, one branch later. Reaching this label by the `beq`
    ;from `ldy x816_smoke_exit` -- an ordinary 'x' typed with the smoke flag
    ;clear, which is every 'x' a user types -- arrived with Z set, and the
    ;caller's `bne gotachar` then dropped the character. A is the key and is
    ;non-zero by construction here, so this just states that.
:   cmp #0
    rts

special:
    ;.a16, AND IT IS LOAD-BEARING. ca65 tracks the accumulator width
    ;TEXTUALLY, and the character path just above ends in `sep #$20` / `.a8`
    ;-- but control REACHES this label from `classify`, which is 16-bit. So
    ;without this the whole ladder below assembled as 8-bit immediates while
    ;the machine ran 16-bit: `and #$00ff` came out `29 FF` instead of
    ;`29 FF 00`, the CPU swallowed the next opcode as the immediate's high
    ;byte, and every comparison after it decoded from the wrong offset.
    ;
    ;That is why NO special key worked -- arrows, Home, End, PgUp/PgDn,
    ;Insert, F1-F8 -- while ordinary characters and Enter were fine: those
    ;never come through here. Verified in the listing, not guessed.
    .a16
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
    ;Both calls widen the INDEX registers too -- see the note in
    ;x816_kernal_getin. These two are reached from the error paths rather than
    ;the main loop, so they would have crashed the machine the first time the
    ;editor tried to report anything.
    cmp #147
    beq cls
    rep #$30
    .a16
    .i16
    and #$00ff
    jsl X816_CALL_CON_PUTC
    sep #$30
    .a8
    .i8
    clc
    rts
cls:
    rep #$30
    .a16
    .i16
    jsl X816_CALL_CON_CLS
    sep #$30
    .a8
    .i8
    clc
    rts
.endproc

;******************************************************************************
;Function name.......: x816_frame_edge
;Purpose.............: Sets irq_flag once per VSYNC frame, so main.asm's loop
;                      runs sixty times a second instead of as fast as the CPU
;                      can go. X816-only.
;
;                      The editor installs no interrupt of its own here -
;                      irq.inc's X816 irq_init only sets the flag once and
;                      scancode_init only clears the modifiers, so NEITHER
;                      touches the I flag and the kernel's VSYNC handler is
;                      still running underneath. That is also why k_edit
;                      brackets the editor with ccur_off/ccur_on: the kernel's
;                      own cursor is live on the same interrupt. So the frame
;                      edge is READ from the kernel's counter rather than
;                      waited for on a vector this program does not own.
;
;                      A frozen counter cannot hang the loop in practice: the
;                      one thing that stops it is an SD transfer, and that
;                      freezes the CPU too, so this code is not running either.
;Input...............: Nothing
;Returns.............: irq_flag = 1 if a new frame started, else untouched
;Error returns.......: None
.proc x816_frame_edge
    rep #$30
    .a16
    .i16
    jsl X816_CALL_IRQ_FRAMES
    sep #$10
    .i8

    ;Explicit compare. The flags a kernel call returns mean nothing - KLEAVE
    ;ends pld/plb, so N and Z describe the caller's own registers - and only
    ;carry carries the ABI's answer. See the note in x816_kernal_getin.
    cmp x816_frame_last
    beq same

    sta x816_frame_last
    stz x816_frame_stall            ;16-bit: the wait is over
    sep #$20
    .a8
    lda #1
    sta irq_flag
    rts

same:
    ;.a16 because ca65 tracks widths TEXTUALLY: the branch above ends in a
    ;sep #$20, so without this it assembles the compare below as 8-bit and
    ;refuses $8000. Control actually reaches here with A still 16 bits.
    .a16
    ;NO NEW FRAME YET. Count the spin, and give up on it after far longer than
    ;a frame can take.
    ;
    ;Waiting forever on a counter this program does not own is a HANG, and a
    ;hung editor is worse than the fast cursor this pacing exists to fix: the
    ;caller never gets its machine back. The kernel's VSYNC handler is running
    ;in every case checked -- the editor sets no I flag anywhere -- but "every
    ;case checked" is not every caller, and durexForth and SuperBasic arrive
    ;here with interrupt state this file has never seen.
    ;
    ;So the fallback is the OLD behaviour, deliberately: run the loop
    ;unpaced. A machine whose frame counter is stopped gets an editor that
    ;blinks too fast, which is usable, instead of one that is dead.
    lda x816_frame_stall
    ina
    sta x816_frame_stall
    cmp #$8000                      ;~12 frames' worth of polls, at any
    bcc waiting                     ;plausible cost per poll
    stz x816_frame_stall
    sep #$20
    .a8
    lda #1
    sta irq_flag
    rts

waiting:
    sep #$20
    .a8
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
    ;The last VSYNC frame number seen by x816_frame_edge. Uninitialised is
    ;fine: whatever it holds, the loop either runs one pass immediately or
    ;waits at most one frame for the counter to move off it.
    x816_frame_last: .res 2
    ;How long x816_frame_edge has been spinning without seeing a new frame.
    x816_frame_stall: .res 2
.CODE
