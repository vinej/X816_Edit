;*******************************************************************************
;x816_file.inc -- the editor's file I/O on the X816 kernel ABI.
;
;X816-ONLY. The X16 build never assembles a line of this file.
;
;It replaces the Kernal/IEC halves of file.inc -- SETNAM/SETLFS/OPEN/CHKIN/
;MACPTR/CHROUT/READST and the drive status channel -- with K_FS_OPEN,
;K_FS_READ, K_FS_WRITE and K_FS_CLOSE. What survives unchanged is the part
;that matters to the user: the line-break encoding is DETECTED on load and the
;same encoding is written back, tabs expand to the next tab stop, and the text
;still lands in the doubly-linked 256-byte pages mem.inc hands out.
;
;Three things here are X816's and not upstream's:
;
;  * Paths, not CBM names. NUL-terminated, '/' separated, resolved by the
;    kernel against the ONE working directory the shell, SuperBasic and
;    durexForth share. The editor keeps no cwd of its own. A leading '@' still
;    means "overwrite" because the editor's own save dialog adds one, and a
;    "0:" drive prefix is dropped rather than sent to a bus that is not there.
;
;  * Flat 24-bit buffers. The transfer buffer and the parameter blocks live in
;    the editor's reserved scratch pages (common.inc), reached with long
;    addressing so DBR can stay $00 for the editor's own variables and VERA.
;
;  * A truncated load can no longer eat the file it came from. If the buffer
;    fills before end of file, file_truncated is set and the path is
;    remembered; a later save to that same path is REFUSED and the user is
;    told to save as another name. On the X16 this wrote the partial buffer
;    over the original -- survivable for a source file you were editing,
;    silent data loss for an arbitrary file off the card, which is exactly
;    what "the editor is available from the console for any file" makes
;    routine.
;
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
;THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
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

.if ::target_mem=target_x816

;The slot the truncated-load guard remembers a source path in. Sized and
;placed with the other scratch fields in common.inc's page $C1:0100.
X816_FILE_SRC = X816_EDIT_BASE + $0180

;Editor error codes, reported in file_io_err and rendered by
;cmd_file_show_io_err.
;
;1-7 ARE the kernel's KERR_* codes, deliberately the same numbers: nothing has
;to translate between them, and a kernel that gains an error code cannot end
;up printing the message for a different one. 10 keeps upstream's meaning,
;"memory full", because the read loop reports it for the same reason.
X816_ERR_NONAME    = 8
X816_ERR_LONGNAME  = 9
X816_ERR_MEMFULL   = 10
X816_ERR_TRUNCATED = 11
X816_ERR_WRITE     = 12
X816_ERR_LAST      = 12

;The CBM "file exists" drive status. cmd_file_save keys its overwrite prompt
;to this value, so reporting it is what keeps that dialog working on a machine
;with no drive to report it.
X816_STATUS_EXISTS = $63

;******************************************************************************
;Function name.......: x816_fs_stage_path
;Purpose.............: Copies a file name into the flat, NUL-terminated path
;                      buffer the kernel calls take, stripping the CBM
;                      prefixes the editor's own dialogs still add
;Input...............: X=name lo, Y=name hi (bank $00), A=name length
;Returns.............: C=0 staged, x816_fs_pathlen set
;Error returns.......: C=1 and A = error code
.proc x816_fs_stage_path
    stx TMP1_ADR
    sty TMP1_ADR+1
    sta srclen
    stz x816_fs_overwrite

    lda srclen
    beq no_name

    ;A leading '@' means overwrite, here as on the X16: it is what
    ;cmd_file_save_overwrite prepends when the user answers the overwrite
    ;prompt.
    ldy #0
    lda (TMP1_ADR),y
    cmp #'@'
    bne :+
    lda #1
    sta x816_fs_overwrite
    iny

    ;A ':' before any '/' is a drive prefix -- "0:name", or the bare ':' the
    ;same dialog inserts. Drop everything up to and including it. A ':' after
    ;a '/' would be part of a name, so the scan stops at the first separator.
