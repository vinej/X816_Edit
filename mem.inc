;*******************************************************************************
;Copyright 2022-2024, Stefan Jakobsson
;
;Redistribution and use in source and binary forms, with or without modification, 
;are permitted provided that the following conditions are met:
;
;1. Redistributions of source code must retain the above copyright notice, this 
;   list of conditions and the following disclaimer.
;
;2. Redistributions in binary form must reproduce the above copyright notice, 
;   this list of conditions and the following disclaimer in the documentation 
;   and/or other materials provided with the distribution.
;
;THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” 
;AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
;IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
;DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE 
;FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
;DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
;SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
;CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
;OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
;OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;*******************************************************************************

;**********************************************************
;General information on banked RAM usage and layout
;----------------------------------------------------------
;- The editor's use of banked RAM is controlled by
;  the global variables mem_start and mem_top, that
;  are set on startup.
;
;  mem_start is the first bank number used, and mem_top
;  is the last bank number used.
;
;  The purpose is to reserve parts of banked RAM for
;  other use. Bank 0 is always used by the Kernal.
;
;- The first bank available to the editor (mem_start) is
;  used for internal purposes, and not for the text buffer.
;
;- The head of the text buffer is in bank mem_start+1,
;  address $a000.
;
;- The text buffer is not organzied as a continuous string,
;  as this would cause performance problems when
;  editing large text buffers. This would especially be
;  an issue if you insert or delete text at the start
;  of a large text buffer, as all subsequent content
;  would need to be copied/moved forward or backward.
;
;  To overcome this, the text buffer is divided into
;  memory pages each of 256 bytes. The memory pages
;  are doubly linked to each other, holding a 
;  reference both to the previous and the next
;  page.
;
;  This makes it possible for the editor to dynamically
;  link in new memory pages instead of copying/moving
;  the whole buffer.
;
;- There is a memory map (similar to 1541 BAM) to 
;  keep record of which pages are allocated and which 
;  are free. The map is primarily used by the alloc 
;  and free functions.
;
;- The format of each page of the text buffer is as follows:
;
;  Offset  Description
;  $00     Bank where previous page is
;  $01     Previous page ($A0-$BF, $00=NULL=>head of buffer)
;  $02     Bank where next page is
;  $03     Next page ($A0-$BF, $00=NULL=>end of buffer)
;  $04     Length of text stored in this page
;  $05-$FF Text data
;
;- The described memory strategy will, however, cause
;  defragmentation, especially if the user goes back
;  and forward a lot inserting and deleting content.
;
;  By defragmentation I mean empty memory pages or
;  pages with unused space scattered around the buffer.
;
;  To overcome this, the editor has a defragmentation
;  routine that is called on every interrupt cycle
;  where there is no user input to handle.
;
;  Not all pages can be defragmented on each run, as
;  this would take the same amount of processing time
;  as moving/copying one continuous buffer - what
;  we are trying to avoid with the paged memory
;  strategy.
;
;  Therefore, on each invocation of the defragmentation
;  routine only one memory page is defragmented. If the
;  user is not making any input, 60 pages per second 
;  are defragmentated. If all pages are allocated for
;  the buffer, a maximum of 8,128 pages, one complete
;  defragmentation will take a total of 135 seconds.
;
;  As the defragmentation is run in the background and
;  is paused if the user enters any input during an
;  interrupt cycle, it will not affect the end user
;  experience in any noticable way.
;
;  The routine works by continuously moving text 
;  upstreams in the buffer. After all pages have been 
;  looked at, there is unused space only in the last page of 
;  the buffer.
;**********************************************************

;**********************************************************
;Function name.......: mem_init
;Description.........: Initializes program's memory
;                      usage, and allocates the first
;                      page of banked RAM.
;Input...............: Nothing
;Returns.............: Nothing
;Error returns.......: Nothing
.proc mem_init
.if ::target_mem=target_x816
    lda #X816_EDIT_BASE_BANK
    sta mem_start
    sta CRS_BNK
    sta LNV_BNK
    sta SCR_BNK
    sta mem_run_defrag_page
    sta BNK_SEL

    lda #X816_EDIT_LAST_BANK
    sta mem_top

    lda #X816_EDIT_MAP_BANK
    sta TMP1_BNK
    lda #X816_EDIT_MAP_PAGE
    sta TMP1_ADR+1
    stz TMP1_ADR

    ; The allocation bitmap lives in the last four pages of the editor region.
    x816_data_dbr_on TMP1_BNK
    lda #0
    ldx #3
clear_map_page:
    ldy #0
:   sta (TMP1_ADR),y
    iny
    bne :-
    inc TMP1_ADR+1
    dex
    bne clear_map_page

    ldy #0
:   sta (TMP1_ADR),y
    iny
    cpy #$e0
    bne :-

    ldy #0
    sty TMP1_ADR
    lda #X816_EDIT_MAP_PAGE
    sta TMP1_ADR+1
    ldx #$1f
mark_null_pages:
    lda #1
    sta (TMP1_ADR),y
    clc
    lda TMP1_ADR
    adc #$20
    sta TMP1_ADR
    bcc :+
    inc TMP1_ADR+1
:   dex
    bne mark_null_pages

    ;The base bank's first pages are never allocatable: pages $00-$07 are the
    ;editor's flat scratch (common.inc), and page X816_EDIT_BASE_PAGE is the
    ;head of the text buffer. Two map bytes cover all nine.
    stz TMP1_ADR
    lda #X816_EDIT_MAP_PAGE
    sta TMP1_ADR+1
    lda #$ff
    sta (TMP1_ADR),y
    iny
    lda #(1 << (X816_EDIT_BASE_PAGE & 7))
    sta (TMP1_ADR),y
    ldy #0

    lda #$df
    sta TMP1_ADR
    lda #$ff
    sta TMP1_ADR+1
    ldy #0
    lda #X816_EDIT_RESERVED_TOP_MASK
    sta (TMP1_ADR),y
    x816_data_dbr_off

    lda #X816_EDIT_BASE_PAGE
    sta CRS_ADR+1
    sta LNV_ADR+1
    sta SCR_ADR+1
    sta mem_run_defrag_page+1

    stz CRS_ADR
    stz LNV_ADR
    stz SCR_ADR

    stz CRS_IDX
    stz LNV_IDX
    stz SCR_IDX

    x816_data_dbr_on CRS_BNK
    lda #0
    ldy #0
:   sta (CRS_ADR),y
    iny
    cpy #5
    bne :-
    x816_data_dbr_off

    ;Free pages: X816_EDIT_PAGE_COUNT less the eight scratch pages, the head
    ;page, the thirty null-link sentinels in banks $C2-$DF and the four pages
    ;the allocation bitmap itself occupies.
    lda #<X816_EDIT_FREE_PAGES
    sta mem_blocks_free
    lda #>X816_EDIT_FREE_PAGES
    sta mem_blocks_free+1

    lda #1
    sta mem_cur_line
    sta mem_cur_col

    stz mem_cur_line+1
    stz mem_cur_line+2

    stz mem_cur_col+1
    stz mem_cur_col+2

    stz mem_modified

    rts
.endif

    ;Get mem top
    bridge_setaddr KERNAL_MEMTOP
    sec
    bridge_call KERNAL_MEMTOP
    dea                 ;Kernal MEMTOP actually returns memtop+1, so we need to decrease by 1 to make it usable

    cmp mem_top         ;Use mem top by from Kernal if less than the user request
    bcs :+
    sta mem_top

    ;Setup mem map values
:   lda mem_start       ;Prepare
    sta BNK_SEL

    lda #<mem_map
    sta TMP1_ADR
    lda #>mem_map
    sta TMP1_ADR+1
    
    ldy #0

    lda #255            ;Bank 0 to mem_start: Not part of the text buffer, mark as allocated
    ldx mem_start
    inx
    jsr init_map

    lda #%00000001      ;Mark first page in the text buffer as allocated
    sta (TMP1_ADR),y
    iny
    
    sec                 ;Bank mem_start+1 to mem_top: Rest of the text buffer, mark as free
    lda mem_top
    sbc mem_start
    tax
    lda #0
    jsr init_map

    sec                 ;Bank mem_top+1 to 255: Memory above text buffer, mark as allocated
    lda #255
    sbc mem_top
    tax
    lda #255
    jsr init_map

blocks_free_counter:
    ;Calculate number of free blocks, (mem_top-mem_start)*256/8 = (mem_top-mem_start)*32
    sec
    lda mem_top
    sbc mem_start

    stz mem_blocks_free
    sta mem_blocks_free+1       ;By storing mem_top-mem_start in the MSB we are effectively multiplying by 256

    ldx #3                      ;And here we do a 16 bit division by 8
