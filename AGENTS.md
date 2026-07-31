# AGENTS.md — working rules for this repository

Guidance for anyone — human or AI agent — making changes to the
RadeonHD (unofficial) driver fork. It is deliberately short and points
at the authoritative documents rather than restating them.

If you read nothing else, read the four non-negotiables below.

---

## The four non-negotiables

1. **Follow `docs/STYLE_GUIDE.md`.** It is the Haiku coding guidelines
   plus the project notes in its §1. Where it is silent, defer to
   Haiku. Where it conflicts with the file you are editing, **the file
   wins** — see "Do not restyle upstream code" below.
2. **A release ships the kernel driver *and* the accelerant, from the
   same build.** Never half. See "Kernel + accelerant" below for the
   two independent reasons.
3. **Run the linter before you commit:**
   `python3 scripts/style-check.py`
   It must exit 0. `scripts/package.sh` enforces this at release time,
   but do not make the gate the first time anyone finds out.
4. **Update the docs before you push.** Which docs, and when, is
   enumerated in "Documentation duties" below.

---

## What this repository is

An **overlay**, not a buildable tree. It carries only the files the
fork actually modifies:

- `src/add-ons/kernel/drivers/graphics/radeon_hd/` — kernel driver
- `src/add-ons/accelerants/radeon_hd/` — userspace accelerant
- `headers/private/graphics/radeon_hd/` — register headers only
  (`evergreen_reg.h`, `ni_reg.h`, `sea_reg.h`)

`scripts/build.sh` copies these over a configured Haiku source tree and
runs `jam` there. Two consequences that catch people out:

- To read an unmodified driver file, open it in the **Haiku tree**. If
  it is not in this repo, the fork never changed it.
- A new source file must be added to its `Jamfile` **and** to the
  overlay copy step in `scripts/build.sh`, or jam will never see it.

**Scope is driver-only.** Changes stay inside the three directories
above. No app_server, kernel, or shared-header patches — those belong
upstream, not in this fork (`STYLE_GUIDE.md` §1.1).

**Linux is a reference, never a copy source.** Read Linux `radeon`/
`amdgpu` for register semantics and sequencing, then write our own
implementation. Do not copy code — the licences differ
(`STYLE_GUIDE.md` §1.4).

---

## Kernel + accelerant

Two separate reasons, both documented in full under "Packaging
Constraint" in `docs/technical-documentation.md`:

1. **ABI lockstep.** The kernel driver creates a `radeon_shared_info`
   area — declared in `headers/private/graphics/radeon_hd/radeon_hd.h`,
   which the fork does **not** carry, so read it from the Haiku tree —
   and the accelerant maps it with `clone_area`. Both dereference fields
   directly — no marshalling, no version negotiation, no reserved
   padding. Mismatched layouts mean crashes or silent state
   corruption. `radeon_shared_info` is the *only* load-bearing struct;
   `accelerant_info` is accelerant-private and may change freely.
2. **The device table.** This fork enables PCI IDs upstream compiles
   out behind `#if 0`. For those cards the *stock* kernel driver never
   binds, so it never publishes `/dev/graphics/radeon_hd_*`, so an
   accelerant-only override is completely inert. The failure is
   **silent** — the system falls through to `vesa`/`framebuffer` and
   you get a working but unaccelerated desktop.

An accelerant-only override is a legitimate *development* shortcut when
`radeon_hd.h` is unchanged between the target's hrev and your tree —
check that, do not assume it. It is never a way to ship or to test a
card whose upstream table entry is gated. When in doubt, stage both.

Related trap: when checking whether a PCI ID is supported, grep is not
enough — the entry may be present but gated. Check the enclosing
preprocessor context, or read the built table rather than the source.

---

## The linter

```
python3 scripts/style-check.py                  # honour the baseline
python3 scripts/style-check.py --all            # show baselined too
python3 scripts/style-check.py --changed        # only what you touched
python3 scripts/style-check.py --list-rules
```

Invoke it through `python3`: this repo keeps `core.fileMode false`, so
nothing under `scripts/` carries an executable bit.

It is a **checker, never a reformatter** — deliberately. It checks
tracked files *and* new untracked ones, because a brand new source file
is exactly where it earns its keep.

### The baseline, and how to treat it

`scripts/style-baseline.txt` records pre-existing findings so the gate
can require zero **new** findings without waiting on a cleanup. It is
not a list of things that are fine; it is a list of things not fixed
yet.

