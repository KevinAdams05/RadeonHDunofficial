> [!NOTE]
> An LLM was used to aid in development of this code.

# RadeonHD Coding Style Guide

RadeonHD is an **unofficial fork** of Haiku's in-tree `radeon_hd`
graphics driver for ATI/AMD Radeon GPUs (R420 through Vega / Navi),
distributed as a standalone `.hpkg`. The codebase is C++ in Haiku's
particular dialect — mostly C-style structs and free functions on the
kernel side, with a userspace accelerant alongside. Our style is the
**Haiku Project Coding Guidelines** verbatim, with the project-specific
notes called out in §1.

**Authoritative base:** <https://www.haiku-os.org/development/coding-guidelines>

When this document and the Haiku guidelines disagree, **this document
wins** for RadeonHD code. When this document is silent, defer to Haiku.

This is a fork of existing Haiku code, so the overriding rule is: **look
at how the surrounding code already does it.** Consistency with the
immediate context outranks consistency with this guide. Never make a
file "stick out" from its neighbours just to match a rule here — and in
particular, do not rewrite working upstream code purely to bring it into
line with this guide. Match the file you are editing.

---

## 1. Project-specific notes

These are the **only** intentional differences from upstream Haiku
style. Everything else in this document is a restatement of the Haiku
rules for convenience.

### 1.1 Driver-only scope

Every change in this fork stays inside the `radeon_hd` driver tree:

- `src/add-ons/kernel/drivers/graphics/radeon_hd/`
- `src/add-ons/accelerants/radeon_hd/`
- `headers/private/graphics/radeon_hd/`

Do **not** touch `app_server`, the kernel proper, or shared graphics
headers (`headers/os/...`, `accelerant.h` in the OS tree, etc.). If a
fix appears to require a change outside the driver tree, it is out of
scope for this project — raise it upstream instead.

### 1.2 The repo is an overlay, not a standalone tree

This repository carries only the handful of files the fork actually
modifies. It does **not** build in-place. `scripts/build.sh` copies the
repo's files over a configured Haiku source tree and runs `jam` there
(see `docs/building-and-packaging.md`). Two consequences:

- When you need to inspect an unmodified driver file (`driver.h`,
  `radeon_hd_private.h`, `device.cpp`, `radeon_hd.h`, the register
  headers, etc.), read it from the Haiku source tree — the fork only
  carries it if it was changed.
- A new source file must be added to the relevant `Jamfile` *and* the
  overlay copy step in `scripts/build.sh`, or jam will never see it.

### 1.3 Line length — 80-character cap

- **Hard cap: 80 columns**, computed with a tab width of 4. This is
  plain upstream Haiku; the `radeon_hd` tree already lives within it.
- The only routine exceptions are unbreakable string literals, URLs,
  and long register-define names — and even those should be wrapped
  where the grammar allows it (`Write32(...)` calls wrap their
  arguments rather than running long).

### 1.4 Linux source is a reference, never a copy source

You may read Linux's `radeon` / `amdgpu` drivers (and `xf86-video-ati`,
the X11 predecessor) to understand register semantics, hardware quirks,
PLL math, and init sequences. You may **not** copy code from them.

Write original Haiku-style code that achieves the same hardware result.
This is the standard policy across Kevin's Haiku work, and it is what
keeps every file in this fork cleanly MIT-licensed (see §16). When a
register sequence was informed by the Linux driver, a comment pointing a
future reader at the upstream file is welcome — but the implementation
must be your own.

---

## 2. Indentation and whitespace

- **Tabs** for indenting blocks. Editor tab width is **4** for purposes
  of computing line length and alignment.
- Wrapped lines get **at least one extra tab**, plus one more tab per
  expression nesting level.
- Namespace contents are **not indented** — they sit flush at column 0.
  (The `radeon_hd` driver is largely namespace-free C-style code, so
  this rarely comes up.)
- **Spaces** on both sides of binary operators (`a + b`, `x == y`).
- **No space** between a C-style cast operator and its operand: `(int)x`.
- **Always a space** after a comma.
- Every file ends with a newline.
- No trailing whitespace on any line.