:   lsr mem_blocks_free+1
    ror mem_blocks_free
    dex
    bne :-

    ;Decrease mem blocks free counter by 1 to account for the head of buffer that was allocated above in this function
    sec
    lda mem_blocks_free
    sbc #1
    sta mem_blocks_free
    lda mem_blocks_free+1
    sbc #0
    sta mem_blocks_free+1

    ;Setup zero page vectors
    ;Bank
    lda mem_start
    ina             ;The head of buffer is the bank after mem_start (mem_start used for internal data storage)
    sta BNK_SEL
    sta CRS_BNK     ;Cursor
    sta LNV_BNK     ;Line first visible char
    sta SCR_BNK     ;Screen first visible char

    ;Mem page, i.e. most significant byte of address
    lda #$a0
    sta CRS_ADR+1   ;Cursor
    sta LNV_ADR+1   ;Line first visible char
    sta SCR_ADR+1   ;Screen first visible char

    ;Address, least significant byte
    stz CRS_ADR     ;Cursor
    stz LNV_ADR     ;Line first visible char
    stz SCR_ADR     ;Screen first visible char

    ;Memory offset values
    stz CRS_IDX     ;Cursor
    stz LNV_IDX     ;Line first visible char
    stz SCR_IDX     ;Screen first visible char

    ;Setup initial memory page metadata (bank mem_start+1, page $a0), bank selected above already
    lda #0
    ldy #0
:   sta (CRS_ADR),y
    iny
    cpy #5
    bne :-

    ;Set initial memory page for defrag runner
    lda mem_start
    ina
    sta mem_run_defrag_page
    lda #$a0
    sta mem_run_defrag_page+1

    ;Set current line=1 and column=1 (each is a 24 bit value)
    lda #1
    sta mem_cur_line
    sta mem_cur_col
    
    stz mem_cur_line+1
    stz mem_cur_line+2

    stz mem_cur_col+1
    stz mem_cur_col+2

    ;Set status: buffer not modified
    stz mem_modified

    rts

    ;Internal subroutine to set mem_map values
    ;Input: A=value, X=number of banks to write, Y should 
init_map:
    cpx #0              ;Exit if bank count=0
    beq init_map_exit

    sta tempvars        ;Save input
init_map_loop:
    lda tempvars
    sta (TMP1_ADR),y
    iny
    bne :+
    inc TMP1_ADR+1

:   tya
    and #3
    bne init_map_loop

    dex
    bne init_map_loop

init_map_exit:
    rts

.endproc

;**********************************************************
;Function name.......: x816_edit_mem_smoke
;Description.........: X816-only allocator/link smoke test
;Returns.............: A=0 on success, A=1 on failure. Detail byte at $07fc.
.if ::target_mem=target_x816
.export x816_edit_mem_smoke

.macro x816_smoke_assert_a expected, detail
    cmp #expected
    beq :+
    lda #detail
    jmp fail
:
.endmacro

.macro x816_smoke_assert_x expected, detail
    cpx #expected
    beq :+
    lda #detail
    jmp fail
:
.endmacro

.macro x816_smoke_assert_y expected, detail
    cpy #expected
    beq :+
    lda #detail
    jmp fail
:
.endmacro

;The map byte holding the head page and the first allocations, and the two
;pages mem_alloc must hand out first. Spelled as arithmetic on the layout
;constants: pinning them to literals is how a test keeps passing after the
;thing it tests has moved.
X816_SMOKE_MAPBYTE = X816_EDIT_BASE_PAGE / 8
X816_SMOKE_HEADBIT = 1 << (X816_EDIT_BASE_PAGE & 7)
X816_SMOKE_PAGE1   = X816_EDIT_BASE_PAGE + 1
X816_SMOKE_PAGE2   = X816_EDIT_BASE_PAGE + 2

.proc x816_edit_mem_smoke
    stz x816_mem_smoke_detail
    jsr mem_init

    lda mem_blocks_free
    x816_smoke_assert_a <X816_EDIT_FREE_PAGES, 1
    lda mem_blocks_free+1
    x816_smoke_assert_a >X816_EDIT_FREE_PAGES, 2

    ;Map byte 0 covers the scratch pages, all eight of them. It must read as
    ;fully allocated before and after everything below: the whole point of
    ;reserving them is that file_buf and the caller's backup can never be
    ;handed out as text pages.
    lda #X816_EDIT_MAP_BANK
    ldx #X816_EDIT_MAP_PAGE
    ldy #0
    jsr smoke_read
    x816_smoke_assert_a $ff, 3

    lda #X816_EDIT_MAP_BANK
    ldx #X816_EDIT_MAP_PAGE
    ldy #X816_SMOKE_MAPBYTE
    jsr smoke_read
    x816_smoke_assert_a X816_SMOKE_HEADBIT, 4

    lda #X816_EDIT_BASE_BANK
    ldx #X816_EDIT_BASE_PAGE
    ldy #4
    jsr smoke_read
    x816_smoke_assert_a 0, 5

    jsr mem_alloc
    x816_smoke_assert_y X816_EDIT_BASE_BANK, 6
    x816_smoke_assert_x X816_SMOKE_PAGE1, 7

    lda mem_blocks_free
    x816_smoke_assert_a <(X816_EDIT_FREE_PAGES-1), 8
    lda mem_blocks_free+1
    x816_smoke_assert_a >(X816_EDIT_FREE_PAGES-1), 9

    lda #X816_EDIT_BASE_BANK
    ldx #X816_EDIT_BASE_PAGE
    ldy #2
    jsr smoke_read
    x816_smoke_assert_a X816_EDIT_BASE_BANK, 10
    lda #X816_EDIT_BASE_BANK
    ldx #X816_EDIT_BASE_PAGE
    ldy #3
    jsr smoke_read
    x816_smoke_assert_a X816_SMOKE_PAGE1, 11

    lda #X816_EDIT_BASE_BANK
    ldx #X816_SMOKE_PAGE1
    ldy #0
    jsr smoke_read
    x816_smoke_assert_a X816_EDIT_BASE_BANK, 12
    lda #X816_EDIT_BASE_BANK
    ldx #X816_SMOKE_PAGE1
    ldy #1
    jsr smoke_read
    x816_smoke_assert_a X816_EDIT_BASE_PAGE, 13

    jsr mem_alloc
    x816_smoke_assert_y X816_EDIT_BASE_BANK, 14
    x816_smoke_assert_x X816_SMOKE_PAGE2, 15

    lda #X816_EDIT_MAP_BANK
    ldx #X816_EDIT_MAP_PAGE
    ldy #X816_SMOKE_MAPBYTE
    jsr smoke_read
    x816_smoke_assert_a (X816_SMOKE_HEADBIT | X816_SMOKE_HEADBIT<<1 | X816_SMOKE_HEADBIT<<2), 16

    lda #X816_EDIT_BASE_BANK
    ldx #X816_EDIT_BASE_PAGE
    ldy #3
    jsr smoke_read
    x816_smoke_assert_a X816_SMOKE_PAGE2, 17

    lda #X816_EDIT_BASE_BANK
    ldx #X816_SMOKE_PAGE2
    ldy #3
    jsr smoke_read
    x816_smoke_assert_a X816_SMOKE_PAGE1, 18

    lda #X816_EDIT_BASE_BANK
    ldx #X816_SMOKE_PAGE1
    ldy #1
    jsr smoke_read
    x816_smoke_assert_a X816_SMOKE_PAGE2, 19

    ldy #X816_EDIT_BASE_BANK
    ldx #X816_SMOKE_PAGE2
    jsr mem_free
    bcc :+
    lda #20
    jmp fail
:

    lda #X816_EDIT_MAP_BANK
    ldx #X816_EDIT_MAP_PAGE
    ldy #X816_SMOKE_MAPBYTE
    jsr smoke_read
    x816_smoke_assert_a (X816_SMOKE_HEADBIT | X816_SMOKE_HEADBIT<<1), 21

    lda #X816_EDIT_BASE_BANK
    ldx #X816_EDIT_BASE_PAGE
    ldy #3
    jsr smoke_read
    x816_smoke_assert_a X816_SMOKE_PAGE1, 22

    lda #X816_EDIT_BASE_BANK
    ldx #X816_SMOKE_PAGE1
    ldy #1
    jsr smoke_read
    x816_smoke_assert_a X816_EDIT_BASE_PAGE, 23

    ldy #X816_EDIT_BASE_BANK
    ldx #X816_SMOKE_PAGE1
    jsr mem_free
    bcc :+
    lda #24
    jmp fail
:

    lda #X816_EDIT_MAP_BANK
    ldx #X816_EDIT_MAP_PAGE
    ldy #X816_SMOKE_MAPBYTE
    jsr smoke_read
    x816_smoke_assert_a X816_SMOKE_HEADBIT, 25

    lda #X816_EDIT_BASE_BANK
    ldx #X816_EDIT_BASE_PAGE
    ldy #3
    jsr smoke_read
    x816_smoke_assert_a 0, 26

    lda #X816_EDIT_MAP_BANK
    ldx #X816_EDIT_MAP_PAGE
    ldy #0
    jsr smoke_read
    x816_smoke_assert_a $ff, 27

    lda mem_blocks_free
    x816_smoke_assert_a <X816_EDIT_FREE_PAGES, 28
    lda mem_blocks_free+1
    x816_smoke_assert_a >X816_EDIT_FREE_PAGES, 29

    jsr mem_init
    lda #0
    rts

fail:
    sta x816_mem_smoke_detail
    jsr mem_init
    lda #1
    rts

smoke_read:
    sta TMP1_BNK
    stx TMP1_ADR+1
    stz TMP1_ADR
    x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y
    x816_data_dbr_off
    rts
.endproc
.endif

