# X816_Edit

X16 Edit ported to the **X816** — a flat 16 MB, native-mode-only 65C816 MiSTer
core (8 MHz average, 14 MHz with `SYSCTL[2]` TURBO) — as the machine's
**resident text editor**: it lives in the kernel firmware and is *called* from
durexForth, SuperBasic and the shell, returning to whichever called it.

This repo is the editor. The machine lives in sibling repos:

- `../X816_core` — RTL, docs (`doc/KERNEL.md`, `doc/MEMORY_MAP.md`,
  `doc/SHELL.md`), `tools/contract.py`, release tooling
- `../X816_Calypsi` — the kernel/runtime: `runtime/console.[ch]`, `kmem.c`,
  `kerntab.s`, `shell.c`, the linker maps, and `programs/shell/build.sh` which
  produces the `kernel.bin` (`boot2.rom`) this editor has to end up inside
- `../X816_Emulator` — the emulator (`build/x16emu.exe`, trace build in
  `build-trace/`)
- `../X816_DurexForth`, `../X816_SuperBasic` — the two callers
- `../X16Edit_ref` — **pinned, untouched upstream. The oracle. Never edit it.**

**The user runs Quartus builds and MiSTer hardware tests themselves — prepare
changes, never launch FPGA builds.**

## This is a fork. Keep it one.

`upstream` is `github.com/stefan-b-jakobsson/x16-edit` with all 341 commits of
real history; `origin` is `github.com/vinej/X816_Edit`. Work on **`x816`** (the
default branch); `master` tracks upstream so a merge has a clean base:

```sh
git fetch upstream && git merge upstream/master   # on x816
```

Two consequences that shape everything:

* **The ca65 sources stay ca65.** Converting ~13,000 lines to Calypsi syntax
  would end the merge path. The build assembles this tree with ca65 at a fixed
  firmware address and `.incbin`s the blob into a small Calypsi stub —
  `as65816` has `.incbin` (verified in the 5.18 assembler manual).
* **Prefer changes upstream would accept**, and isolate what it would not into
  X816-only files. A diff that merges is worth more than a diff that is tidy.

## Licence — not cosmetic

X16 Edit is **BSD-2-Clause, © Stefan Jakobsson 2022-2024** (`license`). The
notice and disclaimer must be reproduced in source **and** in the documentation
of any binary distribution — and this ships inside `boot2.rom`, so that
obligation is live. Keep every file header. New X816-only files carry their own
and say what they are.

## State (2026-08-12): file I/O ported — `edit <file>` loads and saves for real

All three callers now **open a file, edit it and save it**: the shell's
`edit [file]`, SuperBasic's `EDIT "name"` and durexForth's `S" name" edit` all
reach `main_x816_entry`, which loads the named file through `K_FS_OPEN`/
`K_FS_READ` before entering the main loop. Ctrl+S writes back through
`K_FS_WRITE`. Proof, all green:

| Script | What it proves |
|---|---|
| `../X816_Calypsi/programs/shell/run-editfile.sh` | load → render → save → **read back on a fresh machine**, byte for byte and at exactly the source's size; `--negative` requires a missing file to be reported |
| `../X816_SuperBasic/run-edit-smoke.sh` | the language caller's `EDIT "HELLO.TXT"` really loads it (the assertion was checked to fail with a name that is not on the card) |
| `run-editmem.sh`, `run-edit.sh`, `run-edittype.sh`, `../X816_DurexForth/run-edit-smoke.sh` | still pass with the new page layout |

The new X816-only code is **`x816_file.inc`**. `file.inc`'s Kernal bodies stay
in the tree untouched, with a one-line `jmp` dispatch at the top of `file_read`
and `file_write`, so a merge from upstream still applies to them.

Three things settled on the way, each of which was a live hazard:

