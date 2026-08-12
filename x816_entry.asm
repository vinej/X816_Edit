;*******************************************************************************
; X816 resident-editor entry thunks.
;
; The ca65 editor blob is bank-local code. Calypsi firmware code will enter it
; with JSL, so the public entry point must return with RTL even though the
; editor internals use ordinary JSR/RTS within their own bank.
;*******************************************************************************

.import main_x816_entry
.import x816_edit_mem_smoke
.export x816_edit_default_entry

.segment "CODE"

.proc x816_edit_default_entry
    php
    phb
    phd
    sep #$20
    pea $0000
    plb
    plb
    pea $0000
    pld
    sep #$30
    lda $07fe
    cmp #1
    bne :+
    stz $07fe
    jsr x816_edit_mem_smoke
    sta $07fd
    bra done

:   
    jsr main_x816_entry
done:
    rep #$30
    pld
    plb
    plp
    rtl
.endproc