## 3. Naming

`radeon_hd` is older Haiku driver code and uses a **mix** of styles:
C-style `snake_case` structs and free functions on the kernel side
(`radeon_info`, `radeon_hd_init`, `radeon_shared_info`), and a few
`UpperCamelCase` classes/helpers (`RingQueue`, `AreaKeeper`). When
adding code, match the surrounding file. For genuinely new C++ classes,
follow the Haiku conventions below.

| Kind | Convention | Example |
|---|---|---|
| Classes, C++ types, namespaces, methods | `UpperCamelCase` | `RingQueue`, `AreaKeeper` |
| C-style structs / typedefs | `lower_snake_case` | `radeon_info`, `display_info`, `gpu_state` |
| Free functions (kernel-style) | `lower_snake_case` | `radeon_hd_init`, `radeon_hd_pci_bar_mmio` |
| Local variables | `lowerCamelCase` | `pixelClock`, `connectorIndex` |
| Struct/member fields | bare `lowerCamelCase` (no `f` prefix in the C-style structs) | `chipsetID`, `frame_buffer`, `dceMajor` |
| C++ class members | `f` prefix + `UpperCamelCase` | `fRegisters`, `fFramebuffer` |
| Constants | `k` prefix + `UpperCamelCase` | `kSupportedDevices` |
| Globals | `g` prefix | `gInfo`, `gDeviceInfo`, `gPCI`, `gAtomContext` |
| Statics (file/function scope) | `s` prefix | `sChipTable` |
| Private C++ methods | `_` prefix | `_ProgramPll`, `_ReadDpcd` |

Rules:

- No underscores in **C++ type or method** names (other than the `_`
  prefix on private methods, and the lowercase-underscore form required
  by Haiku driver entry points — see §3.1). Note this does *not* apply
  to the existing C-style `radeon_hd_*` functions and `snake_case`
  structs, which are correct as they are.
- **Descriptive names beat short ones.** Prefer spelled-out names — the
  few extra characters pay for themselves the first time someone
  unfamiliar reads the code.
  - Variables: `connector` not `conn`, `framebuffer` not `fb` (the
    existing `frame_buffer` field is fine), `width` not `w`,
    `index` not `idx`.
- Exception: well-known graphics-driver terms of art are fine and
  spelling them out would only hurt — `id`, `dpi`, `rgb`, `min`/`max`,
  `i`/`j`/`k` for tight loop indices, `pll`, `bpp`, `crtc`, `edid`,
  `i2c`, `ddc`, `aux`, `mmio`, `bar`, `dpcd`, `dvo`, `tmds`, `dac`,
  `vga`, `lcd`, `dpms`, `dce`, `dcn`, `gart`, `vram`, `atombios`.
  When in doubt, spell it out.
- No articles in names — avoid `aMessage`, `theView`, `MyDraw`.
- All identifiers, comments, and strings in **US English**
  ("color", not "colour"). The shared `color_data` LUT field follows
  this even though a comment in the header spells it "colour".

### 3.1 Haiku driver entry-point naming exception

The Haiku kernel and accelerant subsystems specify lowercase-with-
underscores names for driver entry points (`init_driver`,
`uninit_driver`, `init_hardware`, `publish_devices`, etc.) and for the
accelerant hook table (`get_accelerant_hook`, `set_display_mode`, ...).
Use the exact names the OS expects — do not "Haiku-ify" them to
`InitDriver` style. `radeon_hd` already does this; keep it.

## 4. Braces and blocks

- **Class / struct** opening brace: same line as the declaration.
- **Function** opening brace: on its own line.
- **`if` / `else` / `for` / `while` / `switch`** opening brace: same
  line as the keyword and condition.
- `else` and `else if` go on a new line, after the closing brace of
  the previous block.
- **Single-statement** `if`/`else`/`for`/`while`: omit the braces,
  put the statement on a new indented line.