:   sty start
scan:
    cpy srclen
    beq copy_start
    lda (TMP1_ADR),y
    cmp #'/'
    beq copy_start
    cmp #':'
    beq found_colon
    iny
    bra scan

found_colon:
    iny
    sty start

copy_start:
    ldy start
    ldx #0
copy:
    cpy srclen
    beq copy_done
    cpx #X816_FILE_PATH_MAX
    beq too_long
    lda (TMP1_ADR),y
    sta f:X816_FILE_PATH,x
    iny
    inx
    bra copy

copy_done:
    cpx #0
    beq no_name
    stx x816_fs_pathlen
    lda #0
    sta f:X816_FILE_PATH,x
    clc
    rts

too_long:
    lda #X816_ERR_LONGNAME
    sec
    rts

no_name:
    lda #X816_ERR_NONAME
    sec
    rts

.segment "VARS"
    srclen: .res 1
    start:  .res 1
.CODE
.endproc

;******************************************************************************
;Function name.......: x816_fs_open
;Purpose.............: Opens the staged path
;Input...............: A = KOPEN_READ or KOPEN_WRITE
;Returns.............: C=0 and x816_fs_handle holds the handle
;Error returns.......: C=1 and A = KERR_* code
;Note................: The kernel ABI is entered by jsl in native mode with
;                      M=0 and X=0 -- 16-bit accumulator AND index registers.
;                      The editor runs 8-bit, so every wrapper in this file
;                      widens on the way in and narrows on the way out. rep,
;                      sep, pha and pla all leave carry alone, so the kernel's
;                      verdict survives the narrowing.
.proc x816_fs_open
    sta x816_fs_mode
    stz x816_fs_mode+1

    rep #$30
    .a16
    .i16
    lda #(X816_FILE_PATH & $ffff)
    ldx #(X816_FILE_PATH >> 16)
    ldy x816_fs_mode
    jsl X816_CALL_FS_OPEN
    bcs failed
    sta x816_fs_handle
    sep #$30
    .a8
    .i8
    clc
    rts

failed:
    .a16
    .i16
    sta x816_fs_ret
    lda #0
    sta x816_fs_handle
    sep #$30
    .a8
    .i8
    lda x816_fs_ret
    sec
    rts
.endproc

;******************************************************************************
;Function name.......: x816_fs_close
;Purpose.............: Closes the open file, if any
;Input...............: Nothing
;Returns.............: C=0
;Error returns.......: None. A refused close is ignored: it means the handle
;                      was already gone, which is not something the user can
;                      act on and not a reason to hide the real error that got
;                      us here.
.proc x816_fs_close
    rep #$30
    .a16
    .i16
    lda x816_fs_handle
    beq done
    jsl X816_CALL_FS_CLOSE
    lda #0
    sta x816_fs_handle
done:
    sep #$30
    .a8
    .i8
    clc
    rts
.endproc

;******************************************************************************
;Function name.......: x816_fs_blk_init
;Purpose.............: Fills in the fields of the K_FS_READ and K_FS_WRITE
;                      parameter blocks that do not change per call
;Input...............: Nothing
;Returns.............: Nothing
;Error returns.......: None
;Note................: doc/KERNEL.md 5.3: reserved fields must be written as
;                      zero so a later revision can define them. A 24-bit
;                      address occupies four bytes, so writing the low half
;                      and then the bank with a 16-bit store covers the
;                      reserved top byte with the zero it requires.
.proc x816_fs_blk_init
    rep #$30
    .a16
    .i16
    lda #(X816_FILE_BUF & $ffff)
    sta f:X816_FILE_RDBLK+2
    sta f:X816_FILE_WRBLK+2
    lda #(X816_FILE_BUF >> 16)
    sta f:X816_FILE_RDBLK+4
    sta f:X816_FILE_WRBLK+4
    lda #0
    sta f:X816_FILE_RDBLK+8
    sta f:X816_FILE_WRBLK+8
    sta f:X816_FILE_RDBLK+10
    sta f:X816_FILE_WRBLK+10
    sta f:X816_FILE_RDBLK+12
    sta f:X816_FILE_WRBLK+12
    lda #X816_FILE_BUF_SIZE
    sta f:X816_FILE_RDBLK+6
    sep #$30
    .a8
    .i8
    rts