;**********************************************************
;Function name.......: mem_is_head
;Description.........: Is page head of buffer, i.e.
;                      bank mem_start+1, page $a0
;Input...............: Y    Bank
;                      X    Page
;Returns.............: C = 1    Is head
;                      C = 0    Is not head
;Error returns.......: None
.proc mem_is_head
.if ::target_mem=target_x816
    cpy #X816_EDIT_BASE_BANK
    bne :+
    cpx #X816_EDIT_BASE_PAGE
    bne :+

    sec
    rts

:   clc
    rts
.endif

    dey
    cpy mem_start
    bne :+
    cpx #$a0
    bne :+

    sec
    rts

:   clc
    rts

.endproc

;**********************************************************
;Function name.......: mem_at_bof
;Description.........: Is cursor at beginning of file, 
;                      i.e. first char, not necessarily
;                      the same as head of buffer
;Input...............: Nothing
;Returns.............: C = 1    Cursor at beginning of file
;                      C = 0    Not at beginning of file
;Error returns.......: Nothing
.proc mem_at_bof
    ;Get cursor index
    lda CRS_IDX
    beq :+

    ;If cursor index > 0 we are not at bof
    clc
    rts

    ;Search for head of buffer, return if we find a char before that
    ;
    ;X816: every (TMP1_ADR),y below is a BUFFER PAGE read and needs the page's
    ;bank in DBR -- TMP1_BNK tracks it exactly where the X16 code moved
    ;BNK_SEL. This proc was never converted: with DBR=$00 the walk read bank
    ;$00, took whatever it found there for the prev-page link, and answered
    ;"at beginning of file" more or less at random. mem_crs_step_left asks it
    ;FIRST, so vertical movement died here as often as anywhere. The
    ;x816_data_dbr_off macro ends in `cmp #0`, which is what lets the beq/bne
    ;after each read keep meaning "was the byte zero".
:   lda CRS_BNK
    sta BNK_SEL
    sta TMP1_BNK

    lda CRS_ADR+1
    sta TMP1_ADR+1
    stz TMP1_ADR

:   ldy #1
    x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y        ;Prev page
    x816_data_dbr_off
    beq at_bof

    tax
    ldy #0
    x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y        ;Prev Bank
    x816_data_dbr_off
    sta BNK_SEL
    sta TMP1_BNK
    stx TMP1_ADR+1          ;Prev page

    ldy #4
    x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y        ;Page len
    x816_data_dbr_off
    beq :-                  ;Loop until we find non zero len page

not_bof:
    clc
    rts

at_bof:
    sec
    rts
.endproc

;**********************************************************
;Function name.......: mem_at_eof
;Description.........: Returns if cursor is at end of file.
;                      When inserting text at the end of the
;                      text buffer, the cursor will be
;                      one step right of the last char.
;                      This function returns true both
;                      when the cursor is at the last
;                      char and one step right of that.
;Input...............: Nothing
;Returns.............: C = 1    Cursor at end of file
;                      C = 0    Not at end of file
;Error returns.......: Nothing
.proc mem_at_eof
    ;Copy cursor pointer to temp pointer
    lda CRS_BNK
    sta BNK_SEL
    sta TMP1_BNK
    lda CRS_ADR+1
    sta TMP1_ADR+1
    stz TMP1_ADR

    ;Is cursor index + 1 < page len, we're not at end of mem page, and consequently not at end of file
    ;
    ;_KEEP_A, because A is the value being compared. The plain macro is
    ;phb / LDA bank / pha / plb -- it LOADS A with the bank number, so the
    ;compare below was testing the bank against the page length instead of
    ;the cursor index. Banks here are $C1-$DF, page lengths at most 251, so
    ;the answer was arbitrary and usually wrong.
    lda CRS_IDX
    ina
    ldy #4
    x816_data_dbr_on_keep_a TMP1_BNK
    cmp (TMP1_ADR),y
    x816_data_dbr_restore           ;plb: clobbers N and Z, but NOT carry
    bcc not_eof

:   ldy #3
    x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y        ;Next page
    x816_data_dbr_off
    tax
    beq at_eof              ;Next page=0 => there is no next page, i.e. eof

    tax
    ldy #2
    x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y        ;Next bank
    x816_data_dbr_off
    sta BNK_SEL
    sta TMP1_BNK
    stx TMP1_ADR+1

    ldy #4
    x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y        ;Next page len
    x816_data_dbr_off
    beq :-                  ;Loop until we find a non zero len page

not_eof:
    clc
    rts

at_eof:
    sec
    rts

.endproc

;**********************************************************
;Funcion name........: mem_alloc
;Description.........: Allocates a page in banked RAM. The
;                      newly allocated page will be linked
;                      in after the page pointed to by the
;                      cursor.
;Input...............: Nothing
;Returns.............: Allocated memory page, Y= bank, and 
;                      X=page (address MSB)
;Error returns.......: X=0 if memory full
.proc mem_alloc
.if ::target_mem=target_x816
    lda #X816_EDIT_MAP_BANK
    sta TMP1_BNK
    lda #X816_EDIT_MAP_PAGE
    sta TMP1_ADR+1
    stz TMP1_ADR

    stz map_index
    stz map_index+1
    lda #<X816_EDIT_MAP_BYTES
    sta bytes_left
    lda #>X816_EDIT_MAP_BYTES
    sta bytes_left+1

    ldy #0
x816_scan_map:
    lda bytes_left
    ora bytes_left+1
    bne :+
    ldx #0
    rts

:   x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y
    x816_data_dbr_off
    cmp #$ff
    bne x816_free_mem_found

    inc TMP1_ADR
    bne :+
    inc TMP1_ADR+1

:   inc map_index
    bne :+
    inc map_index+1

:   lda bytes_left
    bne :+
    dec bytes_left+1
:   dec bytes_left
    bra x816_scan_map

x816_free_mem_found:
    sta value

    ldx #0
x816_find_bit:
    x816_code_dbr_on
    lda bitmask,x
    x816_code_dbr_off
    and value
    beq x816_mark_page_allocated
    inx
    bra x816_find_bit

x816_mark_page_allocated:
    x816_code_dbr_on
    lda bitmask,x
    x816_code_dbr_off
    ora value
    x816_data_dbr_on_keep_a TMP1_BNK
    sta (TMP1_ADR),y
    x816_data_dbr_off

    lda map_index
    sta page_index
    lda map_index+1
    sta page_index+1
    asl page_index
    rol page_index+1
    asl page_index
    rol page_index+1
    asl page_index
    rol page_index+1

    txa
    clc
    adc page_index
    sta newpage
    lda page_index+1
    adc #0
    clc
    adc #X816_EDIT_BASE_BANK
    sta newbank

    x816_data_dbr_on CRS_BNK
    ldy #2
    lda (CRS_ADR),y
    x816_data_dbr_off
    sta nextbank

    x816_data_dbr_on CRS_BNK
    ldy #3
    lda (CRS_ADR),y
    x816_data_dbr_off
    sta nextpage

    lda newpage
    x816_data_dbr_on_keep_a CRS_BNK
    ldy #3
    sta (CRS_ADR),y
    x816_data_dbr_off

    lda newbank
    x816_data_dbr_on_keep_a CRS_BNK
    ldy #2
    sta (CRS_ADR),y
    x816_data_dbr_off

    lda newbank
    sta TMP1_BNK
    sta BNK_SEL
    lda newpage
    sta TMP1_ADR+1
    stz TMP1_ADR

    lda CRS_BNK
    x816_data_dbr_on_keep_a TMP1_BNK
    ldy #0
    sta (TMP1_ADR),y
    x816_data_dbr_off

    lda CRS_ADR+1
    x816_data_dbr_on_keep_a TMP1_BNK
    ldy #1
    sta (TMP1_ADR),y
    x816_data_dbr_off

    lda nextbank
    x816_data_dbr_on_keep_a TMP1_BNK
    ldy #2
    sta (TMP1_ADR),y
    x816_data_dbr_off

    lda nextpage
    x816_data_dbr_on_keep_a TMP1_BNK
    ldy #3
    sta (TMP1_ADR),y
    x816_data_dbr_off

    lda #0
    x816_data_dbr_on_keep_a TMP1_BNK
    ldy #4
    sta (TMP1_ADR),y
    x816_data_dbr_off

    lda nextpage
    beq x816_alloc_exit

    sta TMP2_ADR+1
    stz TMP2_ADR
    lda nextbank
    sta TMP1_BNK
    sta BNK_SEL

    lda newbank
    x816_data_dbr_on_keep_a TMP1_BNK
    ldy #0
    sta (TMP2_ADR),y
    x816_data_dbr_off

    lda newpage
    x816_data_dbr_on_keep_a TMP1_BNK
    ldy #1
    sta (TMP2_ADR),y
    x816_data_dbr_off

x816_alloc_exit:
    ldy newbank
    ldx newpage

    lda mem_blocks_free
    bne :+
    dec mem_blocks_free+1
:   dec mem_blocks_free

    rts
.endif

    ;Select RAM bank mem_start
    lda mem_start
    sta BNK_SEL

    ;Prepare searching the memory map
    lda #<mem_map
    sta TMP1_ADR
    lda #>mem_map
    sta TMP1_ADR+1

    ldy #0
loop:
    ;Search memory map for free pages: search for entries != $ff
    lda (TMP1_ADR),y
    cmp #$ff
    bne free_mem_found
    
    iny
    bne loop

    inc TMP1_ADR+1
    lda TMP1_ADR+1
    cmp #>mem_map+4
    bne loop