- **Multi-statement** blocks: always braces.
- After an early `return` (or `break`/`continue`) inside an `if`, do
  **not** write an `else` — the `else` is dead syntax.

```cpp
status_t
radeon_hd_init(radeon_info& info)
{
	TRACE("card(%" B_PRId32 "): %s: called\n", info.id, __func__);

	uint32 pciConfig = get_pci_config(info.pci, PCI_command, 2);
	pciConfig |= PCI_command_io | PCI_command_memory | PCI_command_master;
	set_pci_config(info.pci, PCI_command, 2, pciConfig);

	if (info.shared_area < B_OK) {
		ERROR("%s: card (%" B_PRId32 "): couldn't map shared area!\n",
			__func__, info.id);
		return info.shared_area;
	}

	return B_OK;
}
```

## 5. Functions

- Return type on its own line, **above** the function name.
- Opening brace on its own line, flush left.
- **Two blank lines** between function definitions.
- Long argument lists: wrap and indent the continuation by **one tab**.

```cpp
static uint32
radeon_hd_pci_bar_mmio(uint16 chipsetID)
{
	if (chipsetID < RADEON_BONAIRE)
		return 2;
	else
		return 5;
}
```

## 6. Constructor initializer lists

For the few C++ classes in the tree (`RingQueue`, etc.):

- Colon on its **own line**, indented one tab.
- Each initializer on its own line, indented one tab.
- Prefer initializer lists over assigning in the body — only put work
  in the body that genuinely cannot be expressed as initialization.

```cpp
RingQueue::RingQueue(size_t bytes, uint32 queue)
	:
	fSize(bytes),
	fWriteCount(0),
	fReadCount(0),
	fQueueType(queue)
{
}
```

## 7. Blank lines

- **Two blank lines** between functions.
- **Two blank lines** between the include block and any subsequent
  define block, and between defines and the first variable/function.
- **One blank line** between cases in a `switch`.
- **One blank line** after the opening `#define` of a header guard.
- **Two blank lines** before the closing `#endif` of a header guard.
- No blank line between the license/copyright block and the header
  guard.

## 8. Control flow specifics

### 8.1 If / else

- Always use explicit boolean tests, never rely on implicit
  truthiness.
  - Pointers: `if (pointer != NULL)`, not `if (pointer)`.
  - Integers: `if (count != 0)`, not `if (count)`.
- Bitmasks always go in parentheses with an explicit comparison:
  `if ((flags & CHIP_IGP) != 0)`.
- No assignment inside an `if` (or `while`) condition. Split it:
  ```cpp
  status_t status = gpu_probe(info);
  if (status != B_OK)
      return status;
  ```
- Variable goes on the **left** of comparisons: `if (status == B_OK)`,
  never `if (B_OK == status)`. No Yoda conditions.
- Do not wrap an entire `if` condition in redundant outer parentheses,
  and do not parenthesise each clause:
  `if (a == 3 && b != 4)`, not `if ((a == 3) && (b != 4))`.

### 8.2 Long conditions

When wrapping a long boolean expression, put the **logical operator at
the start** of the next line, not at the end of the previous one:

```cpp
if (pciBarMmio < 5
	&& (info.pci->u.h0.base_register_flags[pciBarMmio] & PCI_address_type)
		== PCI_address_type_64) {
	// ...
}
```

### 8.3 Switch

- `case` labels are indented one tab inside the `switch`.
- The body of each case is indented one further tab.
- One blank line between cases.
- Wrap a case body in `{ }` whenever it declares its own variables.
- Always have a `default:` (even if it just `break;`s).

```cpp
switch (info.chipsetID) {
	case RADEON_BONAIRE:
	{
		uint32 mmioBar = radeon_hd_pci_bar_mmio(info.chipsetID);
		// ...
		break;
	}

	case RADEON_CEDAR:
		// ...
		break;

	default:
		ERROR("%s: unknown chipset 0x%x\n", __func__, info.chipsetID);
		return B_NOT_SUPPORTED;
}
```

### 8.4 Loops

- Prefer `for` over `while`-with-assignment. If you find yourself
  writing `while ((x = next()) != NULL)`, refactor to a `for` or
  pull the assignment out.