.endproc

;******************************************************************************
;Function name.......: x816_fs_read
;Purpose.............: Reads up to X816_FILE_BUF_SIZE bytes into the transfer
;                      buffer with one kernel crossing
;Input...............: Nothing
;Returns.............: C=0 and A = bytes read, 0 meaning end of file
;Error returns.......: C=1 and A = KERR_* code
.proc x816_fs_read
    rep #$30
    .a16
    .i16
    lda x816_fs_handle
    sta f:X816_FILE_RDBLK
    lda #(X816_FILE_RDBLK & $ffff)
    ldx #(X816_FILE_RDBLK >> 16)
    jsl X816_CALL_FS_READ
    sta x816_fs_ret
    sep #$30
    .a8
    .i8
    lda x816_fs_ret
    rts
.endproc

;******************************************************************************
;Function name.......: x816_fs_write
;Purpose.............: Writes A bytes from the transfer buffer
;Input...............: A = byte count, 1..X816_FILE_BUF_SIZE
;Returns.............: C=0 and A = bytes written
;Error returns.......: C=1 and A = KERR_* code
.proc x816_fs_write
    sta x816_fs_ret
    stz x816_fs_ret+1

    rep #$30
    .a16
    .i16
    lda x816_fs_ret
    sta f:X816_FILE_WRBLK+6
    lda x816_fs_handle
    sta f:X816_FILE_WRBLK
    lda #(X816_FILE_WRBLK & $ffff)
    ldx #(X816_FILE_WRBLK >> 16)
    jsl X816_CALL_FS_WRITE
    sta x816_fs_ret
    sep #$30
    .a8
    .i8
    lda x816_fs_ret
    rts
.endproc

;******************************************************************************
;Function name.......: x816_status_stage
;Purpose.............: Copies a NUL-terminated string from the CODE bank into
;                      file_disk_status, where the rest of the editor can
;                      reach it with ordinary DBR=$00 addressing
;Input...............: X=string lo, Y=string hi
;Returns.............: Nothing
;Error returns.......: None
;Note................: The editor's literals live in the incbin, which is in
;                      the FIRMWARE bank, while screen_print_status reads its
;                      argument with (zp),y -- that is, through DBR, which
;                      must be $00 for the VERA writes in the same routine.
;                      Staging through a bank-$00 buffer is what squares the
;                      two. file_disk_status is that buffer: it was the IEC
;                      drive status channel, and this machine has no drive to
;                      report one.
.proc x816_status_stage
    stx TMP1_ADR
    sty TMP1_ADR+1
    ldy #0
copy:
    x816_code_dbr_on
    lda (TMP1_ADR),y
    x816_code_dbr_off
    sta file_disk_status,y
    beq done
    iny
    cpy #120
    bne copy
    lda #0
    sta file_disk_status,y
done:
    rts
.endproc

;******************************************************************************
;Function name.......: x816_status_print
;Purpose.............: x816_status_stage, then show it on the status line
;Input...............: X=string lo, Y=string hi
;Returns.............: Nothing
;Error returns.......: None
.proc x816_status_print
    jsr x816_status_stage
    ldx #<file_disk_status
    ldy #>file_disk_status
    jmp screen_print_status
.endproc

