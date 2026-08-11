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

## State (2026-08-11): kernel side done, editor side not started

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

**Nothing started in this repo** beyond `README-X816.md` and this file.

## Do this first — it is not in this repo

**Ctrl and Alt never reach a program**, and X16 Edit's entire UI is Ctrl+letter
(`keyboard_ctrl_keyval`, `keyboard_ctrl_jmptbl`). `../X816_Calypsi/runtime/console.c`
tracks `shift_l`/`shift_r` only; Ctrl and Alt arrive as `KEY_LCTRL`/`KEY_LALT`
press-and-release events with no state and no composite. `console.h` says so
itself: *"Making Ctrl-C a character is a design step, not a mapping one."*

Recommended: a third 16-bit class — `0x0200|key` for ctrl, `0x0400|key` for alt
— rather than `$01`-`$1A`, which are real CP437 glyphs `con_putraw` can draw.
It is an ABI addition either way, so it goes in `contract.py` and `console.h`
together.

**Confirm the keycodes on hardware** with a `KEYSCAN`-style probe. `console.h`
records that the last time these were assumed rather than measured they were
wrong in *both* places at once and agreed with each other.

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
and `K_EXIT` (33) — **not added yet, on purpose.** Settle the argument block
first: the slot is ABI the moment it ships. Intended shape: `C:X` = 24-bit path
or 0 for an empty buffer, `Y` = option flags; carry clear with `C` = 0 saved /
1 discarded, carry set with a `KERR_`.

**The shell needs no slot.** `../X816_Calypsi/programs/shell/kernelmain.c` is
`con_init(); kern_install(); kirq_install(); ccur_on(); sh_run();` — the shell
**is** the resident kernel, so `cmd_edit` calls the editor directly, in-image.
That makes the console path the cheapest of the three callers and the right
first milestone: testable before either language shim exists. Add one row to
`sh_commands[]` in `runtime/shell.c` (`{ "edit", "edit a file", 0, 1, cmd_edit }`)
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
   (Lands in `../X816_Calypsi` + `contract.py`, not here.)
2. **Build plumbing**: fixed load address, ca65 → raw binary, `.incbin` stub,
   firmware assembly, `run-edit.sh` + negative control. The blob needs a 4-byte
   `jsr main_default_entry / rtl` wrapper — a `jsr` cannot reach it across banks.
3. **`mem.inc`** (2,082 lines): keep the doubly-linked 256-byte page design,
   widen links to 24 bits, drop the `$A0-$BF` page-in-bank encoding and the
   window arithmetic. Biggest single rewrite.
4. **`file.inc` + `dir.inc` + `cmd_file.inc`** (~2,150) onto `K_FS_*`/`K_DIR_*`.
   `file_cur_device` and the whole IEC notion delete. The editor must **not**
   keep its own working directory — all three callers resolve relative paths
   against `kfs`'s one `cwd`.
5. **Screen save/restore** (80×60×2 = 9,600 bytes) so returning to a REPL does
   not blank it.
6. **Shell `edit`**, then slot 34, then the durexForth and SuperBasic shims.

## What ports easily, what deletes

**Deletes — ~1,700 lines.** `bridge.inc`, `bridge_macro.inc`, `jsrfar.inc` (365
lines and 115 `BNK_SEL`/`ROM_SEL` references: no ROM banks, no window),
`printer.inc` (779), `mouse.inc` (433 — no `MOUSE` kernel call, and
`input/mouse` is library by KERNEL.md §2.3), `ram.inc` (104 — the ABI preserves
D and DBR and the editor's state is kernel-side).

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

## One bug to fix that upstream needn't care about

`file.inc:711-717` — a file larger than the buffer stops at `mem_full` and
reports it, leaving a **partial** buffer, and Ctrl+S then writes that over the
original. Survivable for a source file you were editing anyway; silent data loss
for an arbitrary file off the card, which is exactly what "available from the
console for any file" makes routine. Needs a `truncated` flag that refuses
save-over-source and forces a save-as.

**Line endings, by contrast, are already right** — `file.inc:570-595` detects
LF/CR/CRLF on load and `212-227` writes the same encoding back. Checked, not
assumed, and it matters: SuperBasic's `LOAD` bug was a line-ending assumption,
and an editor that normalised silently would reintroduce that class of bug from
the other side. **Do not "tidy" this into always writing one convention.**