- Range-based `for` is allowed; use it when the index is not needed.

### 8.5 No `goto`

Use RAII (e.g. `AreaKeeper` / `AGPGartMapper`, which the driver already
uses for scoped area mapping) or early return. The existing `radeon_hd`
code is already goto-free; keep it that way.

## 9. Types

### 9.1 Prefer Haiku types over raw C types

When working in Haiku-native code (the entire codebase):

- `int32` / `uint32` instead of `int` / `unsigned`.
- `int64` / `uint64` for explicit 64-bit (the driver uses `uint64`
  throughout the BAR-assembly code).
- `off_t` for file offsets.
- `size_t` / `ssize_t` for sizes.
- `phys_addr_t` for physical addresses (the MMIO/FB base addresses).
- `addr_t` for pointer-sized integers — `radeon_info::registers` is an
  `addr_t`, and register access casts through it.
- `status_t` for error returns, with `B_OK` on success. Functions that
  can fail return `status_t`.

These come from `<SupportDefs.h>`.

When formatting Haiku integer types in `TRACE`/`ERROR`, use the
`B_PRId32` / `B_PRIx32` / `B_PRIX32` macros — `radeon_hd` does this
consistently (`TRACE("card(%" B_PRId32 ")...", info.id)`).

### 9.2 Strings

- `char[N]` buffers and `sprintf` / `snprintf` for kernel-driver code
  (no `BString` in kernel space). Device names are built this way:
  `sprintf(name, "graphics/radeon_hd_%02x%02x%02x", ...)`.
- `BString` is available in the accelerant; prefer it over
  `char*` / `malloc`/`strdup`/`free` there. Use `BString::operator<<`
  and `BString::SetToFormat` over `sprintf` in `BString` code.

### 9.3 Casts

- Use C++ casts: `static_cast`, `dynamic_cast`, `const_cast`,
  `reinterpret_cast`.
- C-style casts are acceptable for primitive numeric conversions and
  must have **no whitespace** after the cast operator: `(int)x`, not
  `(int) x`. The existing register-access helpers use them
  (`*(volatile uint32*)(gInfo->regs + offset)`).
- The driver assembles 64-bit BAR addresses by OR-ing the high dword:
  `addr |= (uint64)info.pci->u.h0.base_registers[bar + 1] << 32;`.
  Match that pattern rather than inventing a new one.

## 10. Pointers and null

- `NULL`, not `0` or `nullptr`. (Haiku tradition.)
- Initialize pointers with traditional assignment, not constructor
  syntax: `radeon_info* info = NULL;`, not `radeon_info* info(NULL);`.
- **Pointer asterisk binds to the type**: `radeon_info* fDevice;`, not
  `radeon_info *fDevice;`. (Some existing register helpers in
  `accelerant.h` write `volatile uint32 *` — when editing those, match
  the file; for new code, bind left.)
- Do **not** check for `NULL` before `delete` or `free` — both accept
  `NULL` and the check is noise:
  ```cpp
  delete fEdid;   // not: if (fEdid != NULL) delete fEdid;
  ```

## 11. Boolean conventions

- Use `true` / `false` from C++, never `TRUE` / `FALSE` macros.
- Functions that report success/failure return `status_t` (`B_OK` on
  success), not `bool` — `bool` should mean a genuine yes/no flag
  (`has_edid`, `is_clone`), not "did it work".

## 12. Returns and parentheses

- Do not parenthesise the return expression: `return result;`, not
  `return (result);`.
- Prefer early returns. Keep happy-path code at one indent level.

## 13. Comments

- Prefer `//` over `/* */`.
- Explain **why**, not what. `i++; // increment i` is noise.
- For genuinely tricky code, describe the constraint or pitfall — e.g.
  `// Sea Islands (Bonaire+) moved MMIO from BAR 2 to BAR 5.`
- A comment pointing at the Linux driver for a register's *semantics*
  is fine (`// see Linux radeon ni_reg.h for the HDMI AFMT layout`),
  but the code itself must be original (§1.4).