;******************************************************************************
;Function name.......: x816_show_io_err
;Purpose.............: Shows the message for an editor error code
;Input...............: X = error code, 1..X816_ERR_LAST
;Returns.............: Nothing
;Error returns.......: None
.proc x816_show_io_err
    cpx #0
    beq unknown
    cpx #X816_ERR_LAST+1
    bcs unknown
    dex
    txa
    asl
    tax

    ;The table and the strings are both in the code bank, so DBR has to move
    ;for the fetch -- and nothing may be STORED while it has moved, which is
    ;why both halves come out in registers before it goes back.
    x816_code_dbr_on
    lda x816_ioerr_tbl,x
    tay
    lda x816_ioerr_tbl+1,x
    x816_code_dbr_off
    sta TMP1_ADR+1
    tya
    sta TMP1_ADR
    ldx TMP1_ADR
    ldy TMP1_ADR+1
    jmp x816_status_print

unknown:
    ldx #<x816_msg_ioerr
    ldy #>x816_msg_ioerr
    jmp x816_status_print
.endproc

x816_ioerr_tbl:
    .addr x816_msg_nosys, x816_msg_notfound, x816_msg_nospace
    .addr x816_msg_badarg, x816_msg_ioerr, x816_msg_exists
    .addr x816_msg_notempty, x816_msg_noname, x816_msg_longname
    .addr x816_msg_memfull, x816_msg_truncated, x816_msg_writefail

x816_msg_nosys:     .byt "not supported by this kernel", 0
x816_msg_notfound:  .byt "file not found", 0
x816_msg_nospace:   .byt "no space left on the card", 0
x816_msg_badarg:    .byt "invalid file name", 0
x816_msg_ioerr:     .byt "i/o error", 0
x816_msg_exists:    .byt "file exists", 0
x816_msg_notempty:  .byt "directory not empty", 0
x816_msg_noname:    .byt "no file name given", 0
x816_msg_longname:  .byt "file name too long", 0
x816_msg_memfull:   .byt "memory full", 0
x816_msg_truncated: .byt "partial buffer: save under another name", 0
x816_msg_writefail: .byt "write failed, file may be incomplete", 0

x816_msg_loading:   .byt "loading...", 0
x816_msg_saving:    .byt "saving...", 0
x816_msg_loaded:    .byt "file loaded", 0
x816_msg_saved:     .byt "file saved", 0

;******************************************************************************
;Function name.......: x816_page_ptr
;Purpose.............: Points X816_P1 at the page the write cursor is on
;Input...............: CRS_BNK, CRS_ADR+1
;Returns.............: X816_P1 = flat address of the page base
;Error returns.......: None
.proc x816_page_ptr
    stz X816_P1
    lda CRS_ADR+1
    sta X816_P1+1
    lda CRS_BNK
    sta X816_P1+2
    rts
.endproc

;******************************************************************************
;Function name.......: x816_goto_start
;Purpose.............: Puts the cursor on the first character of the buffer
;Input...............: Nothing
;Returns.............: Nothing
;Error returns.......: None
.proc x816_goto_start
    lda #X816_EDIT_BASE_BANK
    sta CRS_BNK
    lda #X816_EDIT_BASE_PAGE
    sta CRS_ADR+1
    stz CRS_ADR
    stz CRS_IDX

    lda #1
    sta mem_cur_col
    sta mem_cur_line
    stz mem_cur_col+1
    stz mem_cur_col+2
    stz mem_cur_line+1
    stz mem_cur_line+2
    rts
.endproc

;******************************************************************************
;Function name.......: x816_remember_src
;Purpose.............: Remembers the staged path as the file a truncated
;                      buffer was read from
;Input...............: Nothing
;Returns.............: Nothing
;Error returns.......: None
.proc x816_remember_src
    ldx #0
loop:
    lda f:X816_FILE_PATH,x
    sta f:X816_FILE_SRC,x
    beq done
    inx
    cpx #X816_FILE_PATH_MAX
    bne loop
    lda #0
    sta f:X816_FILE_SRC,x
