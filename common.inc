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

.import __ZP_START__, __ZP_LAST__, __VARS_LOAD__

.export APP_MOD

;*****************************************************************************
;KERNAL
;*****************************************************************************

;RAM and ROM select
.define BNK_SEL $00 
.define ROM_SEL $01

;Current character set
.define KERNAL_MODE $0372

;File management
.define KERNAL_CLALL $ffe7
.define KERNAL_CLOSE $ffc3
.define KERNAL_CLRCHN $ffcc 
.define KERNAL_SETLFS $ffba
.define KERNAL_SETNAM $ffbd
.define KERNAL_OPEN $ffc0
.define KERNAL_CHKIN $ffc6
.define KERNAL_CHKOUT $ffc9
.define KERNAL_CHROUT $ffd2
.define KERNAL_CHRIN $ffcf
.define KERNAL_READST $ffb7
.define KERNAL_MEMTOP $ff99
.define KERNAL_DECOMPRESS $feed
.define KERNAL_MACPTR $ff44
.define KERNAL_MCIOUT $feb1
.define KERNAL_SCREEN_MODE $ff5f
.define KERNAL_FETCH $ff74
.define KERNAL_SET_CHARSET $ff62
.define KERNAL_EXTAPI $feab
.define KERNAL_LOAD $ffd5
.define KERNAL_SETMSG $ff90

;Keyboard & mouse
.define KERNAL_GETIN $ffe4
.define KERNAL_MOUSE_CONFIG $ff68
.define KERNAL_MOUSE_GET $ff6b

;Other
.define KERNAL_SCREEN_SET_CHARSET $ff62

;*****************************************************************************
;VERA
;*****************************************************************************
.define VERA_L $9f20
.define VERA_M $9f21
.define VERA_H $9f22

.define VERA_D0 $9f23
.define VERA_D1 $9f24

.define VERA_CTRL $9f25
.define VERA_ISR $9F27
.define VERA_VIDEO $9f29

.define VERA_L0_CONFIG $9f2d
.define VERA_L0_MAPBASE $9f2e
.define VERA_L0_TILEBASE $9f2f

.define VERA_L1_CONFIG $9f34
.define VERA_L1_MAPBASE $9f35
.define VERA_L1_TILEBASE $9f36

.if .defined(target_mem) .and target_mem=target_x816
    .define VERA_BUFADR_M $00
    .define VERA_BUFADR_H $00
.else
    .define VERA_BUFADR_M $b0
    .define VERA_BUFADR_H $01
.endif

.define VERA_SPRITE_ATTR $1fc00   ; VRAM address
.define VERA_POINTER_ADDR $0      ; VRAM address where custom mouse pointer is stored

;*****************************************************************************
;ZERO PAGE "REGISTERS"
;*****************************************************************************
r0 = $02
r1 = $04
r2 = $06
r3 = $08
r4 = $0a
r5 = $0c
r6 = $0e
r7 = $10
r8 = $12
r9 = $14
r10 = $16
r11 = $18
r12 = $1a
r13 = $1c
r14 = $1e
r15 = $20

;*****************************************************************************
;ZERO PAGE VECTORS AND VARIABLES
;*****************************************************************************
.segment "ZEROPAGE"

;Pointer to memory location: Cursor position
CRS_BNK:    .res 1   ;Bank
CRS_ADR:    .res 2   ;Address
CRS_IDX:    .res 1   ;Index

;Pointer to memory location: First visible character of the currently edited line
LNV_BNK:    .res 1   ;Bank
LNV_ADR:    .res 2   ;Address
LNV_IDX:    .res 1   ;Byte index

;Pointer to memory location: First visible character of the screen (top left)
SCR_BNK:    .res 1   ;Bank
SCR_ADR:    .res 2   ;Address
SCR_IDX:    .res 1   ;Index

;Temporary pointers to memory locations, used for miscellaneous (short-term) purposes
;Warning: Any routine may use these temp pointers at any time and for whatever reason
TMP1_BNK:   .res 1   ;Bank
TMP1_ADR:   .res 2   ;Address MSB
TMP1_IDX:   .res 1   ;Index

TMP2_BNK:   .res 1   ;Bank
TMP2_ADR:   .res 2   ;Address MSB
TMP2_IDX:   .res 1   ;Index

;Zero page addresses reserved for printer drivers
.assert *=$36, error, "PRINTER_ZP_VARS not at address $36"
PRINTER_ZP_VARS:
            .res 4