mem_full:
    ;If we reach this point there was no free memory, return X=0
    ldx #0
    rts

free_mem_found:    
    ;Store the value of the byte in mem_map where we found free memory, will be needed later in the function
    sta value           

    ;Step 1. Divide the LSB of the number of search loops (= value of .Y) by 4 and we have the first part of the bank
    tya
    lsr
    lsr
    sta newbank

    ;Step 2. Multiply the MSB of the number of search loops (>TMP1_ADR minus >mem_map) by 64, and add that to the bank
    lda TMP1_ADR+1
    sec
    sbc #>mem_map
    asl
    asl
    asl
    asl
    asl
    asl
    clc
    adc newbank
    sta newbank

    ;Step 3. Multiply the LSB of the number of search loops (= value of .Y) by 8 and we have the first part of the page
    tya
    asl
    asl
    asl

    ;Step 4. And that value by $1f to keep within page boundary (00-1f)
    and #$1f
    sta newpage

    ;Step 5. The second part of the page is found in the mem_map value stored earlier. Add the positional value of the first bit not set to the page
    ldx #0

:   lda value
    and bitmask,x
    beq mark_page_allocated
    inx
    bra :-

mark_page_allocated:
    lda value
    ora bitmask,x
    sta (TMP1_ADR),y

    ;Step 6. Finally add start of banked ram ($a0) to the page. And we're done.
    txa
    clc
    adc newpage
    adc #$a0
    sta newpage

    ;Update mem page metadata; we need to change the current page, the new page, and the page following that
    lda CRS_BNK
    sta BNK_SEL
    
    ldy #2
    lda (CRS_ADR),y
    sta nextbank
    ldy #3
    lda (CRS_ADR),y
    sta nextpage

    lda newpage
    sta (CRS_ADR),y
    ldy #2
    lda newbank
    sta (CRS_ADR),y

    lda newbank
    sta BNK_SEL
    lda newpage
    sta TMP1_ADR+1
    stz TMP1_ADR

    ldy #0
    lda CRS_BNK
    sta (TMP1_ADR),y
    ldy #1
    lda CRS_ADR+1
    sta (TMP1_ADR),y
    ldy #2
    lda nextbank
    sta (TMP1_ADR),y
    ldy #3
    lda nextpage
    sta (TMP1_ADR),y
    ldy #4
    lda #0
    sta (TMP1_ADR),y

    lda nextpage
    beq exit

    sta TMP2_ADR+1
    stz TMP2_ADR
    lda nextbank
    sta BNK_SEL

    ldy #0
    lda newbank
    sta (TMP2_ADR),y
    ldy #1
    lda newpage
    sta (TMP2_ADR),y

exit:
    ;Return allocated bank and page
    ldy newbank
    ldx newpage

    ;Decrease mem blocks free counter
    lda mem_blocks_free
    bne :+
    dec mem_blocks_free+1
:   dec mem_blocks_free

    rts

newbank = tempvars          ;1 byte
newpage = tempvars+1        ;1 byte
value = tempvars+2          ;1 byte
nextbank = tempvars+3       ;1 byte
nextpage = tempvars + 4     ;1 byte
map_index = tempvars + 5    ;2 bytes
bytes_left = tempvars + 7   ;2 bytes
page_index = tempvars + 9   ;2 bytes

bitmask:
    .byt 1, 2, 4, 8, 16, 32, 64, 128
.endproc

;**********************************************************
;Function name.......: mem_free
;Description.........: Frees a page in banked RAM
;Input...............: Y    Bank
;                      X    Page
;Returns.............: Nohting
;Error returns.......: C = 1    Could not free page
.proc mem_free
    ;Save input params
    sty bank
    stx page

.if ::target_mem=target_x816
    cpy #X816_EDIT_BASE_BANK
    bcc x816_is_reserved
    cpy #X816_EDIT_LAST_BANK
    bcc :+
    bne x816_is_reserved
    cpx #X816_EDIT_MAP_PAGE
    bcs x816_is_reserved

:   cpx #0
    bne :+
x816_is_reserved:
    sec
    rts

:   tya
    sec
    sbc #X816_EDIT_BASE_BANK
    sta map_index
    stz map_index+1
    asl map_index
    rol map_index+1
    asl map_index
    rol map_index+1
    asl map_index
    rol map_index+1
    asl map_index
    rol map_index+1
    asl map_index
    rol map_index+1

    lda page
    lsr
    lsr
    lsr
    clc
    adc map_index
    sta TMP1_ADR
    lda map_index+1
    adc #X816_EDIT_MAP_PAGE
    sta TMP1_ADR+1

    lda page
    and #7
    tax

    lda #X816_EDIT_MAP_BANK
    sta TMP1_BNK
    ldy #0
    x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y
    x816_data_dbr_off
    sta value

    x816_code_dbr_on
    lda bitmask,x
    x816_code_dbr_off
    and value
    bne :+
    clc
    rts

:   x816_code_dbr_on
    lda bitmask,x
    x816_code_dbr_off
    eor #255
    and value
    x816_data_dbr_on_keep_a TMP1_BNK
    sta (TMP1_ADR),y
    x816_data_dbr_off

    lda bank
    sta TMP1_BNK
    sta BNK_SEL
    lda page
    sta TMP1_ADR+1
    stz TMP1_ADR

    x816_data_dbr_on TMP1_BNK
    ldy #0
    lda (TMP1_ADR),y
    x816_data_dbr_off
    sta prev

    x816_data_dbr_on TMP1_BNK
    ldy #1
    lda (TMP1_ADR),y
    x816_data_dbr_off
    sta prev+1

    x816_data_dbr_on TMP1_BNK
    ldy #2
    lda (TMP1_ADR),y
    x816_data_dbr_off
    sta next

    x816_data_dbr_on TMP1_BNK
    ldy #3
    lda (TMP1_ADR),y
    x816_data_dbr_off
    sta next+1

    lda prev+1
    beq x816_next_page_link

    sta TMP1_ADR+1
    stz TMP1_ADR
    lda prev
    sta TMP1_BNK
    sta BNK_SEL

    lda next
    x816_data_dbr_on_keep_a TMP1_BNK
    ldy #2
    sta (TMP1_ADR),y
    x816_data_dbr_off

    lda next+1
    x816_data_dbr_on_keep_a TMP1_BNK
    ldy #3
    sta (TMP1_ADR),y
    x816_data_dbr_off

x816_next_page_link:
    lda next+1
    beq x816_free_exit

    sta TMP1_ADR+1
    stz TMP1_ADR
    lda next
    sta TMP1_BNK
    sta BNK_SEL

    lda prev
    x816_data_dbr_on_keep_a TMP1_BNK
    ldy #0
    sta (TMP1_ADR),y
    x816_data_dbr_off

    lda prev+1
    x816_data_dbr_on_keep_a TMP1_BNK
    ldy #1
    sta (TMP1_ADR),y
    x816_data_dbr_off

x816_free_exit:
    inc mem_blocks_free
    bne :+
    inc mem_blocks_free+1

:   clc
    rts
.endif
    
    ;Check if bank is reserved, exit without deallocating if true
    cpy mem_start
    beq is_reserved     ;Don't touch head of buffer - bank mem_start+1, page $a0
    bcc is_reserved
    
    cpy mem_top
    beq :+
    bcs is_reserved     ;Don't touch unpopulated memory

:   tya                 
    dea
    cmp mem_start
    bne :+
    lda page
    cmp #$a0
    bne :+

is_reserved:
    sec
    rts

    ;Continue freeing that mem page...
    ;Find out position in mem_map corresponding to specified bank/page
:   lda mem_start              ;Select bank mem_start to access mem_map
    sta BNK_SEL

    lda bank
    lsr
    lsr
    lsr
    lsr
    lsr
    lsr
    clc
    adc #>mem_map
    sta TMP1_ADR+1      ;High byte = bank/64 + mem_mapH
    
    lda bank
    asl
    asl
    adc #<mem_map
    sta TMP1_ADR        ;Low byte = bank * 4 + mem_mapL
    lda #0
    adc TMP1_ADR+1
    sta TMP1_ADR+1      ;Add possible overflow to high byte

    lda page
    sec
    sbc #$a0
    lsr
    lsr
    lsr
    tay                 ;Offset, Y = page/8

    lda page
    and #7
    tax                 ;X is now bitmask index corresponding to the page

    lda bitmask,x
    and (TMP1_ADR),y
    beq exit2           ;Page already free, exit
    
    lda bitmask,x
    eor #255
    and (TMP1_ADR),y
    sta (TMP1_ADR),y    ;Mark page as free

    ;Get prev and next page
    lda bank
    sta BNK_SEL
    lda page
    sta TMP1_ADR+1
    stz TMP1_ADR

    ldy #0
    lda (TMP1_ADR),y
    sta prev
    ldy #1
    lda (TMP1_ADR),y
    sta prev+1

    ldy #2
    lda (TMP1_ADR),y
    sta next
    ldy #3
    lda (TMP1_ADR),y
    sta next+1

    ;Set prev page pointer
    lda prev+1
    beq next_page_link      ;Prev page null, shouldn't happen as we don't allow freeing the head of the buffer
   
    sta TMP1_ADR+1
    lda prev
    sta BNK_SEL

    ldy #2
    lda next
    sta (TMP1_ADR),y
    ldy #3
    lda next+1
    sta (TMP1_ADR),y

    ;Set next page pointer