done:
    rts
.endproc

;******************************************************************************
;Function name.......: x816_path_is_src
;Purpose.............: Is the staged path the file a truncated buffer came
;                      from?
;Input...............: Nothing
;Returns.............: C=1 same file, C=0 a different one
;Error returns.......: None
.proc x816_path_is_src
    ldx #0
loop:
    lda f:X816_FILE_PATH,x
    cmp f:X816_FILE_SRC,x
    bne differ
    cmp #0
    beq same
    inx
    cpx #X816_FILE_PATH_MAX
    bne loop
same:
    sec
    rts
differ:
    clc
    rts
.endproc

;******************************************************************************
;Function name.......: x816_file_read
;Purpose.............: Reads a file into the buffer, replacing any current
;                      text. The X816 replacement for file.inc's file_read.
;Input...............: X=name lo, Y=name hi, A=name length
;Returns.............: Nothing. Cursor left at the start of the buffer.
;Error returns.......: Error code in file_io_err; file_truncated set if the
;                      buffer filled before end of file
.proc x816_file_read
    stz file_io_err
    stz file_truncated
    stz x816_fs_status

    jsr x816_fs_stage_path
    bcc :+
    jmp name_err

:   lda #KOPEN_READ
    jsr x816_fs_open
    bcc :+
    jmp open_err

:   jsr x816_fs_blk_init

    ldx #<x816_msg_loading
    ldy #>x816_msg_loading
    jsr x816_status_print

    ;The buffer is replaced wholesale, exactly as the X16 path does: mem_init
    ;clears the allocation bitmap, so every page the old text held is free
    ;again and the head page is empty.
    jsr mem_init
    jsr x816_page_ptr
    lda #5
    sta pos
    stz prev

fill:
    jsr x816_fs_read
    bcc :+
    jmp io_err
:   sta buflen
    bne :+
    jmp eof
:   stz index

byte_loop:
    ldx index
    lda f:X816_FILE_BUF,x

    ;Current char in A, previous char in X: the pair is what tells CR, LF and
    ;CRLF apart.
    ldx prev
    sta prev

    cmp #CR
    beq is_cr
    cpx #CR
    beq after_cr
    cmp #LF
    beq is_lf
    bra not_break

is_cr:
    lda #LINEBREAK_CR
    sta file_linebreak_encoding
    bra do_break

after_cr:
    ;The previous char was CR and already inserted the break. An LF here makes
    ;the file's encoding CRLF and is dropped; anything else is ordinary text.
    cmp #LF
    bne not_break
    lda #LINEBREAK_CRLF
    sta file_linebreak_encoding
    jmp next_byte

is_lf:
    lda #LINEBREAK_LF
    sta file_linebreak_encoding

do_break:
    stz mem_cur_col
    stz mem_cur_col+1
    stz mem_cur_col+2
    lda #LF
    jsr insert_char
    bcs mem_full
    jmp advance_col

not_break:
    cmp #KEYVAL_TAB
    beq do_tab
    jsr insert_char
    bcs mem_full
    jmp advance_col

do_tab:
    ;A tab becomes blanks up to the next tab stop, which is why the load loop
    ;has to track the column at all.
    jsr cmd_next_tab_stop
    stx tabdist
tab_loop:
    lda #32
    jsr insert_char
    bcs mem_full
    dec tabdist
    beq advance_col
    jsr mem_cur_col_inc
    bra tab_loop

advance_col:
    jsr mem_cur_col_inc

next_byte:
    inc index
    lda index
    cmp buflen
    bcs :+
    jmp byte_loop
:   jmp fill

    ;----------------------------------------------------------------------
    ;Insert one character at the write cursor, allocating a page when the
    ;current one is full. C=1 means the page pool is exhausted.
    ;
    ;pos is the write offset within the page, 5..255 -- bytes 0..4 are the
    ;page's links and its length. It is incremented past the last byte, so a
    ;wrap to zero IS the "page full" test, and no comparison is needed in the
    ;per-character path.