;X816: 24-bit pointers for flat [dp],y accesses. Long indirect ignores DBR
;entirely, which is both shorter and safer than bracketing every access with
;phb/plb -- and it is the only way to reach the page pool and the scratch
;pages while DBR stays $00 for the editor's own variables and for VERA.
.if .defined(target_mem) .and target_mem=target_x816
X816_P1:    .res 3
X816_P2:    .res 3
.endif
.CODE

;*****************************************************************************
;Global variables not stored in zero page
;*****************************************************************************
.segment "VARS"

bridge_code:            .res 71             ;Bridge code, used by ROM version of the program
APP_MOD:                .res 1              ;1 byte, application mode
APP_QUIT:               .res 1              ;1 byte, used to signal program exit
.if target_mem=target_rom
rom_bank:               .res 1              ;Bank where ROM version of the program is stored
rom_bank_save:          .res 1
.endif
tempvars:               .res 16             ;Temporary variables

.CODE

;*****************************************************************************
;VARIABLES/BUFFERS STORED IN RAM BANK 1
;*****************************************************************************
mem_map                                 = $a000               ;1024 bytes ($a000-$a3ff)
clipboard_mem                           = $a400               ;3072 bytes ($a400-$afff)
ram_backup_storage                      = $b000               ;1280 bytes ($b000-$b4ff)
dir_entry                               = $b500               ;256 bytes ($b500-$b5ff)
rambuf_backup                           = $b600               ;768 bytes ($b600-$b8ff)
help_txt                                = $b900               ;1792 bytes ($b900-$bfff)

;*****************************************************************************
;CONSTANTS
;*****************************************************************************
.define LF 10
.define CR 13          

