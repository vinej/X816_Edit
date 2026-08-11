# X816_Edit

X16 Edit ported to the **X816** — a flat 16 MB, native-mode-only 65C816 MiSTer
core — as the machine's **resident text editor**, living in the kernel firmware
and callable from durexForth, SuperBasic and the shell, returning to whichever
called it.

| | |
|---|---|
| Upstream | <https://github.com/stefan-b-jakobsson/x16-edit> (Stefan Jakobsson) |
| Core | <https://github.com/vinej/X816_Core> |
| Kernel / runtime | `../X816_Calypsi` |
| Callers | `../X816_DurexForth`, `../X816_SuperBasic` |

## This is a fork, not a copy, and that is the point

`git remote -v` shows `upstream` pointing at Stefan Jakobsson's repository, with
all 341 commits of its history present. Upstream fixes arrive by
`git fetch upstream && git merge upstream/master` — not by hand-copying files.

That is also why the port keeps the **ca65** sources rather than converting
15,000 lines to the Calypsi assembler: a converted tree cannot be merged into.
The X816 build assembles this tree with ca65 at a fixed firmware address and
`.incbin`s the result into a small Calypsi stub, which `as65816` supports
(verified in the 5.18 assembler manual). One blob, one entry thunk, upstream
still reachable.

Work happens on the **`x816`** branch. `master` tracks upstream so a merge has
somewhere clean to land.

## Licence

X16 Edit is **BSD-2-Clause**, © Stefan Jakobsson 2022-2024 — see `license`. That
notice must be reproduced in source *and* in the documentation of any binary
distribution, and the editor ships inside `boot2.rom`, so the obligation is live
rather than theoretical. Every file keeps its header; new X816-only files carry
their own and say so.

## What the machine already provides

The memory contract is **done and green** (`X816_core/tools/contract.py`, one
source, generated into every consumer):

| Constant | Value | What it is |
|---|---|---|
| `X816_KDATA_BASE` | `$C0:0000` | the kernel writable-data region, 2 MB |
| `X816_KDATA_FAR` | `$C0:0000` | bank `$C0` — the kernel's own `far` data |
| `X816_EDIT_BASE` | `$C1:0000` | **the editor's page pool** |
| `X816_EDIT_LAST` | `$DF:FFFF` | 2,031,616 bytes = 7,936 pages of 256 |
| `KMEM_REGION_EDIT` | 0 | `K_MEM_RELEASE` region id |

Reserved at boot; `K_MEM_RELEASE` hands it to the kernel heap for the rest of a
session, one way, and the editor must then refuse to open. `K_MEM_TOP` reports
the live ceiling and **every allocator is required to ask** rather than compile
it in. Conformance: `X816_Calypsi/programs/shell/run-mem.sh`, with a negative
control. See `X816_core/doc/MEMORY_MAP.md` §1.1.

**The region is not a linker section in any map, and must never become one.**
`K_EXIT` restarts the kernel through `cstartup`, and anything the linker places
is something `cstartup` initialises. Same rule as the carry-over block at
`$20:A0-$20:FF` (`x816-kernel.scm`), for the same reason — and here it buys
something: the buffer **survives between editor invocations**, so edit → run →
edit keeps your text. It needs a magic word and a page count to tell a live
buffer from power-up SDRAM noise.

## What the port has to do

Assessed against the sources, not guessed:

**Deletes outright — ~1,700 lines.** `bridge.inc`, `bridge_macro.inc`,
`jsrfar.inc` (365 lines and 115 `BNK_SEL`/`ROM_SEL` references: there are no ROM
banks and no window), `printer.inc` (779, no printer), `mouse.inc` (433, no
`MOUSE` kernel call — `input/mouse` is library by KERNEL.md §2.3), `ram.inc`
(104: the kernel ABI preserves D and DBR and the editor's state is kernel-side,
so there is nothing to back up).

**Ports nearly unchanged.** `screen.inc` has 95 VERA references and only 8
Kernal ones, and the X816 I/O page is byte-for-byte the X16's for
`$9F00-$9F7F` — both consoles are 80×60, 1bpp tile mode, per-cell attributes.
`cursor.inc`, `selection.inc`, `clipboard.inc`, `util.inc`, `prompt.inc`,
`cmd.inc`, `compile.inc` touch no Kernal at all. 12 of 28 files have zero
Kernal/bridge calls.