insert_char:
    ldy pos
    bne store
    jsr alloc_page
    bcs full
    ldy pos
store:
    sta [X816_P1],y
    inc pos
    clc
full:
    rts

alloc_page:
    pha                         ;the character being inserted must survive
    lda #251                    ;the page that just filled holds all of it
    ldy #4
    sta [X816_P1],y

    jsr mem_alloc               ;links the new page in after CRS_*
    cpx #0
    beq alloc_full

    sty CRS_BNK
    stx CRS_ADR+1
    stz CRS_ADR
    jsr x816_page_ptr

    lda #5
    sta pos
    lda #0
    ldy #4
    sta [X816_P1],y
    pla
    clc
    rts

alloc_full:
    pla
    sec
    rts
    ;----------------------------------------------------------------------

mem_full:
    ;The buffer filled before end of file. Upstream stopped here too, and left
    ;the partial text in place so the user can still work with it -- but it
    ;also left the way open to writing that partial text back over the file it
    ;came from. Remembering the path is what closes it.
    lda #1
    sta file_truncated
    jsr x816_remember_src
    lda #X816_ERR_MEMFULL
    sta file_io_err
    bra finish

eof:
    ldx #<x816_msg_loaded
    ldy #>x816_msg_loaded
    jsr x816_status_stage

finish:
    ;Record the length of the last page, then rewind.
    lda pos
    sec
    sbc #5
    ldy #4
    sta [X816_P1],y

    jsr x816_goto_start
    jmp x816_fs_close

io_err:
    sta file_io_err
    jsr x816_goto_start
    jmp x816_fs_close

open_err:
    sta file_io_err
    rts

name_err:
    sta file_io_err
    rts

.segment "VARS"
    buflen:  .res 1
    index:   .res 1
    prev:    .res 1
    pos:     .res 1
    tabdist: .res 1
.CODE
.endproc

;******************************************************************************
;Function name.......: x816_file_write
;Purpose.............: Writes the buffer to a file. The X816 replacement for
;                      file.inc's file_write.
;Input...............: X=name lo, Y=name hi, A=name length
;Returns.............: Nothing
;Error returns.......: Error code in file_io_err, or x816_fs_status =
;                      X816_STATUS_EXISTS when the file is there and no
;                      overwrite was requested
.proc x816_file_write
    stz file_io_err
    stz x816_fs_status

    jsr x816_fs_stage_path
    bcc :+
    jmp name_err

    ;A partial buffer may not be written back over its own source.
:   lda file_truncated
    beq check_exists
    jsr x816_path_is_src
    bcc check_exists
    lda #X816_ERR_TRUNCATED
    sta file_io_err
    rts

check_exists:
    ;K_FS_OPEN in write mode TRUNCATES an existing file, so "does it already
    ;exist" has to be asked separately. Asking keeps the editor's overwrite
    ;prompt -- and its answer, which prepends '@' -- working exactly as it
    ;does on the X16, where the drive answered the question by refusing.
    lda x816_fs_overwrite
    bne do_write
    lda #KOPEN_READ
    jsr x816_fs_open
    bcs do_write                ;will not open for reading: not there
    jsr x816_fs_close
    lda #X816_STATUS_EXISTS
    sta x816_fs_status
    ldx #<x816_msg_exists
    ldy #>x816_msg_exists
    jmp x816_status_stage

do_write:
    lda #KOPEN_WRITE
    jsr x816_fs_open
    bcc :+
    jmp open_err

:   jsr x816_fs_blk_init

    ldx #<x816_msg_saving
    ldy #>x816_msg_saving
    jsr x816_status_print

    ;Walk the buffer from the head page.
    lda #X816_EDIT_BASE_BANK
    sta X816_P1+2
    lda #X816_EDIT_BASE_PAGE
    sta X816_P1+1
    stz X816_P1
    stz x816_fs_outlen
    stz lastch