next_page_link:
    lda next+1
    beq exit             ;Next page null
    
    sta TMP1_ADR+1
    lda next
    sta BNK_SEL

    ldy #0
    lda prev
    sta (TMP1_ADR),y
    ldy #1
    lda prev+1
    sta (TMP1_ADR),y

exit:
    clc

    ;Increase mem block free counter
    inc mem_blocks_free
    bne exit2
    inc mem_blocks_free+1

exit2:
    rts

bank = tempvars     ;1 byte
page = tempvars+1   ;1 byte
prev = tempvars+2   ;2 bytes
next = tempvars+4   ;2 bytes
value = tempvars+6  ;1 byte
map_index = tempvars+7 ;2 bytes

bitmask:
    .byt 1,2,4,8,16,32,64,128
.endproc

;**********************************************************
;Function name.......: mem_run_defrag
;Description.........: This function is meant to be invoked
;                      once every interrupt if there is
;                      no user input to handle. It will run 
;                      the defrag routine on one mem page 
;                      per invocation. The next candidate
;                      for defragmentation stored in global
;                      variable mem_run_defrag_page (16 bits)
;Input...............: Nothing
;Returns.............: Nothing
;Error returns.......: Nothing
.proc mem_run_defrag
.if ::target_mem=target_x816
    rts
.endif

    ;Set pointers to next bank/page to defrag, which is stored in
    ;mem_run_defrag_page (2 bytes)
    lda mem_run_defrag_page
    sta BNK_SEL
    lda mem_run_defrag_page+1
    sta TMP1_ADR+1
    stz TMP1_ADR

    ;First visble char of the screen at page after page to defrag - Pointer to first char could be lost
    ldy #2
    lda (TMP1_ADR),y
    cmp SCR_BNK
    bne :+
    ldy #3
    lda (TMP1_ADR),y
    cmp SCR_ADR+1
    bne :+
    bra exit

    ;First visible char of line at page after page to defrag - Pointer to first char could be lost
:   ldy #2
    lda (TMP1_ADR),y
    cmp LNV_BNK
    bne :+
    ldy #3
    lda (TMP1_ADR),y
    cmp LNV_ADR+1
    bne :+
    bra exit

    ;Cursor at page after page to defrag - Pointer to cursor could be lost
:   ldy #2
    lda (TMP1_ADR),y
    cmp CRS_BNK
    bne :+
    ldy #3
    lda (TMP1_ADR),y
    cmp CRS_ADR+1
    bne :+
    bra exit

    ;Cursor at page to defrag - Not effective, as we're typing here
:   lda mem_run_defrag_page
    cmp CRS_BNK
    bne :+
    lda mem_run_defrag_page+1
    cmp CRS_ADR+1
    bne :+
    bra exit

    ;Cursor at page before page to defrag - Not effective, as we're typing here
:   lda CRS_BNK
    sta BNK_SEL

    ldy #2
    lda (CRS_ADR),y
    cmp mem_run_defrag_page
    bne :+
    ldy #3
    lda (CRS_ADR),y
    cmp mem_run_defrag_page+1
    bne :+
    bra exit

    ;All checks passed, go on and run the defrag
:   ldy mem_run_defrag_page
    ldx mem_run_defrag_page+1
    jsr mem_defrag

exit:
    ;Select bank/page to look at when calling this function next time
    lda mem_run_defrag_page
    sta BNK_SEL
    
    lda mem_run_defrag_page+1
    sta TMP1_ADR+1
    stz TMP1_ADR
    
    ldy #3
    lda (TMP1_ADR),y
    beq eof
    sta mem_run_defrag_page+1
    
    ldy #2
    lda (TMP1_ADR),y
    sta mem_run_defrag_page
    rts

eof:
    ;Start over from head of buffer
    lda mem_start
    ina
    sta mem_run_defrag_page
    lda #$a0
    sta mem_run_defrag_page+1
    rts

.endproc

;**********************************************************
;Function name.......: mem_defrag
;Description.........: Defragments a memory page. This
;                      function will copy text from the
;                      page after the page to dedfrag filling 
;                      up the page to defrag. If the len of the
;                      page after the page to defrag is 0
;                      before or after this it will be marked
;                      as free.
;Input...............: Y = bank and X = page of page to defrag
;Returns.............: Nothing
;Error returns.......: Nothing
.proc mem_defrag
    ;Set pointer to page to defrag
    sty BNK_SEL
    sty TMP1_BNK
    stx TMP1_ADR+1
    stz TMP1_ADR

    ;Get len of page to defrag - exit if full
    ldy #4
    lda (TMP1_ADR),y
    cmp #251
    bcc :+
    rts

:   sta page_len

    ;Get page after page to defrag - exit if null
    ldy #3
    lda (TMP1_ADR),y
    bne :+              
    rts

:   sta TMP2_ADR+1
    stz TMP2_ADR
    
    ldy #2
    lda (TMP1_ADR),y
    sta TMP2_BNK 
    sta BNK_SEL

    ;Get len of page after page to defrag
    ldy #4
    lda (TMP2_ADR),y
    bne :+
    bra free                ;Next page len = 0, free it
:   sta nextpage_len

    ;Prepare copy to page to defrag
    lda #5
    sta TMP1_ADR
    sta TMP2_ADR
    stz nextpage_index

copy:
    lda nextpage_index
    cmp nextpage_len
    bcs copy_done

    lda page_len
    cmp #251
    bcs copy_done

    ldy nextpage_index
    ldx TMP2_BNK
    stx BNK_SEL
    lda (TMP2_ADR),y

    ldy page_len
    ldx TMP1_BNK
    stx BNK_SEL
    sta (TMP1_ADR),y

    inc nextpage_index
    inc page_len

    bra copy

copy_done:
    ;Set len of page to defrag after copy done
    lda TMP1_BNK
    sta BNK_SEL
    stz TMP1_ADR
    ldy #4
    lda page_len
    sta (TMP1_ADR),y

    ;Set len of page after page to defrag after copy done
    lda TMP2_BNK
    sta BNK_SEL
    stz TMP2_ADR
    sec
    lda nextpage_len
    sbc nextpage_index
    ldy #4
    sta (TMP2_ADR),y
    cmp #0
    beq free

move_nextpage:
    ;Move remaining content of page after page to defrag to start of that page
    lda TMP2_BNK
    sta BNK_SEL

    stz nextpage_index2
    lda #5
    sta TMP2_ADR

:   ldy nextpage_index
    cpy #251
    beq exit
    lda (TMP2_ADR),y
    ldy nextpage_index2
    sta (TMP2_ADR),y
    inc nextpage_index
    inc nextpage_index2
    bra :-

exit:
    rts

free:
    ldy TMP2_BNK
    ldx TMP2_ADR+1
    jmp mem_free

page_len = tempvars             ;1 byte
nextpage_len = tempvars +1      ;1 byte
nextpage_index = tempvars + 2   ;1 byte
nextpage_index2 = tempvars +3   ;1 byte
.endproc

;**********************************************************
;Function name.......: mem_push
;Description.........: Pushes content of a mem page
;                      one step forward
;Input...............: Y=mem bank, X=mem page, A=index to start pushing from
;Returns.............: C=1 if overflow; overflow char returned in .A
;Error returns.......: Nothing
.proc mem_push
    ;Save start index for later use
    sta start

    ;Set pointers to page to push
    sty BNK_SEL
    sty TMP1_BNK
    stx TMP1_ADR+1
    stz TMP1_ADR
    stx TMP2_ADR+1

    ;Get page len
    ldy #4
    x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y
    x816_data_dbr_off
    sta len

    ;If len = 0 there's nothing to push. Set len = 1 and exit
    cmp #0
    bne :+

    lda #1
    x816_data_dbr_on_keep_a TMP1_BNK
    sta (TMP1_ADR),y
    x816_data_dbr_restore
    clc
    rts
    
    ;Save last char of page, if there is overflow to return later
:   ldy #255
    x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y
    x816_data_dbr_off
    sta last_char

    ;Is start index >= 250, skip push routine
    lda start
    cmp #250
    bcc :+
    bra setlen
    
    ;Set offset used when pushing data
:   lda #5
    sta TMP1_ADR
    lda #6
    sta TMP2_ADR
    
    ldy #249

loop:
    x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y
    x816_data_dbr_off
    x816_data_dbr_on_keep_a TMP1_BNK
    sta (TMP2_ADR),y
    x816_data_dbr_restore

    cpy start
    beq setlen
    dey
    bra loop

setlen:
    ;Restore to default value
    stz TMP1_ADR

    ;Len < 251? Yes: Increase len before exit
    lda len
    cmp #251
    bcc :+

    ;Len >= 251. Don't increase len. Return overflow, indicated by carry=1
    sec
    lda last_char
    rts

:   inc len
    lda len
    ldy #4
    x816_data_dbr_on_keep_a TMP1_BNK
    sta (TMP1_ADR),y
    x816_data_dbr_restore
    
    clc
    rts

start = tempvars        ;1 byte
len = tempvars+1        ;1 byte
last_char = tempvars+2  ;1 byte
.endproc