- **Fixing a baselined finding is always welcome.** Fix it, then
  `--update-baseline` to shrink the file.
- **Do not add to it to silence your own new code.** If
  `--update-baseline` grows the file, you almost certainly meant to fix
  the finding instead.
- Most current entries are long lines and `#if 0` blocks in
  upstream-derived files, left alone on purpose (see below).

### Do not restyle upstream code

This is a fork. Reflowing or reformatting upstream code to match the
guide creates permanent diff noise against upstream for zero functional
gain, and makes every future rebase harder. `encoder.cpp` diverges from
upstream by roughly 40 lines out of 2350 — keep it that way.

Likewise, `#if 0` blocks that came from upstream stay. §18's "delete it,
git has the history" holds for code *we* wrote; this repo's history
contains none of upstream's reasoning for those blocks, so deleting them
destroys information that cannot be recovered here.

Style fixes belong in code the fork owns.

---

## Building and verifying

```
bash scripts/build.sh            # overlay + jam
bash scripts/package.sh <ver>    # gate, then .hpkg
```

Both must exit 0. Then:

**Verify the artifact, not the log.** `jam` prints almost nothing on a
no-op build, so a failed or skipped compile looks exactly like a
successful one. This has caused a stale binary to be deployed and a
phantom bug to be chased. Check the exit status, check the binary's
mtime, and confirm a new string literal actually made it in:

```
strings build/x86_64/radeon_hd.accelerant | grep -c "<your new literal>"
```

`package.sh` has a staleness gate for exactly this, but it only runs at
package time.

**The build tree is shared state.** If you edit files directly in the
Haiku tree for an experiment, restore them afterwards
(`git -C <haiku> status` in the driver directories should show only the
files this repo carries). Editing a file the overlay does *not* carry
means `build.sh` will never restore it, and you will build something you
did not intend.

**Tracing must keep compiling when switched off.** Each file gates its
own `TRACE()` behind `#define TRACE_<MODULE>`, but `ERROR()` is
unconditional — so `_sPrintf`/`dprintf` declarations must sit *outside*
the `#ifdef`, and locals used only inside `TRACE()` calls need guarding
or `-Werror` will reject them as unused. The linter's
`trace-error-gate` rule catches the declaration half; only a build with
tracing disabled catches the rest.

---

## Documentation duties

Update these **before pushing**, in the same change as the code:

| When | Update |
|---|---|
| Any behavioural change | `docs/technical-documentation.md` — root cause, registers, evidence |
| Any shipped version | `CHANGELOG.md` (Keep a Changelog + SemVer) and `docs/fixes-by-version.md` |
| New/finished work item | `docs/TODO.md` |
| New convention or rule | `docs/STYLE_GUIDE.md`, and this file if it affects workflow |
| Anything user-visible | `README.md`, including the tested-hardware table |

Conventions:

- **Diagrams are SVG** in `diagrams/`, never ASCII art, and are
  referenced from the doc that explains them.
- **Write down what was wrong, not just what is right.** Several
  sections carry explicit "this earlier claim was wrong, here is why"
  corrections. That is deliberate — a confidently wrong doc costs more
  than a missing one. If you correct a document, say what changed.
- **Record evidence, not conclusions alone.** Register dumps, syslog
  excerpts, and which card and connector something was tested on.
  "Tested on Turks (DCE 5), HDMI, 1080p" beats "works".

When reading a syslog, anchor to the **last** `Welcome to syslog debug
output!` line. The log retains previous boots, and it is very easy to
report a previous card's numbers as the current one's.

---

## Before you commit

`docs/STYLE_GUIDE.md` §20 has the full checklist. The short version:

- [ ] `python3 scripts/style-check.py` exits 0, and the baseline did
      not grow.
- [ ] `bash scripts/build.sh` exits 0 — accelerant *and* kernel driver.
- [ ] Artifact verified fresh, not assumed from a quiet log.
- [ ] New files added to the `Jamfile` *and* `scripts/build.sh`.
- [ ] Change stayed inside the `radeon_hd` tree.
- [ ] Copyright header correct: MIT (Haiku two-line form), existing
      Haiku lines preserved, your line and `Authors:` entry added
      (§16).
- [ ] No code copied from Linux — register knowledge only.
- [ ] Docs updated per the table above.
- [ ] Hardware claims state the card, connector, and resolution tested.

If you could not test on hardware, **say so explicitly** in the commit
message. Untested driver changes are acceptable; untested changes
described as verified are not.