* **The editor's writable buffers may not live in bank $00** — that bank
  belongs to the *caller*. `x816-lib.scm` gives a loadable program
  `$0100-$1FFF` for its stack, `$3000-$9DFF` for data and heap and
  `$A000-$FDFF` for near data, so durexForth and SuperBasic own nearly all of
  it while the editor runs. The transfer buffer, the path buffer and the FS
  parameter blocks now live in **eight reserved scratch pages at the bottom of
  the page pool** (`$C1:0000-$C1:07FF`, laid out in `common.inc`); the buffer
  head page moved to `$C1:0800` and `mem_init` marks all nine allocated.
  `X816_EDIT_FREE_PAGES` states the free count as arithmetic, so a reserved
  page added without moving the count is a build-time change, not a silent
  double-booking.

* **`ram.inc` was corrupting its caller.** `ram_backup_storage` is `$B000`,
  which on the X16 meant a banked RAM bank the editor owned and here means
  `$00:B000` — a loadable program's `near` data. Every `edit` from durexForth
  or SuperBasic wrote 1,280 bytes over it. The X816 branch now backs up to the
  scratch pages with long (`f:`) addressing.

* **VARS could silently overrun the hand-off block.** `$07F8-$07FF` holds the
  filename pointer, its length and the smoke-request bytes at fixed addresses
  no linker owns. `VARMEM` in `conf/x816-edit.cfg` now stops at `$07F7`, so
  growth there is a link error. VARS ends at `$07CE` — 41 bytes of headroom.

**The X16 targets assemble again.** `main.asm` exported `main_x816_entry`
unconditionally while defining it only under `target_mem=target_x816`, so
`target_mem=1` and `2` failed outright — the fork's merge path was broken and
nothing was checking. The export moved next to the definition. `make ram`/`rom`
still cannot *link* here because `lzsa` is absent (below), but
`ca65 -Dtarget_mem=1` and `-Dtarget_mem=2` now assemble clean with stub help
blobs, which is the part this tree can verify. **Check it after touching a
shared `.inc`.**

**This repo is now a contract consumer.** `contract.py`'s ca65 emitter also
writes `X816_Edit/x816_contract.inc`, and it now carries the kernel call
numbers, so `K_FS_OPEN` and friends are computed as
`KERN_TABLE + K_FS_OPEN * KERN_ENTRY_SIZE` rather than hand-copied. The
duplicate `KEY_CTRL`/`KEY_ALT`/`K_CON_*` equates in `x816_kernal.inc` are gone
— the assembler rejected them, which is the mechanism working. `--check`
passes; only additions landed in `boot/x816_contract.inc`, so `boot.hex` is
unaffected.

## State (2026-08-11): kernel side done, keyboard ABI started, real editor entry reached

**Done, pushed and verified — in `../X816_Calypsi` and `../X816_core`:**

| Constant | Value | |
|---|---|---|
| `X816_KDATA_BASE` | `$C0:0000` | kernel writable-data region, 2 MB |
| `X816_KDATA_FAR` | `$C0:0000` | bank `$C0` — the kernel's own `far` data |
| `X816_EDIT_BASE` | `$C1:0000` | **the editor's page pool** |
| `X816_EDIT_LAST` | `$DF:FFFF` | 2,031,616 B = 7,936 pages of 256 |
| `KMEM_REGION_EDIT` | 0 | `K_MEM_RELEASE` region id |

Reserved at boot. `K_MEM_RELEASE` hands it to the kernel heap for the rest of a
session — one way, reboot is the undo — taking user space 12 MB → 14 MB, and the
editor must then **refuse to open**. `K_MEM_TOP` reports the live ceiling and
every allocator is required to ask. Proof: `../X816_Calypsi/programs/shell/run-mem.sh`
plus `--negative`. Spec: `../X816_core/doc/MEMORY_MAP.md` §1.1.

**The region is not a linker section in any map and must never become one.**
`K_EXIT` restarts the kernel through `cstartup`, which initialises anything the
linker placed. Same rule as the carry-over block at `$20:A0-$20:FF`
(`x816-kernel.scm`). It buys something real: **the buffer survives between
invocations**, so edit → run → edit keeps your text. That needs a magic word and
a page count to tell a live buffer from power-up SDRAM noise, and a decision —
`edit` with no argument should RESUME, `edit <file>` open fresh. `mem_init`
currently allocates a first page unconditionally.