page_start:
    ldy #4
    lda [X816_P1],y
    sta left
    lda #5
    sta datapos

page_loop:
    lda left
    beq next_page
    ldy datapos
    lda [X816_P1],y
    jsr emit_char
    bcs write_err
    inc datapos
    dec left
    bra page_loop

next_page:
    ldy #3
    lda [X816_P1],y
    beq finish                  ;next page = 0 means end of the buffer
    tax
    ldy #2
    lda [X816_P1],y
    sta X816_P1+2
    stx X816_P1+1
    stz X816_P1
    bra page_start

finish:
    ;Give the file a closing line break if the text does not end with one.
    lda lastch
    beq add_break
    cmp #LF
    beq flush_out
    cmp #CR
    beq flush_out
add_break:
    lda #LF
    jsr emit_char
    bcs write_err

flush_out:
    jsr x816_fs_flush
    bcs write_err
    jsr x816_fs_close

    ;The buffer has a file of its own now, so it is no longer a partial copy
    ;of somebody else's.
    stz file_truncated

    ldx #<x816_msg_saved
    ldy #>x816_msg_saved
    jmp x816_status_stage

write_err:
    sta file_io_err
    jmp x816_fs_close

open_err:
    sta file_io_err
    rts

name_err:
    sta file_io_err
    rts

    ;----------------------------------------------------------------------
    ;Append one buffer character to the output, translating the editor's
    ;single LF into whatever the file itself used.
    ;
    ;This is the half of upstream's behaviour that must NOT be tidied into
    ;"always write one convention": SuperBasic's LOAD bug was a line-ending
    ;assumption, and an editor that silently normalised would reintroduce
    ;that class of bug from the other side.
emit_char:
    cmp #LF
    beq emit_break
    bra emit_raw

emit_break:
    lda file_linebreak_encoding
    beq emit_lf                 ;LINEBREAK_LF
    lda #CR
    jsr emit_raw
    bcs emit_fail
    lda file_linebreak_encoding
    cmp #LINEBREAK_CR
    beq emit_ok
emit_lf:
    lda #LF
    bra emit_raw
emit_ok:
    clc
emit_fail:
    rts

emit_raw:
    ldx x816_fs_outlen
    sta f:X816_FILE_BUF,x
    sta lastch
    inx
    stx x816_fs_outlen
    cpx #X816_FILE_BUF_SIZE-1   ;room left for the second byte of a CRLF
    bcc emit_no_flush
    jmp x816_fs_flush
emit_no_flush:
    clc
    rts
    ;----------------------------------------------------------------------

.segment "VARS"
    left:    .res 1
    datapos: .res 1
    lastch:  .res 1
.CODE
.endproc

;******************************************************************************
;Function name.......: x816_fs_flush
;Purpose.............: Writes out whatever the output buffer holds
;Input...............: Nothing
;Returns.............: C=0 written
;Error returns.......: C=1 and A = error code
.proc x816_fs_flush
    lda x816_fs_outlen
    beq done
    jsr x816_fs_write
    bcs failed
    ;A short write is the card filling up, not a transfer that "worked".
    ;Reporting bytes written and ignoring the count is how a save silently
    ;truncates.
    cmp x816_fs_outlen
    bne short_write
    stz x816_fs_outlen
done:
    clc
    rts

short_write:
    lda #X816_ERR_WRITE
failed:
    sec
    rts
.endproc

.segment "VARS"
    x816_fs_handle:    .res 2   ;16-bit: it is what the ABI passes and returns
    x816_fs_mode:      .res 2
    x816_fs_ret:       .res 2
    x816_fs_pathlen:   .res 1
    x816_fs_overwrite: .res 1
    x816_fs_status:    .res 1   ;what file_read_disk_status reports
    x816_fs_outlen:    .res 1
.CODE

.endif