.if .defined(target_mem) .and target_mem=target_x816
    ;Every kernel address, call number and error code comes from
    ;X816_core/tools/contract.py, generated into this include. Nothing below
    ;may hand-copy one of them: run `python tools/contract.py --check` in
    ;../X816_core after touching anything that looks like an ABI constant.
    .include "x816_contract.inc"

    ;Kernel entry addresses: the table base plus the call number times the
    ;stride, all three from the contract. A caller that computes them this way
    ;cannot get the stride right for one call and wrong for another.
    X816_CALL_CON_PUTC   = KERN_TABLE + K_CON_PUTC   * KERN_ENTRY_SIZE
    X816_CALL_CON_GETKEY = KERN_TABLE + K_CON_GETKEY * KERN_ENTRY_SIZE
    X816_CALL_CON_CLS    = KERN_TABLE + K_CON_CLS    * KERN_ENTRY_SIZE
    X816_CALL_FS_OPEN    = KERN_TABLE + K_FS_OPEN    * KERN_ENTRY_SIZE
    X816_CALL_FS_CLOSE   = KERN_TABLE + K_FS_CLOSE   * KERN_ENTRY_SIZE
    X816_CALL_FS_READ    = KERN_TABLE + K_FS_READ    * KERN_ENTRY_SIZE
    X816_CALL_FS_WRITE   = KERN_TABLE + K_FS_WRITE   * KERN_ENTRY_SIZE
    X816_CALL_FS_DELETE  = KERN_TABLE + K_FS_DELETE  * KERN_ENTRY_SIZE
    X816_CALL_IRQ_FRAMES = KERN_TABLE + K_IRQ_FRAMES * KERN_ENTRY_SIZE

    ;K_FS_OPEN's mode. Single-sourced in X816_Calypsi/runtime/kfs.h and
    ;X816_Library/src_acme/core/const_kernel.asm under these names; kept here
    ;rather than in the contract because the contract does not carry them yet.
    KOPEN_READ  = 0
    KOPEN_WRITE = 1

    ;The editor's page pool, derived from the contract's flat addresses so a
    ;change to the memory map cannot leave a stale bank number here.
    ;
    ;EQUATES, NOT `.define`: ca65 reads the `(` after a .define name as the
    ;start of a PARAMETER LIST, so `.define FOO (BAR >> 16)` declares a macro
    ;taking one argument and every plain use of FOO then fails with "macro
    ;parameter #1 is empty". Equates also make `<FOO` and `>FOO` mean the low
    ;and high byte of the whole expression, which textual substitution does
    ;not -- `<` would bind to the first term only.
    X816_EDIT_BASE_BANK  = X816_EDIT_BASE >> 16
    X816_EDIT_LAST_BANK  = X816_EDIT_LAST >> 16
    X816_EDIT_PAGE_COUNT = X816_EDIT_SIZE >> 8

    ;--------------------------------------------------------------------------
    ;The editor's flat scratch pages: the first eight pages of the pool.
    ;
    ;They are here and not in bank $00 because bank $00 belongs to the CALLER.
    ;X816_Calypsi/runtime/x816-lib.scm claims $0100-$1FFF for a loadable
    ;program's stack, $3000-$9DFF for its data and heap and $A000-$FDFF for
    ;its near data -- so durexForth and SuperBasic own almost all of it while
    ;the editor runs. mem_init marks pages $00-$08 of the base bank allocated,
    ;so the allocator can never hand any of this out.
    ;
    ;  $C1:0000  file_buf, the K_FS_READ/K_FS_WRITE transfer buffer   256 B
    ;  $C1:0100  the staged path, then the FS parameter blocks
    ;  $C1:0200  ram_backup: the caller's $0400-$07FF               1,024 B
    ;  $C1:0600  ram_backup: the caller's zero page from $22
    ;  $C1:0700  spare -- dir_entry's home when dir.inc is ported
    ;  $C1:0800  head page of the text buffer
    ;  $C1:0900  first page mem_alloc may hand out
    ;--------------------------------------------------------------------------
    X816_EDIT_SCRATCH_PAGES = 8
    X816_EDIT_BASE_PAGE     = $08
    X816_FILE_BUF           = X816_EDIT_BASE + $0000
    X816_FILE_BUF_SIZE      = 255
    X816_FILE_PATH          = X816_EDIT_BASE + $0100
    X816_FILE_PATH_MAX      = 79
    X816_FILE_RDBLK         = X816_EDIT_BASE + $0160
    X816_FILE_WRBLK         = X816_EDIT_BASE + $0170
    X816_RAM_BACKUP         = X816_EDIT_BASE + $0200
    X816_RAM_BACKUP_ZP      = X816_EDIT_BASE + $0600

    X816_EDIT_MAP_BANK          = $df
    X816_EDIT_MAP_PAGE          = $fc
    X816_EDIT_MAP_BYTES         = $03e0
    X816_EDIT_RESERVED_TOP_MASK = $f0

    ;What mem_init must report as free, spelled out as the arithmetic rather
    ;than as a magic number -- a reserved page added without moving this count
    ;is an allocator that hands out memory it has also promised to something
    ;else. The four reserved top pages are the allocation bitmap itself, and
    ;the thirty are the page-$00 null-link sentinels in banks $C2-$DF.
    X816_EDIT_FREE_PAGES = X816_EDIT_PAGE_COUNT - X816_EDIT_SCRATCH_PAGES - 1 - 30 - 4
    .define x816_edit_arg_ptr $07f8
    .define x816_edit_arg_len $07fb
    .define x816_mem_smoke_request $07fe
    .define x816_mem_smoke_result $07fd
    .define x816_mem_smoke_detail $07fc
    .define x816_smoke_exit $07ff

    .macro x816_code_dbr_on
        phb
        phk
        plb
    .endmacro

    .macro x816_code_dbr_off
        plb
        cmp #0
    .endmacro

    .macro x816_data_dbr_on bank
        phb
        lda bank
        pha
        plb
    .endmacro

    .macro x816_data_dbr_on_keep_a bank
        phb
        pha
        lda bank
        pha
        plb
        pla
    .endmacro

    .macro x816_data_dbr_off
        plb
        cmp #0
    .endmacro

    .macro x816_data_dbr_restore
        plb
    .endmacro
.else
    .macro x816_code_dbr_on
    .endmacro

    .macro x816_code_dbr_off
    .endmacro

    .macro x816_data_dbr_on bank
    .endmacro

    .macro x816_data_dbr_on_keep_a bank
    .endmacro

    .macro x816_data_dbr_off
    .endmacro

    .macro x816_data_dbr_restore
    .endmacro
.endif

.define SCREEN_TEXTMODE 0

.if SCREEN_TEXTMODE = 0
    .define SCREEN_WIDTH 80
    .define SCREEN_HEIGHT 60
.elseif SCREEN_TEXTMODE = 1
    .define SCREEN_WIDTH 80
    .define SCREEN_HEIGHT 30
.elseif SCREEN_TEXTMODE = 2
    .define SCREEN_WIDTH 40
    .define SCREEN_HEIGHT 60
.elseif SCREEN_TEXTMODE = 3
    .define SCREEN_WIDTH 40
    .define SCREEN_HEIGHT 30
.elseif SCREEN_TEXTMODE = 4
    .define SCREEN_WIDTH 40
    .define SCREEN_HEIGHT 15
.else
    .error "Unsupported screen mode"
.endif