**Ctrl/Alt key classes are now implemented locally** in
`../X816_core/tools/contract.py` and `../X816_Calypsi/runtime/console.[ch]`:
`KEY_CTRL = $0200`, `KEY_ALT = $0400`, composable with `KEY_SPECIAL`. The
contract check passes, the Calypsi shell/kernel build passes, and the existing
keyboard GIF smoke check still proves ordinary keys and Shift. Hardware
confirmation of the raw Ctrl/Alt positions is still required.

**Build plumbing and resident entry now run.** `make x816` assembles the ca65
sources with `target_mem=3`, links them with `conf/x816-edit.cfg`, and produces
`build/x816-edit.bin` at bank-local `$2000`. `x816_entry.asm` exports
`x816_edit_default_entry`, a native-mode-safe thunk that saves P/DBR/D, switches
to 8-bit A/X/Y with D/DBR in bank 0, calls `main_default_entry`, then restores
the caller context and returns with `rtl`. The Calypsi kernel incbins the blob
at `$F1:2000`; shell `edit` enters the real editor and reinitializes the
resident console after return.

The first editor screen now renders readable header and both footer rows. The gotcha was
65816 `DBR`: mutable editor variables and VERA need DBR=0, while literal strings
inside the incbin live in the executing firmware bank. X816-only
`x816_code_dbr_on/off` brackets the initial literal fetches and restores DBR=0
before VERA writes. `x816_code_dbr_off` must not lose the loaded byte's zero
state; it restores DBR and then `cmp #0`.

The current X816 shim gives the editor the resident console's layer-0 map/font
layout, skips rc-file and mouse setup, polls instead of waiting on an installed
editor IRQ, and maps X816 `con_getkey` events back to X16-Edit's 8-bit key
values. `programs/shell/run-edit.sh` uses resident-only `editsmk`, which sets a
smoke flag, lets the first screen sit long enough to be captured, then exits
after editor setup. It proves readable render of the header plus both footer
rows and return without pretending file I/O, the page pool, or full command
handling are finished.

The first `mem.inc` slice has moved the initial buffer head page from the old
X16 bank window (`mem_start+1:$A0`) to `$C1:0100`. X816 display reads now set
DBR to the buffer page bank only for the `(ptr),y` access and restore DBR=0
before touching editor variables or VERA. This proves the current empty-buffer
render path can use the reserved editor region.

The X816 `mem_alloc`/`mem_free` branch now uses a 992-byte bitmap in the last
four editor-region pages (`$DF:FC00-$DF:FFFF`) and allocates flat 256-byte pages
from `$C1:0200-$DF:FBFF`; `$C1:0100` is the head page. Page byte `0` is still
the inherited null-link sentinel, so page `00` in every bank is reserved until
the later link-format rewrite. `programs/shell/run-editmem.sh` now proves
allocate/link/free behavior in the emulator. X816 defrag is deliberately gated
off until it can be ported to flat DBR-scoped page access. `run-edittype.sh`
now proves a minimal insert/render/exit/return path through the editor's normal
key handlers. Multi-page editing, defrag, file load/save, and persistent buffer
metadata remain open.

## First prerequisite — mostly not in this repo

X16 Edit's entire UI is Ctrl+letter (`keyboard_ctrl_keyval`,
`keyboard_ctrl_jmptbl`). The runtime now tracks Ctrl and Alt as state and
returns a 16-bit composite key event rather than stealing CP437 control glyphs:
`KEY_CTRL|'c'`, `KEY_ALT|'x'`, or `KEY_CTRL|KEY_SPECIAL|n` for special keys.

**Confirm the keycodes on hardware** with a `KEYSCAN`-style probe before
building editor behavior on Ctrl/Alt. `console.h` records that the last time
these were assumed rather than measured they were wrong in *both* places at
once and agreed with each other.

## The contract is single-sourced. Do not hand-copy a constant.

Every address, call number and error code comes from
`../X816_core/tools/contract.py`, generated into `x816_contract.h` /
`.inc` for five consumers:

```sh
cd ../X816_core && python tools/contract.py --write   # regenerate
cd ../X816_core && python tools/contract.py --check   # must PASS
```

`--check` also verifies the sites that keep their own literal (`x816.sv`, the
linker scripts, `mksdcard.py`, and x16lib's `const_kernel.asm`). **Adding a
kernel call means adding it to `contract.py` AND to
`../X816_Library/src_acme/core/const_kernel.asm`**, or `--check` fails.

`K_EDIT` is **slot 34** (`$00:FE88`), the "programs" group beside `K_EXEC` (32)
and `K_EXIT` (33). It takes `C:X` as a zero-terminated filename pointer, or `0`
for an unnamed buffer, and returns after the resident editor exits. The filename
is copied into the editor session now; actual load/save still waits for the
`file.inc` port to `K_FS_*`. The slot is in `contract.py`.

**The shell needs no slot.** `../X816_Calypsi/programs/shell/kernelmain.c` is
`con_init(); kern_install(); kirq_install(); ccur_on(); sh_run();` — the shell
**is** the resident kernel, so `cmd_edit` calls the editor directly, in-image.
That makes the console path the cheapest of the three callers and the right
first milestone: testable before either language shim exists. Add one row to
`sh_commands[]` in `runtime/shell.c` (`{ "edit", "edit [file]", 0, 1, cmd_edit }`)
— `doc/SHELL.md` §4 calls that table *the* extension point and §7 promises a
later scripting layer inherits it.

## Toolchain

`ca65`, `ld65` and `cl65` are on PATH from `/c/Emulator/cc65/bin`. Calypsi lives
at `../X816_Calypsi/Calypsi/calypsi-65816-5.18` and its env comes from
`../X816_Calypsi/runtime/calypsi.sh` (sourced by every run script; derives its
paths from its own location, so a moved checkout moves once).

**`lzsa` is NOT installed**, so upstream's `make ram` cannot run here as-is — it
compresses the help files. That does not need fixing: X816 has no Kernal
`DECOMPRESS` ($feed) to decompress them with, the firmware region has ~1 MB
free, and the help text is a few KB. Store it uncompressed and drop
`help.inc`'s lzsa path. (`../X816_Library` has `zx0`/`tscrunch` if compression
ever earns its place.)

## Build and test conventions (house style — follow them)

Every feature gets a `run-*.sh` that boots the real emulator, drives the real
keyboard path with `-autokeys`, and reads the answer back off the screen by
matching 8×8 cells against `runtime/font_cp437.s`. Copy the shape from
`../X816_Calypsi/programs/shell/run-mem.sh`.

**Every test gets a `--negative` control** that breaks the thing under test and
requires the check to fail. A test that has never failed is not evidence.

Assert on the *mechanism*, not just the symptom — `run-mem.sh` checks the
reported ceiling **and** that the allocator's free count moved by exactly 2 MB,
because a release that updated the printout without moving `MEM_ALLOC`'s limit
would pass a ceiling-only check and hand out nothing.

## Conventions that will bite you

**`-autokeys` maps CHARACTERS to keycodes, so an ARROW KEY CANNOT BE SENT.**
Cursor movement, `/M`-style drags and anything arrow-driven is not testable
end-to-end through it; cover the wiring instead and say so in the script. Known
from the kalk work.

**Each `run-*.sh` decodes a FIXED NUMBER OF SCREEN ROWS.** Adding cases can push
the verdict below the window, and then every case prints `ok` while the run
fails. Check the window when you add cases.

**The same scripts also cap the FRAME COUNT, and then read "the last frame".**
`while n < 800` over a warp run that records three thousand frames does not
read the end of the run, it reads the middle — which showed the third command
still being typed and reported a working save as a broken page walk. Count the
frames with bare `seek`/`load` (cheap) and *sample* only the per-cell decode
(expensive). `run-editfile.sh` does both.