;**********************************************************
;Procedure name......: mem_insert
;Description.........: Inserts a char at cursor location
;Input...............: A=char to insert
;Returns.............: Nothing
;Error returns.......: C=1 if mem is full
.proc mem_insert
    ;Collapse selections
    stz selection_active

    ;Save char on stack
    pha

    ;Check if we are at beginning of mem page, and if there is room to insert
    ;the char in previous mem page
    lda CRS_IDX
    beq :+
    jmp pushforward

:   lda CRS_BNK
    sta BNK_SEL
    sta TMP1_BNK

    ldy #1
    x816_data_dbr_on TMP1_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    bne :+
    jmp pushforward
:
    tax
    ldy #0
    x816_data_dbr_on TMP1_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    sta BNK_SEL
    sta TMP1_BNK
    stx TMP1_ADR+1
    stz TMP1_ADR

    ldy #4
    x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y
    x816_data_dbr_off
    cmp #251
    bcs pushforward

    ;There was room in previous mem page, let's insert char there
    tax     ;Temp storage for prev page len
    clc
    adc #5  ;Add 5 to skip mem page metadata
    tay
    pla     ;Get char from stack
    x816_data_dbr_on_keep_a TMP1_BNK
    sta (TMP1_ADR),y
    x816_data_dbr_restore

    inx     ;Increase prev page len
    txa
    ldy #4
    x816_data_dbr_on_keep_a TMP1_BNK
    sta (TMP1_ADR),y
    x816_data_dbr_restore

    dex             ;Decrease to represent last index in prev page

    ;Check if LNV_XXX is at CRS_XXX. If so, step left to char we inserted
    lda LNV_BNK
    cmp CRS_BNK
    bne :+
    lda LNV_ADR+1
    cmp CRS_ADR+1
    bne :+
    lda LNV_IDX
    cmp CRS_IDX
    bne :+

    lda TMP1_BNK
    sta LNV_BNK
    lda TMP1_ADR+1
    sta LNV_ADR+1
    stx LNV_IDX    

    ;Check if SCR_XXX is at CRS_XXX. If so, step left to char we inserted
:   lda SCR_BNK
    cmp CRS_BNK
    bne :+
    lda SCR_ADR+1
    cmp CRS_ADR+1
    bne :+
    lda SCR_IDX
    cmp CRS_IDX
    bne :+

    lda TMP1_BNK
    sta SCR_BNK
    lda TMP1_ADR+1
    sta SCR_ADR+1
    stx SCR_IDX

    ;Exit
:   lda #1
    sta mem_modified
    clc
    rts

pushforward:
    ldy CRS_BNK
    ldx CRS_ADR+1
    lda CRS_IDX
    jsr mem_push
    bcc insert

;Store overflow
    pha
    lda CRS_BNK
    sta BNK_SEL
    sta TMP1_BNK
    
    ldy #3
    x816_data_dbr_on TMP1_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    beq alloc
    
    tax
    
    ldy #2
    x816_data_dbr_on TMP1_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    
    stx TMP1_ADR+1
    stz TMP1_ADR
    sta BNK_SEL
    sta TMP1_BNK

    ldy #4
    x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y
    x816_data_dbr_off
    cmp #251
    bcs alloc
    
    ldy TMP1_BNK
    ldx TMP1_ADR+1
    lda #0
    jsr mem_push

    pla
    ldy #5
    stz TMP1_ADR
    x816_data_dbr_on_keep_a TMP1_BNK
    sta (TMP1_ADR),y
    x816_data_dbr_restore
    bra insert

alloc:
    jsr mem_alloc
    cpx #0          ;Mem full?
    beq mem_full
    sty BNK_SEL
    sty TMP1_BNK
    stx TMP1_ADR+1
    stz TMP1_ADR
    
    pla
    ldy #5
    x816_data_dbr_on_keep_a TMP1_BNK
    sta (TMP1_ADR),y
    x816_data_dbr_restore
    ldy #4
    lda #1
    x816_data_dbr_on_keep_a TMP1_BNK
    sta (TMP1_ADR),y
    x816_data_dbr_restore

insert:
    lda CRS_BNK
    sta BNK_SEL
    sta TMP1_BNK

    lda #5
    sta CRS_ADR

    pla
    ldy CRS_IDX
    x816_data_dbr_on_keep_a TMP1_BNK
    sta (CRS_ADR),y
    x816_data_dbr_restore

    stz CRS_ADR  

    lda #1
    sta mem_modified

    jsr mem_crs_step_right
    cpx #2
    bne :+
    sec
    rts
    
:   clc
    rts

mem_full:
    pla     ;Clean stack
    pla

    sec
    rts
.endproc

;**********************************************************
;Procedure name......: mem_delete
;Description.........: Deletes the char left of cursor
;Input...............: Nothing
;Returns.............: Nothing
;Error returns.......: C=1 if at file start, else C=0
;                      X=1 if at line break, else X=0
.proc mem_delete
    ;Collapse selections
    stz selection_active
    
    stz at_lf

    ;Cursor at beginning of file?
    jsr mem_at_bof
    bcc :+

    ldx #0          ;Yes, exit
    rts

    ;Cursor at beginning of page?
:   lda CRS_IDX
    bne default

    ;Yes, it was at beginning of page: decrease len of the previous page without moving the cursor pointer
    ;X816: never converted -- and this arm WRITES through (TMP1_ADR),y (the
    ;length decrement), so unbracketed it corrupted bank $00 as well as
    ;misreading the walk. TMP1_BNK shadows the bank exactly where the X16
    ;code moved BNK_SEL.
    lda CRS_BNK
    sta BNK_SEL
    sta TMP1_BNK
    lda CRS_ADR+1
    sta TMP1_ADR+1
    stz TMP1_ADR

:   ldy #0
    x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y
    x816_data_dbr_off
    tax
    ldy #1
    x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y
    x816_data_dbr_off
    sta TMP1_ADR+1
    stx BNK_SEL
    stx TMP1_BNK
    ldy #4
    x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y
    x816_data_dbr_off
    beq :-

    dea
    x816_data_dbr_on_keep_a TMP1_BNK
    sta (TMP1_ADR),y
    x816_data_dbr_restore

    tay
    lda #5
    sta TMP1_ADR
    x816_data_dbr_on TMP1_BNK
    lda (TMP1_ADR),y
    x816_data_dbr_off
    cmp #LF
    bne :+
    inc at_lf

:   ldx at_lf
    clc
    lda #1
    sta mem_modified

    rts

    ;We are not at beginning of the page: Move the cursor pointer one step left, decrease page len, and copy text from the cursor to end of page one step left
default:
    jsr mem_crs_step_left

    lda CRS_BNK             ;Check if we deleted a line feed
    sta BNK_SEL
    lda #5
    sta CRS_ADR
    ldy CRS_IDX
    x816_data_dbr_on CRS_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    cmp #LF
    bne :+
    inc at_lf

:   stz CRS_ADR             ;Decrease page len
    ldy #4
    x816_data_dbr_on CRS_BNK
    lda (CRS_ADR),y
    dea
    sta (CRS_ADR),y
    x816_data_dbr_restore

    lda #5
    sta CRS_ADR
    ldy CRS_IDX
    iny

    ;X816: ONE bracket around the whole copy loop -- every access is the same
    ;page, nothing else runs inside, and the loop's own flags come from cpy.
    x816_data_dbr_on CRS_BNK
:   lda (CRS_ADR),y         ;Copy text from cursor to page end one step left
    dey
    sta (CRS_ADR),y
    iny
    iny
    cpy #251
    bne :-
    x816_data_dbr_restore

    stz CRS_ADR             ;Restore vector

    ldy #4
    x816_data_dbr_on CRS_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    bne :+                  ;Mem page len > 0, exit

    jsr mem_at_eof          ;Mem page len = 0, check if we are at EOF, if not need to take one step left, otherwise the cursor would be at an undefined position
    bcs :+
    jsr mem_crs_step_left

:   ldx at_lf
    clc
    lda #1
    sta mem_modified
    rts

.segment "VARS"
    at_lf: .res 1
.CODE
.endproc

;**********************************************************
;Procedure name......: mem_get_value
;Description.........: Returns value at cursor position
;Input...............: Nothing
;Returns.............: A = value
.proc mem_get_value
    ldy CRS_BNK
    sty BNK_SEL
    sty TMP1_BNK
    ldy #5
    sty CRS_ADR
    ldy CRS_IDX
    x816_data_dbr_on TMP1_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    stz CRS_ADR
    rts
.endproc

;**********************************************************
;Procedure name......: mem_crs_step_right
;Description.........: Moves the pointer to memory location
;                      of the cursor one step right
;Input...............: Nothing
;Returns.............: .C=1 if eof else .C=0
;                      .X=1 if LF else .X=0
;Error returns.......: X=2 if memory full error
.proc mem_crs_step_right
    ;Is cursor at LF?
    stz at_lf

    lda CRS_BNK
    sta BNK_SEL
    sta TMP1_BNK
    clc
    lda CRS_IDX
    adc #5
    tay
    x816_data_dbr_on TMP1_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    cmp #LF
    bne :+
    inc at_lf

    ;Is cursor at end of file?