- No author initials in comments. Git already knows.
- No `// TODO: kevin` style markers. Plain `// TODO:` is fine.
- No `#if 0`'d dead code. Delete it; git has the history.
- **Doxygen** (`/*! ... */`) for documenting public/header API surface
  (see `hdmi.h` for the in-tree style). Used for code comprehension,
  not end-user documentation — that lives in `docs/`.

## 14. Includes

### 14.1 Ordering

Within a source file (`.cpp`), in this order, with **one blank line**
between groups:

1. The corresponding header (`#include "driver.h"` from `driver.cpp`),
   or the primary local header for the file.
2. POSIX / standard C headers (`<stdio.h>`, `<stdlib.h>`, ...).
3. C++ standard headers — only when needed.
4. Haiku API headers (`<KernelExport.h>`, `<PCI.h>`, `<AGP.h>`, ...).
5. Local project headers (`"radeon_hd.h"`, `"device.h"`, register
   headers like `"evergreen_reg.h"`).

Within each group, **alphabetize** include lines. (Existing files are
not perfectly sorted; match the file you are editing and don't churn
unrelated includes.)

### 14.2 Style

- `<angle>` for system / framework headers.
- `"quoted"` for local project headers (including the register headers
  pulled in via `radeon_hd.h`).
- Use **C-style header names**: `<string.h>`, `<stdlib.h>` — not
  `<cstring>`, `<cstdlib>`. (Haiku tradition; the driver uses these.)
- Avoid path components the build system makes unnecessary:
  `<KernelExport.h>`, not a fully-qualified path.

## 15. Header files

### 15.1 Layout

```cpp
/*
 * Copyright 2006-2011, Haiku, Inc. All Rights Reserved.
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Alexander von Gluck, kallisti5@unixzen.com
 *		Kevin Adams, kevinadams05@gmail.com
 */
#ifndef RADEON_HD_HDMI_H
#define RADEON_HD_HDMI_H


#include <SupportDefs.h>


/*! Program the HDMI AVI infoframe + KEEPOUT for one CRTC. */
void hdmi_avi_infoframe_program(uint8 crtcID);


#endif	// RADEON_HD_HDMI_H
```

### 15.2 Header-guard rules

- Form follows the existing files: `RADEON_HD_<NAME>_H` for the
  accelerant headers (`RADEON_HD_ACCELERANT_H`, `RADEON_HD_HDMI_H`),
  and the shorter upstream forms in the kernel headers (`DRIVER_H`,
  `RADEON_HD_H`). Match the convention of the directory you are in.
- The guard immediately follows the copyright block — **no blank
  line between them**.
- **One blank line** after the `#define`.
- **Two blank lines** before the closing `#endif`.
- The closing `#endif` carries a `// <GUARD>` (or `/* <GUARD> */`)
  comment.

### 15.3 Member declaration alignment

- Members and methods inside a class/struct are aligned in columns —
  the C-style structs (`radeon_shared_info`, `accelerant_info`) align
  the field type and name, and this matches Haiku style. Follow the
  existing alignment when adding a field.

## 16. Copyright headers

RadeonHD is **MIT-licensed**. The `radeon_hd` driver is MIT-licensed
Haiku code, and every file in this fork stays MIT. This is the standard
Haiku two-line form. **Do not introduce GPL.** Because we never copy
from GPL-licensed Linux drivers (§1.4), there is nothing forcing a
license change.

When you modify an existing Haiku file, **preserve the existing Haiku
copyright lines**, add your own copyright line below them, and add
yourself to the `Authors:` list:

```cpp
/*
 * Copyright (c) 2002, Thomas Kurschel
 * Copyright 2004-2016 Haiku, Inc. All rights reserved.
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Thomas Kurschel
 *		Clemens Zeidler, <haiku@clemens-zeidler.de>
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 *		Kevin Adams <kevinadams05@gmail.com>
 */
```

For a **brand-new** file authored from scratch:

```cpp
/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Kevin Adams <kevinadams05@gmail.com>
 */
```