**Do not type follow-up commands while the editor holds the screen.**
`-autokeys` types on a ~25 ms emulated clock regardless of what the guest is
doing, so anything queued during a long busy wait — like the smoke tests'
deliberate capture delay — overflows the SMC key FIFO and is dropped. Nothing
reports this; the screen simply stops mid-word. Read results back in a **second
emulator run against the same card image**, which is what `run-editfile.sh`
does, and which additionally proves the write reached the image rather than a
cache.

**Decoding a glyph by "most common colour is the background" inverts dense
glyphs.** `B`, `R` and `N` light more than half their cell, so they decoded as
`?` — "BIG.BIN" read as "?IG.?I?" while every check still looked plausible.
When a bit pattern is not a glyph, try its complement before giving up.

**Do not edit sources while a suite runs** — the background tests compile
whatever is on disk, and a half-finished edit produces failures that look real.

**Watch line endings.** Files in this tree are CRLF (git converts on checkout),
and a `cat >>` that appends LF makes a file MIXED. A scripted multi-line edit
written with a bare `\n` then matches nothing and reports success. Use the Edit
tool, or try both endings and assert on every substitution.

**Two pre-existing things that are NOT your regression:**

* `../X816_DurexForth/run-emu.sh` reports *"no boot banner"*. Verified with the
  pristine tree against the committed `kernel.bin` — it predates the memory-region
  work. Do not chase it.
* **The kernel's bank-$00 claim (`$2000-$2FFF`) is essentially full.** Adding the
  shell's `mem` command overflowed it; `SH_NAME_MAX` went 8→7 and `SH_HELP_MAX`
  26→19 to pay. `x816-kernel.scm`'s rule for an overflow is *shrink the offender,
  do not grow the claim*. `shell.h` records that reclaiming the ~780 bytes of
  inline-array scaffolding needs a second `shell.o` built with
  `-DKERNEL_RESIDENT`, since `shell.c` is compiled once for both images. Expect
  to need that when the editor wants bank $00 — and note the editor's direct page
  MUST be in bank $00, because on the 65816 D is 16-bit and the direct page is
  always bank $00.

## Milestones

1. **Ctrl/Alt in the console layer**, with a hardware keycode confirmation.
   Implemented locally in `../X816_Calypsi` + `contract.py`; hardware
   confirmation still open.
2. **Build plumbing**: fixed load address, ca65 → raw binary, `.incbin` stub,
   firmware assembly, `run-edit.sh` + negative control. The blob needs a 4-byte
   `jsr main_default_entry / rtl` wrapper — a `jsr` cannot reach it across banks.
   Current local state: ca65 raw binary and native-mode JSL-safe wrapper exist
   (`make x816`); the Calypsi resident kernel incbins it at `$F1:2000`; shell
   `edit` enters the real editor and reinitializes the resident console after
   return. `editsmk` is a resident-only smoke command that verifies readable
   first-screen render, both footer rows, exit, and shell prompt restoration.
   Render smoke uses request value `2` and returns directly from the pre-loop
   test path after rendering; it does not exercise full shutdown.
3. **`mem.inc`** (2,082 lines): keep the doubly-linked 256-byte page design,
   widen links to 24 bits, drop the `$A0-$BF` page-in-bank encoding and the
   window arithmetic. Current local state: the head page lives at `$C1:0800`
   above eight reserved scratch pages, display reads use DBR-scoped flat
   accesses, and `run-editmem.sh` proves allocator/link/free behaviour and that
   the scratch pages are never handed out. `run-edittype.sh` proves text
   insertion, render, exit and prompt restoration through the editor's normal
   key handlers; `run-editfile.sh` fills pages from a real file and walks them
   back out. X816 defrag is still disabled rather than allowed to run through
   the old bank-window code, and links are still bank+page rather than flat 24
   bits. Biggest single rewrite.