**Rewrites.** `mem.inc` (2,082 lines): keep the doubly-linked 256-byte page
design, widen the links to 24 bits, drop the `$A0-$BF` page-in-bank encoding.
`file.inc` + `dir.inc` + `cmd_file.inc` (~2,150) onto `K_FS_*` / `K_DIR_*`;
`file_cur_device` and the whole IEC notion delete, and the editor must **not**
keep its own working directory — all three callers resolve relative paths
against `kfs`'s one `cwd`. `charset.inc` mostly deletes: CP437 *is* ASCII for
`$20-$7E`.

**Opcode-clean.** Zero `rmb`/`smb`/`bbr`/`bbs`/`trb`/`tsb`. Everything
65C02-specific it uses — `stz` ×277, `bra` ×152, `phx`/`plx`/`phy`/`ply` ×73 —
exists on the 65816 and runs in native mode with M=1/X=1.

## The one thing that does not exist yet

**Ctrl and Alt do not reach a program.** `X816_Calypsi/runtime/console.c`
tracks `shift_l`/`shift_r` only; Ctrl and Alt arrive as `KEY_LCTRL`/`KEY_LALT`
press-and-release events with no state and no composite. X16 Edit's entire UI is
Ctrl+letter (`keyboard_ctrl_keyval`, `keyboard_ctrl_jmptbl`), so this blocks
everything downstream. `console.h` says so itself: *"Making Ctrl-C a character
is a design step, not a mapping one."*

Two options, and it is an ABI addition either way:

* report Ctrl+A as `$01`-`$1A` — cheap, but those are real CP437 glyphs
* a third 16-bit class (`0x0200|key` ctrl, `0x0400|key` alt) — unambiguous, and
  what the 16-bit key encoding was built for

Second one recommended. Confirm the keycodes on hardware with a `KEYSCAN`-style
probe rather than from the RTL: console.h records that the last time these were
assumed they were wrong in both places at once *and agreed with each other*.

## The call

`K_EDIT`, slot 34 (`$00:FE88`) — the "programs" group beside `K_EXEC` (32) and
`K_EXIT` (33). `C:X` = 24-bit path or 0 for an empty buffer, `Y` = option flags.
Carry clear with `C` = 0 saved / 1 discarded; carry set with a `KERR_`.

Not yet added to `contract.py` — the argument block should be settled first, and
the slot number is ABI the moment it ships.

**The shell needs no slot.** `kernelmain.c` is `con_init(); kern_install();
kirq_install(); ccur_on(); sh_run();` — the shell **is** the resident kernel, so
`cmd_edit` calls the editor directly, in-image. The slot is for the loadable
programs. That makes the console path the cheapest of the three and the right
first milestone: testable before either language shim exists.

## Milestones

1. Ctrl/Alt in the console layer, with a hardware keycode confirmation.
2. Build plumbing: fixed address, `.incbin` stub, firmware assembly, a
   `run-edit.sh` in the house style with a negative control.
3. `mem.inc` onto flat 24-bit pointers.
4. `file.inc`/`dir.inc`/`cmd_file.inc` onto `K_FS_*`.
5. Screen save/restore (80×60×2 = 9,600 bytes) so returning to a REPL does not
   blank it.
6. Shell `edit`, then slot 34, then the durexForth and SuperBasic shims.

## Two bugs to fix that upstream does not have to care about

**A truncated load can overwrite the original.** `file.inc:711-717`: a file
larger than the buffer stops at `mem_full` and reports it, leaving a **partial**
buffer — and Ctrl+S then writes that over the source. Survivable for a source
file you were editing anyway; data loss for an arbitrary file off the card,
which is what "the editor is available from the console for any file" makes
routine. Needs a `truncated` flag that refuses save-over-source.

**Nothing else — and one thing is already right.** `file.inc:570-595` detects
LF / CR / CRLF on load and `file.inc:212-227` writes the same encoding back, so
line endings round-trip. That matters more than it sounds: SuperBasic's `LOAD`
bug was exactly a line-ending assumption, and an editor that normalised silently
would have reintroduced the same class of bug from the other side.

## Setting up your own remote

The fork has `upstream` only. Add yours and push the branch:

```sh
git remote add origin <your-url>
git push -u origin x816
```

`../X16Edit_ref` stays as the pinned, untouched reference — the test oracle, the
same role the Prog8 port plays for kalk.