:   jsr mem_at_eof
    bcc :+

    lda CRS_BNK
    sta BNK_SEL
    sta TMP1_BNK
    ;TWO bugs lived in these four lines, and between them the right arrow
    ;could not move at all.
    ;
    ;_KEEP_A: the plain macro loads A with the bank number, so the compare
    ;tested the bank against the page length instead of the cursor index.
    ;
    ;THE FLAGS: x816_data_dbr_restore is a bare plb, and plb sets N and Z from
    ;the byte it pulled -- the caller's data bank, which is $00 here. So a
    ;`beq` placed after it tested "is the restored DBR zero", which it always
    ;is, and mem_crs_step_right reported END OF FILE on every call.
    ;
    ;So BRANCH BEFORE RESTORING. php/plp around the restore does not work and
    ;is worth saying why: the macro above has already pushed the data bank, so
    ;a php lands ON TOP of it, and then the plb pulls the STATUS byte as the
    ;bank while the plp pulls the bank as the status. Deciding first costs one
    ;duplicated restore and no stack games.
    lda CRS_IDX
    ina
    ldy #4
    x816_data_dbr_on_keep_a TMP1_BNK
    ;A NAMED label, not `:+`. ca65's anonymous labels are position-relative,
    ;so adding one here silently re-pointed the `bcc :+` further up -- the one
    ;that skips to "is cursor at end of mem page" -- at this block instead,
    ;and its extra plb then unbalanced the stack. ca65 said so ("No reference
    ;to unnamed label"); the warning was the only thing that did.
    cmp (CRS_ADR),y
    bne eof_not_page_end
    x816_data_dbr_restore
    jmp goto_eof
eof_not_page_end:
    x816_data_dbr_restore

    sec
    ldx #0
    rts

:   ;Is cursor at end of mem page?
    lda CRS_BNK
    sta BNK_SEL
    sta TMP1_BNK
    ;_KEEP_A again: A is the cursor index being compared, not scratch.
    lda CRS_IDX
    ina
    x816_data_dbr_on_keep_a TMP1_BNK
    cmp (CRS_ADR),y
    x816_data_dbr_restore           ;plb: carry survives, which is what bcc needs
    bcc default

    ;Go to next mem page
:   ldy #2
    x816_data_dbr_on TMP1_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    tax
    ldy #3
    x816_data_dbr_on TMP1_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    stx CRS_BNK
    stx BNK_SEL
    stx TMP1_BNK
    sta CRS_ADR+1
    stz CRS_IDX
    ldy #4
    x816_data_dbr_on TMP1_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    beq :-              ;Len = 0, continue searching...

    clc
    ldx at_lf
    rts

goto_eof:
    lda CRS_IDX
    cmp #250
    bne default

    jsr mem_alloc
    cpx #0              ;Mem full?
    beq mem_full

    sty CRS_BNK
    stx CRS_ADR+1
    stz CRS_IDX
    clc
    ldx at_lf
    rts

mem_full:
    ldx #2
    sec
    rts

default:
    inc CRS_IDX
    clc
    ldx at_lf
    rts

.segment "VARS"
    at_lf: .res 1
.CODE
.endproc

;**********************************************************
;Procedure name......: mem_crs_step_left
;Description.........: Moves the pointer to memory location
;                      of the cursor one step left
;Input...............: Nothing
;Returns.............: .C=1 if at beginning of file,
;                      otherwise .C=0
;                      .X=1 if line break, else .X=0
;Error returns.......: Nothing
.proc mem_crs_step_left
    ;Is cursor at beginning of file?
    jsr mem_at_bof
    bcc :+

    sec             ;Yes
    ldx #0
    rts

:   ;Is cursor at start of mem page?
    lda CRS_IDX
    bne default

    ;X816: this walk was never converted -- every (CRS_ADR),y here follows
    ;DBR, and with DBR=$00 it read bank $00 instead of the buffer page. It
    ;mostly LOOKED fine because the common case, stepping left inside a page,
    ;is the plain `dec CRS_IDX` below and touches no memory -- the misreads
    ;only fired crossing a page boundary and in the LF check at exit, which is
    ;precisely what line-start/line-end walks (and so the up and down arrows)
    ;live on. CRS_BNK is the bank byte the page links carry.
    lda CRS_BNK     ;Yes
    sta BNK_SEL

:   ldy #1          ;Prev page
    x816_data_dbr_on CRS_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    tax
    ldy #0          ;Prev bank
    x816_data_dbr_on CRS_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    sta CRS_BNK
    sta BNK_SEL
    stx CRS_ADR+1

    ldy #4          ;Page len
    x816_data_dbr_on CRS_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    beq :-          ;Len = 0, continue searching...

    dea             ;Set index to last char = len-1
    sta CRS_IDX

    bra exit

default:
    dec CRS_IDX

exit:
    ldx #0

    lda CRS_BNK
    sta BNK_SEL

    clc             ;Check if now standing at LF
    lda CRS_IDX
    adc #5
    tay
    x816_data_dbr_on CRS_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    cmp #LF
    bne :+
    ldx #1          ;Yes

 :  clc
    rts
.endproc

;**********************************************************
;Procedure name......: mem_crs_move_to_line_start
;Description.........: Moves the pointer to memory location
;                      at start of line
;Input...............: Nothing
;Returns.............: Numbers of steps the cursor was
;                      moved, 24 bit value, X=low, Y=mid, and
;                      A=high
;Error returns.......: Nothing
.proc mem_crs_move_to_line_start
    stz count
    stz count+1
    stz count+2
  
loop:
    stz CRS_ADR

    jsr mem_crs_step_left
    bcs exit

    inc count
    bne :+
    inc count+1
    bne :+
    inc count+2

:   lda CRS_BNK
    sta BNK_SEL

    ;X816: the read of the character under the cursor follows DBR. Unbracketed
    ;it read bank $00, never saw an LF, and this loop walked all the way to
    ;the start of file -- which is why the up arrow left the cursor where it
    ;was: cmd_go_up found itself "at the first line" every time.
    ldy CRS_IDX
    lda #5
    sta CRS_ADR
    x816_data_dbr_on CRS_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    cmp #LF
    bne loop

    stz CRS_ADR

    jsr mem_crs_step_right

    lda count
    bne :++
    lda count+1
    bne :+
    dec count+2
:   dec count+1
:   dec count

exit:
    ldx count
    ldy count+1
    lda count+2
   
    rts

.segment "VARS"
    count: .res 3
.CODE
.endproc

;**********************************************************
;Procedure name......: mem_crs_move_to_prev_blankspace
;Description.........: Moves the pointer to memory location
;                      at previous blank space, it will
;                      however stop at LF or BOF if no blank
;                      space was found before
;Input...............: Nothing
;Returns.............: Number of steps the cursor was moved,
;                      24 bit value, X=low, Y=mid, A=high
;Error returns.......: C=1 if LF found before a blank space
.proc mem_crs_move_to_prev_blankspace
    ;Reset counter
    stz counter
    stz counter+1
    stz counter+2

    ;First take one step left before we begin searching
    jsr mem_crs_step_left

    ;X816: this walk moves BACKWARDS across pages and the X16 code tracked the
    ;bank only in BNK_SEL, which is nothing on this machine. TMP1_BNK is the
    ;shadow -- it is scratch, and nothing called from inside the walk touches
    ;it (the step_left above runs BEFORE the shadow is set, the step_right at
    ;`found` runs after the last read).
    ;Get page len
    lda CRS_BNK
    sta BNK_SEL
    sta TMP1_BNK

    ldy #4
    x816_data_dbr_on TMP1_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    sta pagelen

check_first_char:
    lda CRS_BNK
    sta BNK_SEL
    sta TMP1_BNK

    lda #5
    sta CRS_ADR
    ldy CRS_IDX
    x816_data_dbr_on TMP1_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    cmp #32
    bne loop

    inc counter
    bra found

loop:
    ldy CRS_IDX
    x816_data_dbr_on TMP1_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    cmp #32
    beq found
    cmp #LF
    beq notfound

    inc counter
    bne :+
    inc counter+1
    bne :+
    inc counter+2

:   lda CRS_IDX
    beq prevpage
    dec CRS_IDX
    bra loop

prevpage:
    stz CRS_ADR

    ;Get prev page (header reads: still the CURRENT page's bank)
    ldy #0
    x816_data_dbr_on TMP1_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    tax
    ldy #1
    x816_data_dbr_on TMP1_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    beq notfound
    stx BNK_SEL
    stx TMP1_BNK            ;X816: the shadow moves WITH the walk
    sta CRS_ADR+1

    ;Get prev page len (now the PREV page's bank)
    ldy #4
    x816_data_dbr_on TMP1_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    sta pagelen
    beq prevpage
    
    tay
    dey
    sty CRS_IDX

    lda #5
    sta CRS_ADR
    bra loop


notfound:
    sec
    stz CRS_ADR
    ldx counter
    ldy counter+1
    lda counter+2
    rts

found:
    stz CRS_ADR
    jsr mem_crs_step_right
    
    clc
    ldx counter
    ldy counter+1
    lda counter+2
    rts

.segment "VARS"
    pagelen: .res 1
    counter: .res 3
.CODE

.endproc

;**********************************************************
;Procedure name......: mem_crs_move_to_line_end
;Description.........: Moves the pointer to memory location
;                      at end of line
;Input...............: Nothing
;Returns.............: Number of steps the cursor was moved,
;                      24 bit value, X=low, Y=mid, A=high
;Error returns.......: Nothing
.proc mem_crs_move_to_line_end
    stz count
    stz count+1
    stz count+2

    lda CRS_BNK
    sta BNK_SEL
loop:
    lda #5
    sta CRS_ADR

    ;X816: bracket per iteration, from CRS_BNK EACH time -- the step_right in
    ;this loop moves the cursor across pages and updates CRS_BNK when it does.
    ldy CRS_IDX
    x816_data_dbr_on CRS_BNK
    lda (CRS_ADR),y
    x816_data_dbr_off
    cmp #LF
    beq exit

    stz CRS_ADR
    jsr mem_crs_step_right
    bcs exit

    inc count
    bne loop
    inc count+1
    bne loop
    inc count+2
    bra loop

exit:
    stz CRS_ADR
    lda count+2 ;high
    ldy count+1 ;mid
    ldx count   ;low

    rts

.segment "VARS"
    count: .res 3
.CODE
.endproc

;**********************************************************
;Procedure name......: mem_crs_backup
;Description.........: Saves cursor pointers on stack
;Input...............: Nothing
;Returns.............: Nothing
;Error returns.......: Nothing
.proc mem_crs_backup
    ;Remove return address from stack
    plx
    ply

    ;Push cursor pointers to stack
    lda CRS_BNK
    pha
    lda CRS_ADR+1
    pha
    lda CRS_IDX
    pha

    ;Put back return address
    phy
    phx

    rts
.endproc

;**********************************************************
;Procedure name......: mem_crs_restore
;Description.........: Restors cursor pointers from stack
;Input...............: Nothing
;Returns.............: Nothing
;Error returns.......: Nothing
.proc mem_crs_restore
    ;Remove return address from stack
    plx
    ply

    ;Pull cursor pointers from stack
    pla
    sta CRS_IDX
    pla
    sta CRS_ADR+1
    pla
    sta CRS_BNK

    ;Put back return address
    phy
    phx

    rts
.endproc

;**********************************************************
;Procedure name......: mem_lnv_step_left
;Description.........: Moves the pointer to first visible
;                      char of current line
;Input...............: Nothing
;Returns.............: .C=1 if at beginning of file,
;                      otherwise .C=0
;Error returns.......: Nothing
.proc mem_lnv_step_left
    ;Backup CRS pointers
    jsr mem_crs_backup

    ;Move LNV pointers to CRS
    jsr mem_cp_lnv_crs

    ;Call CRS step function
    jsr mem_crs_step_left

    ;Copy result to LNV pointers
    jsr mem_cp_crs_lnv

    ;Restore CRS pointers
    jsr mem_crs_restore

    rts                 ;Can't be simplified to a jmp statement due to stack handling by mem_crs_restore!
.endproc

;**********************************************************
;Procedure name......: mem_lnv_step_right
;Description.........: Moves the pointer to first visble
;                      char of current line
;Input...............: Nothing
;Returns.............: .C = 1 if at end of file, 
;                      otherwise .C=0
;Error returns.......: Nothing
.proc mem_lnv_step_right
    ;Backup CRS pointers
    jsr mem_crs_backup

    ;Move LNV pointers to CRS
    jsr mem_cp_lnv_crs
    
    ;Call CRS step function
    jsr mem_crs_step_right

    ;Copy result to LNV pointers
    jsr mem_cp_crs_lnv
    
    ;Restore CRS pointers
    jsr mem_crs_restore
    
    rts                 ;Can't be simplified to a jmp statement due to stack handling by mem_crs_restore!
.endproc

;**********************************************************
;Procedure name......: mem_scr_move_down
;Description.........: Moves the pointer to first visible
;                      char of the screen one line down
;Input...............: Nothing
;Returns.............: .C = 1 if at end of file, 
;                      otherwise .C=0
;Error returns.......: Nothing
.proc mem_scr_move_down
    ;Prepare: backup CRS and copy SCR
    jsr mem_crs_backup
    jsr mem_cp_scr_crs

    ;Move pointer
    jsr mem_crs_move_to_line_end
    jsr mem_crs_step_right

    ;Copy SCR back and restore CRS
    jsr mem_cp_crs_scr
    jsr mem_crs_restore
    
    rts                 ;Can't be simplified to a jmp statement due to stack handling by mem_crs_restore!
.endproc

;**********************************************************
;Procedure name......: mem_scr_move_up
;Description.........: Moves the pointer to first visible
;                      char of the screen one line up
;Input...............: Nothing
;Returns.............: .C = 1 if at start of file, 
;                      otherwise .C=0
;Error returns.......: Nothing
.proc mem_scr_move_up
    ;Prepare: backup CRS and copy SCR
    jsr mem_crs_backup
    jsr mem_cp_scr_crs

    ;Move pointer
    jsr mem_crs_move_to_line_start
    jsr mem_crs_step_left
    jsr mem_crs_move_to_line_start

    ;Copy SCR back and restore CRS
    jsr mem_cp_crs_scr
    jsr mem_crs_restore
    
    rts                 ;Can't be simplified to a jmp statement due to stack handling by mem_crs_restore!
.endproc

;**********************************************************
;Procedure name......: mem_cp_crs_lnv
;Description.........: Copies CRS pointer to LNV pointer
;Input...............: Nothing
;Returns.............: Nothing
;Error returns.......: Nothing
.proc mem_cp_crs_lnv
    lda CRS_BNK
    sta LNV_BNK
    
    lda CRS_ADR
    sta LNV_ADR
    
    lda CRS_ADR+1
    sta LNV_ADR+1
    
    lda CRS_IDX
    sta LNV_IDX

    rts
.endproc

;**********************************************************
;Procedure name......: mem_cp_lnv_crs
;Description.........: Copies LNV pointer to CRS pointer
;Input...............: Nothing
;Returns.............: Nothing
;Error returns.......: Nothing
.proc mem_cp_lnv_crs
    lda LNV_BNK
    sta CRS_BNK
    
    lda LNV_ADR
    sta CRS_ADR
    
    lda LNV_ADR+1
    sta CRS_ADR+1
    
    lda LNV_IDX
    sta CRS_IDX

    rts
.endproc

;**********************************************************
;Procedure name......: mem_cp_crs_scr
;Description.........: Copies CRS pointer to SCR pointer
;Input...............: Nothing
;Returns.............: Nothing
;Error returns.......: Nothing
.proc mem_cp_crs_scr
    lda CRS_BNK
    sta SCR_BNK
    
    lda CRS_ADR
    sta SCR_ADR
    
    lda CRS_ADR+1
    sta SCR_ADR+1
    
    lda CRS_IDX
    sta SCR_IDX

    rts
.endproc

;**********************************************************
;Procedure name......: mem_cp_scr_crs
;Description.........: Copies SCR pointer to CRS pointer
;Input...............: Nothing
;Returns.............: Nothing
;Error returns.......: Nothing
.proc mem_cp_scr_crs
    lda SCR_BNK
    sta CRS_BNK
    
    lda SCR_ADR
    sta CRS_ADR
    
    lda SCR_ADR+1
    sta CRS_ADR+1
    
    lda SCR_IDX
    sta CRS_IDX

    rts
.endproc


;**********************************************************
;Procedure name......: mem_cur_col_inc
;Description.........: Increases column index counter
;Input...............: Nothing
;Returns.............: Nothing
;Error returns.......: Nothing
.proc mem_cur_col_inc
    inc mem_cur_col
    bne exit
    inc mem_cur_col+1
    bne exit
    inc mem_cur_col+2

exit:
    rts
.endproc

;**********************************************************
;Procedure name......: mem_cur_col_dec
;Description.........: Decreases column index counter
;Input...............: Nothing
;Returns.............: Nothing
;Error returns.......: Nothing
.proc mem_cur_col_dec
    lda mem_cur_col
    bne :++
    lda mem_cur_col+1
    bne :+
    dec mem_cur_col+2
:   dec mem_cur_col+1
:   dec mem_cur_col

    rts
.endproc

;**********************************************************
;Procedure name......: mem_cur_col_ret
;Description.........: Set column index to 1
;Input...............: Nothing
;Returns.............: Nothing
;Error returns.......: Nothing
.proc mem_cur_col_ret
    lda #1
    sta mem_cur_col
    stz mem_cur_col+1
    stz mem_cur_col+2
    rts
.endproc

;**********************************************************
;Procedure name......: mem_cur_line_inc
;Description.........: Increases line index counter
;Input...............: Nothing
;Returns.............: Nothing
;Error returns.......: Nothing
.proc mem_cur_line_inc
    inc mem_cur_line
    bne exit
    inc mem_cur_line+1
    bne exit
    inc mem_cur_line+2

exit:
    rts
.endproc

;**********************************************************
;Procedure name......: mem_cur_col_dec
;Description.........: Decreases line index counter
;Input...............: Nothing
;Returns.............: Nothing
;Error returns.......: Nothing
.proc mem_cur_line_dec
    lda mem_cur_line
    bne :++
    lda mem_cur_line+1
    bne :+
    dec mem_cur_line+2
:   dec mem_cur_line+1
:   dec mem_cur_line

    rts
.endproc

.segment "VARS"
    mem_run_defrag_page:    .res 2
    mem_modified:           .res 1              ;1 byte 
    mem_blocks_free:        .res 2              ;2 bytes
    mem_start:              .res 1              ;1 byte
    mem_top:                .res 1              ;1 byte
    mem_cur_col:            .res 3              ;3 bytes
    mem_cur_line:           .res 3              ;3 bytes
.CODE