4. **`file.inc` + `dir.inc` + `cmd_file.inc`** (~2,150) onto `K_FS_*`/`K_DIR_*`.
   `file_cur_device` and the whole IEC notion delete. The editor must **not**
   keep its own working directory — all three callers resolve relative paths
   against `kfs`'s one `cwd`. **`file.inc` is done**: `x816_file.inc` implements
   load and save on `K_FS_OPEN`/`READ`/`WRITE`/`CLOSE`, keeps the line-break
   detection and tab expansion, strips the `@`/`0:` CBM prefixes, reports the
   kernel's `KERR_*` codes as the editor's own error codes, and refuses to save
   a truncated buffer over its source. `dir.inc` and the file/DOS dialogs are
   the remainder.
5. **Screen save/restore** (80×60×2 = 9,600 bytes) so returning to a REPL does
   not blank it.
6. **Shell `edit [path]`**, kernel slot 34 (`K_EDIT`), SuperBasic
   `EDIT [path]`, and durexForth `S" path" edit` are in place, and now **open
   the file** rather than only seeding its name. `run-editfile.sh` proves the
   whole round trip from the console; `../X816_SuperBasic/run-edit-smoke.sh`
   proves a language caller's load.

## Remaining X816 migration work

Current verified entry status: shell `edit [path]`, SuperBasic `EDIT [path]`
and durexForth `S" path" edit` all launch the resident editor **and open the
named file**, and Ctrl+S saves it back. Single-page and multi-page loads both
work; what is left is below.

1. **Port directory browsing and the file dialogs.** `dir.inc` and the
   remaining `cmd_file.inc` dialogs are still IEC-shaped: the file-open dialog
   cannot list a directory, and `file_disk_cmd` (the DOS-command prompt) says
   "not available on x816" rather than reaching `K_FS_CHDIR`/`K_FS_MKDIR`/
   `K_FS_RMDIR`/`K_FS_DELETE`/`K_FS_RENAME`. `dir_entry`'s home is reserved at
   `$C1:0200`. `file_cur_device` is now zeroed and unused on X816 but the
   *variable* and the "set device number" dialog are still there to delete.
   **Do not give the editor a working directory of its own** — all three
   callers resolve relative paths against `kfs`'s one cwd, which is what
   `run-editfile.sh` reads the saved file back through.
2. **Finish the flat page model.** Links are still a bank byte plus a page
   byte, which works because the pool is bank-aligned, but the format has no
   room for a page at offset 0 of a bank: byte 0 is the null-link sentinel, so
   page $00 of all 31 banks is reserved. Widening links to flat 24-bit values
   reclaims those and is the prerequisite for **defrag**, which is deliberately
   gated off on X816 and must be ported before it is re-enabled.
3. **Add persistent buffer metadata.** Decide and implement how an existing
   resident buffer is detected after returning to a caller: magic, page count,
   dirty flag, current filename, and whether `edit` with no argument resumes
   while `edit <file>` opens fresh. Note `x816_file_read` calls `mem_init`,
   which discards the whole buffer — resume has to happen before that.
4. **Move the editor's VARS out of bank $00.** The buffers moved; the ~970-byte
   VARS segment did not. It sits at `$0400-$07CE`, inside the region
   `x816-lib.scm` gives a loadable program for its stack. It survives today
   because a caller's stack would have to be ~6 KB deep to reach it, and
   because `ram_backup`/`ram_restore` now save and restore it correctly across
   the call — but it is a bank-$00 claim the editor has not actually been
   granted.
5. **Save and restore caller screens.** Returning currently reinitializes the
   console and clears/repaints rather than restoring the previous REPL view.
   Saving an 80x60x2 text screen needs 9,600 bytes plus cursor/console state.
6. **Confirm Ctrl/Alt on hardware.** The ABI and emulator path now report
   `KEY_CTRL`/`KEY_ALT` composites, but the raw hardware key positions still
   need a `KEYSCAN`-style confirmation before relying on Ctrl-heavy editor UI.
7. **Test the truncation guard.** The `file_truncated` flag, the remembered
   source path and the refusal are implemented (`x816_file.inc`), but nothing
   exercises them: reaching the limit needs a file larger than the 2 MB buffer.
   The cheap way is a build-time knob that shrinks the pool.
8. **Clean generated/debug artifacts before committing.** The shell/program
   build updates many `.bin`/`.raw` outputs, and local debug GIF/probe files may
   exist from smoke-test investigation. Keep the smoke scripts and real build
   products; drop temporary captures/probes.