### 16.1 Years

Update the year range when you make a substantive change. `2026` for a
brand-new file. Trivial typo fixes do not bump the year.

### 16.2 Upstreaming

Because the fork is MIT — same license as Haiku itself — there is no
license barrier to a fix eventually landing upstream. That is not a
goal of this project (it ships as a standalone `.hpkg`), but keeping
everything MIT and driver-only means a clean fix could be offered to
Haiku without relicensing.

## 17. Resource management

- Stack objects over heap objects whenever possible.
- For scoped area / GART mapping in the kernel driver, use the existing
  RAII helpers — `AreaKeeper` (from `<AutoDeleterDrivers.h>` /
  the shared driver utilities) and `AGPGartMapper` — and call
  `Detach()` only when ownership is handed off. `radeon_hd_init` is the
  canonical example.
- For locks in the kernel driver, prefer `MutexLocker` /
  `InterruptsSpinLocker` (RAII scoping) over manual `mutex_lock` /
  `mutex_unlock` pairs. The driver's global `gLock` is a `mutex`.
- For dynamically-allocated kernel resources (areas, semaphores):
  explicit cleanup in `uninit_driver` / `free_device` where the
  lifetime is not naturally scoped (`delete_area(info.registers_area)`
  etc.).
- No `goto cleanup:` patterns. RAII or early return.

## 18. Dead code, debug code, and printfs

- No `#if 0` blocks. Delete the code; git keeps history.
- No leftover raw `printf` / `fprintf(stderr, ...)` — promote to
  `TRACE()` / `ERROR()` (see §19) or remove.
- Long-lived diagnostic code lives behind the per-file `TRACE_*` define
  (see §19) and **must compile warning-clean** whether that define is
  on or off.

## 19. Logging

`radeon_hd` uses a **per-file** logging pattern, not a shared logging
header. Each `.cpp` defines a `TRACE_<MODULE>` switch near the top,
gates `TRACE()` on it, and defines `ERROR()` as always-on. All messages
are prefixed `"radeon_hd: "`.

### 19.1 Kernel driver

The kernel side logs through `dprintf()` (syslog / serial):

```cpp
#define TRACE_DRIVER
#ifdef TRACE_DRIVER
#	define TRACE(x...) dprintf("radeon_hd: " x)
#else
#	define TRACE(x...) ;
#endif

#define ERROR(x...) dprintf("radeon_hd: " x)
```

Usage:

```cpp
TRACE("card(%" B_PRId32 "): %s: called\n", info.id, __func__);
ERROR("%s: card (%" B_PRId32 "): couldn't map shared area!\n",
	__func__, info.id);
```

### 19.2 Accelerant

The accelerant side is identical in shape but logs through `_sPrintf()`
(the accelerant runs inside `app_server`):

```cpp
#define TRACE_MODE
#ifdef TRACE_MODE
#	define TRACE(x...) _sPrintf("radeon_hd: " x)
#else
#	define TRACE(x...) ;
#endif

#define ERROR(x...) _sPrintf("radeon_hd: " x)
```

There is **no** `TRACE_ERROR` macro in this codebase — that is a
different project. Use `ERROR()`. When adding a new `.cpp`, copy the
logging block from a sibling file and name the switch after the module
(`TRACE_DISPLAY`, `TRACE_ENCODER`, `TRACE_HDMI`, ...).

Do not introduce per-file `fprintf(stderr, ...)` loggers — the kernel
has no `stderr`, and the accelerant runs inside `app_server` where
stderr behavior is unspecified.

## 20. PR checklist

Before opening a PR, verify:

- [ ] Change stays inside the `radeon_hd` driver tree (§1.1).
- [ ] Any new source file is added to its `Jamfile` *and* the overlay
      copy in `scripts/build.sh` (§1.2).
- [ ] `scripts/build.sh` builds the accelerant and kernel driver clean.
- [ ] No lines over 80 columns without a real justification.
- [ ] Public/header API has Doxygen comments.
- [ ] No raw `printf`/`fprintf` debug leftovers; logging goes through
      `TRACE()` / `ERROR()`.