## What ports easily, what deletes

**Deletes — ~1,700 lines.** `bridge.inc`, `bridge_macro.inc`, `jsrfar.inc` (365
lines and 115 `BNK_SEL`/`ROM_SEL` references: no ROM banks, no window),
`printer.inc` (779), `mouse.inc` (433 — no `MOUSE` kernel call, and
`input/mouse` is library by KERNEL.md §2.3).

**`ram.inc` does NOT delete** — that call was wrong. The ABI preserving D and
DBR says nothing about the *bytes* the editor writes in bank $00, and the editor
puts its ZP at `$0022-$003F` and its VARS at `$0400-$07CE`, both inside memory a
loadable caller owns. So backup/restore still has a job; what changed is where
it puts the backup (the scratch pages, not `$00:B000`). It deletes when the
editor's state leaves bank $00 — remaining-work item 4.

**Ports nearly unchanged.** `screen.inc` has 95 VERA references and only 8
Kernal ones, and the X816 I/O page is byte-for-byte the X16's for `$9F00-$9F7F`;
both consoles are 80×60, 1bpp tile mode, per-cell attributes. `cursor.inc`,
`selection.inc`, `clipboard.inc`, `util.inc`, `prompt.inc`, `cmd.inc`,
`compile.inc` touch no Kernal at all — 12 of 28 files have zero Kernal/bridge
calls. `charset.inc` mostly deletes: CP437 *is* ASCII for `$20-$7E`.

**Opcode-clean.** No `rmb`/`smb`/`bbr`/`bbs`/`trb`/`tsb`. Everything
65C02-specific it uses — `stz` ×277, `bra` ×152, `phx`/`plx`/`phy`/`ply` ×73 —
exists on the 65816 and runs in native mode with M=1/X=1.

**IRQ.** `irq.inc` chains the X16's CINV; here it is `IRQ_SET` on `KIRQ_VSYNC` —
handler entered by `jsl` with M=0/X=0, D=`$0000`, DBR=`$00`, ending `rtl`, and
it **must not `cli`**. `IRQ_SET` returns the previous handler, so save/restore
across the editor call is clean. `runtime/ccursor.s` is the working example, and
note the kernel's cursor blink already owns VSYNC.

**Register discipline for the callers.** durexForth runs M=0/X=1 with the Forth
data stack pointer in X and its return stack on the hardware stack
(`$0100-$1FFF`, ~8 KB — comfortable). The kernel ABI wants M=0/X=0, so the
crossing needs `rep`/`sep` exactly as `../X816_DurexForth/asm/x816.asm`'s
existing shims do, and X and Y must come back untouched. `KENTER`/`KLEAVE` give
D and DBR for free; the flags are the thunk's job.

## One bug upstream needn't care about — fixed, not yet exercised

`file.inc:711-717` — a file larger than the buffer stops at `mem_full` and
reports it, leaving a **partial** buffer, and Ctrl+S then writes that over the
original. Survivable for a source file you were editing anyway; silent data loss
for an arbitrary file off the card, which is exactly what "available from the
console for any file" makes routine.

`x816_file.inc` closes it: a truncated load sets `file_truncated` **and
remembers the path it was reading**, and a save whose staged path equals that
one is refused with "partial buffer: save under another name". A successful save
clears the flag, because the buffer then has a file of its own. Comparing the
*staged* paths rather than the display filename is deliberate — that is the
string that actually reached `K_FS_OPEN`, after `@` and `0:` stripping.

**Untested**, and it stays on the remaining-work list until it is not: tripping
it needs a file bigger than the 2 MB buffer.

**Line endings, by contrast, are already right** — `file.inc:570-595` detects
LF/CR/CRLF on load and `212-227` writes the same encoding back. Checked, not
assumed, and it matters: SuperBasic's `LOAD` bug was a line-ending assumption,
and an editor that normalised silently would reintroduce that class of bug from
the other side. **Do not "tidy" this into always writing one convention.**