- [ ] No `#if 0` blocks.
- [ ] Copyright headers correct: MIT, existing Haiku lines preserved,
      your line and `Authors:` entry added (§16).
- [ ] No code copied from Linux — register knowledge only (§1.4).
- [ ] File ends with a newline.

---

## Appendix A — Quick reference card

```
Indent: TAB (width 4)
Line:   hard cap 80
Brace:  class/struct same line; function own line; if/for/while same line
Naming: snake_case structs + free fns (radeon_info, radeon_hd_init);
        UpperCamel for new C++ classes; lowerCamel locals;
        g prefix globals (gInfo, gPCI); k prefix consts (kSupportedDevices)
        (driver/accelerant entry points stay lower_case_with_underscores)
Pointer: radeon_info* info = NULL;
Cast:   static_cast<T>(x);   (T)x for primitives only
        addr |= (uint64)bar[n + 1] << 32;  for 64-bit BAR assembly
Null:   NULL, no nullptr; no NULL-check before delete/free
Bool:   true/false, never TRUE/FALSE
Bitmask: if ((flags & CHIP_IGP) != 0)
Switch: case indented; { } if vars; default: required
Types:  Haiku types; B_PRId32/B_PRIx32 in format strings
Strings: char[N]+sprintf in kernel; BString in accelerant
Errors: status_t, B_OK on success
Logging: per-file TRACE_<MODULE> gate; TRACE()/ERROR() prefixed "radeon_hd: "
         (kernel dprintf, accelerant _sPrintf; NO TRACE_ERROR macro)
License: MIT (Haiku two-line form)
```

## Appendix B — Hardware quick facts

Facts a contributor needs to keep examples accurate:

- **Device node:** `/dev/graphics/radeon_hd_<bus><device><function>`,
  built with `sprintf(name, "graphics/radeon_hd_%02x%02x%02x", ...)`.
- **PCI vendor:** ATI/AMD, `VENDOR_ID_ATI` = `0x1002`.
- **BAR layout (PCI header type 0):**
  - **BAR 0/1** — framebuffer aperture (`PCI_BAR_FB` = 0; 64-bit, so it
    consumes BAR 0 and BAR 1).
  - **MMIO registers** — **BAR 2** on chipsets before Bonaire, and
    **BAR 5** on Sea Islands and later (Bonaire+). This is exactly what
    `radeon_hd_pci_bar_mmio()` returns: `chipsetID < RADEON_BONAIRE`
    → 2, else 5.
- **Chipset enum:** `enum radeon_chipset` (`RADEON_R420` …
  `RADEON_NAVI`) in `radeon_hd.h`, paired with the `radeon_chip_name[]`
  string table — the two must stay in sync.
- **Supported-device table:** `kSupportedDevices[]` of
  `struct supported_device` in `driver.cpp` (pciID, dceMajor/Minor,
  chipsetID, chipsetFlags, deviceName).
- **Key structs:** `radeon_info` (per-card kernel state,
  `radeon_hd_private.h`), `radeon_shared_info` (kernel↔accelerant shared
  area, `radeon_hd.h`), `accelerant_info` (accelerant state, reached
  through the `gInfo` global).
- **DCE generations:** the driver dispatches a lot of behavior on
  `dceMajor` / `chipsetID` thresholds (`RADEON_R600`, `RADEON_RV770`,
  `RADEON_CEDAR`, `RADEON_CAICOS`, `RADEON_TAHITI`, `RADEON_BONAIRE`,
  …). See `docs/dce-vs-dcn-driver-boundaries.md`.

## Appendix C — Distribution

RadeonHD ships as a standalone, unofficial `.hpkg`, built by overlaying
this repo onto a Haiku source tree (`scripts/build.sh` →
`scripts/package.sh`). The repo lives at
`KevinAdams05/RadeonHDunofficial`. It is not part of the Haiku source
tree and is not (currently) aiming for upstream inclusion, though its
MIT license leaves that door open.
