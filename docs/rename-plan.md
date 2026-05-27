> [!NOTE]
> An LLM was used to aid in development of this code.

# Rename to `radeon_dce` + DCN Purge — Plan

Status: **draft, not yet executed**. Awaiting HD 6850 hardware validation
and Kevin's go-ahead.

## Why

The radeon_hd driver targets AMD's **DCE** (Display Controller Engine)
display IP — a pipeline driven heavily by AtomBIOS command tables. AMD's
modern display block, **DCN** (Display Core Next), starting with Raven
Ridge (2017) and all of Navi/RDNA, is architecturally different and
deliberately moved most logic out of the BIOS and into the driver
(Linux's DAL/DC). Without porting DAL/DC, this driver fundamentally
cannot drive a DCN card.

The current PCI table claims a number of DCN parts anyway. Those entries
silently bind, fail to drive the display, and fall back to framebuffer
(or worse — partial init noise). They're false advertising.

Two coordinated corrections:

1. **Rename the project/repo/package** to communicate the real scope.
2. **Purge the DCN entries** from the PCI table so the driver only
   claims hardware it can actually drive.

## Hard constraint: the kernel driver binary filename must NOT change

The `.hpkg` works as a "drop-in replacement" because both packages
install a file at the *same path* (`system/add-ons/kernel/drivers/bin/radeon_hd`).
Packagefs shadows the system file with the user-config file. The kernel
sees one driver named `radeon_hd`. Your fixes win.

If the binary is renamed to `radeon_dce`, the shadow stops working.
Both drivers load, both claim the same PCI IDs, both publish device
nodes, and app_server picks one based on undefined enumeration order.
There is no Haiku driver-priority knob to settle ties.

→ **The driver binary, accelerant binary, and their install paths stay
named `radeon_hd`.** Everything else can change.

## What renames

| Layer | From | To |
|---|---|---|
| GitHub repo | `KevinAdams05/RadeonHDunofficial` | `KevinAdams05/RadeonDCEunofficial` (TBD final name) |
| Package name (PackageInfo `name`) | `radeon_hd_unofficial` | `radeon_dce_unofficial` |
| Package `provides` | `radeon_hd_unofficial` | `radeon_dce_unofficial` |
| `.hpkg` filename | `radeon_hd_unofficial-X.Y.Z-1-x86_64.hpkg` | `radeon_dce_unofficial-X.Y.Z-1-x86_64.hpkg` |
| Summary text in PackageInfo | "Unofficial radeon_hd driver fork…" | "Unofficial radeon_dce driver fork… (DCE-era AMD GPUs)" |
| `urls` in PackageInfo | old repo URL | new repo URL |
| README title + body | "RadeonHD (unofficial)" | "RadeonDCE (unofficial)" |
| `docs/*.md` body references | radeon_hd / "the driver" | radeon_dce / "the driver" (selectively — keep references to the kernel driver `radeon_hd` binary as-is) |
| `CHANGELOG.md` | header | header bumped to new project name + entry explaining the rename |

## What does NOT rename

| Item | Reason |
|---|---|
| Kernel driver binary filename `radeon_hd` | File-path shadow requires it |
| Accelerant binary filename `radeon_hd.accelerant` | Same |
| Source tree directories (`src/…/radeon_hd/`, `headers/private/graphics/radeon_hd/`) | Match the binary name for build clarity; would otherwise need Jamfile rework |
| Jamfile target names | Same |
| C++ class names, namespaces, function names | Internal; no user-visible benefit; large diff for zero gain |
| Trace/log string literals inside the driver (e.g. syslog tag) | Stays `radeon_hd` so syslog grep still works and matches the binary identity |
| `safemode` and blacklist entry names | Still `radeon_hd` because that's still the driver's kernel-visible name |

The rule of thumb: anything the **operating system or user types**
stays `radeon_hd`; anything the **GitHub / package ecosystem / docs**
shows is `radeon_dce`.

## DCN purge

Remove from `src/add-ons/kernel/drivers/graphics/radeon_hd/driver.cpp`:

| Lines | Family | Cards covered |
|---|---|---|
| 620-621 | `RADEON_RAVEN` | Raven Ridge APU (DCN 1.0) |
| 629-66x+ | `RADEON_NAVI` | Navi 1x/2x/3x discrete (RX 5500–7900), plus Renoir, Van Gogh, etc. |

Audit pass needed to catch:
- Any code paths gated on `RADEON_RAVEN` or `RADEON_NAVI` (probably none meaningful — that's why these never worked).
- The `RADEON_FAMILY` enum: remove the corresponding enumerators if nothing else references them.
- Any AtomBIOS or asic-init code branching on these families.

Definitely **keep** these DCE-era families:
- `RADEON_VEGA`, `RADEON_VEGAM` (DCE 12 — Vega 10/20 discrete, Kaby Lake-G hybrid)
- `RADEON_POLARIS` (DCE 12)
- `RADEON_FIJI`, `RADEON_TONGA` (DCE 10)
- `RADEON_HAWAII`, `RADEON_BONAIRE` (DCE 8)
- `RADEON_KAVERI`, `RADEON_KABINI`, `RADEON_MULLINS`, `RADEON_CARRIZO`, `RADEON_STONEY` (DCE 8/11 APUs)
- Everything older (Northern Islands, Evergreen, etc.)

`RADEON_HAINAN` is already commented out (line 420) — leave it; the comment is informative.

## GitHub repo rename mechanics

GitHub supports in-place repo rename — **no new repo needed**.

- Settings → Repository name → enter new name → Rename.
- GitHub automatically sets up redirects for the old URL: `git fetch`,
  `git push`, web links, issue links, raw URLs all keep working
  indefinitely (unless someone else later creates a repo at the old
  name).
- Stars, watchers, forks, issues, PRs, releases, wiki, and discussions
  all move with the repo.
- Release tarball URLs change (old release direct links break if
  bookmarked; they redirect through the GitHub UI but not for
  pre-release attachments fetched by exact URL).

After rename:
- Update your local clone: `git remote set-url origin <new URL>`.
- Update the `urls` field in `packaging/PackageInfo.in`.
- Update README links / badges if any embed the repo path.
- Update memory: `project_radeon_hpkg_distribution.md` references
  `KevinAdams05/RadeonHDunofficial`.

## Final repo name candidates

Pick one:

- `RadeonDCEunofficial` — minimal change from current, preserves the
  "unofficial" marker. **Recommended** for continuity.
- `radeon_dce-haiku` — closer to upstream Haiku naming style.
- `RadeonDCE` — drops the "unofficial" qualifier; cleaner but loses the
  signal that this isn't the OS-bundled driver.

## User migration

Renaming the package name (not the binary) means existing installations
won't auto-upgrade — `pkgman` treats `radeon_dce_unofficial` as a
different package from `radeon_hd_unofficial`. Two options:

1. **Document a manual transition** in the release notes:
   ```
   pkgman uninstall radeon_hd_unofficial
   pkgman install radeon_dce_unofficial
   ```
   Simple but trusts users to read.

2. **Use a PackageInfo `replaces`/`conflicts` directive** to declare
   the old package obsolete. *Needs research* — confirm what
   directives Haiku's `hpkg` format actually supports. If it has a
   "supersedes" or "conflicts" mechanism, the package manager can
   handle the swap automatically.

→ **TODO before execution:** check Haiku package-format docs for
this. If supported, prefer the automatic path.

## Version bump

This is a meaningful project change. Bump:

- 0.x.y → 1.0.0, **or**
- 0.5.0 → 0.6.0 if reserving 1.0 for "validated as stable on N cards"

Either is fine; pick based on whether you're treating the rename as a
maturity milestone or just a coordinated cleanup.

`CHANGELOG.md` entry should cover:

- The rename rationale (DCN is architecturally different — see
  `docs/dce-vs-dcn-driver-boundaries.md`).
- The DCN PCI ID purge (list specifically which IDs were removed so
  affected users understand).
- The migration command for existing installs.
- A note that the kernel driver binary name and behavior haven't
  changed — same syslog tag, same safemode handling.

## Execution sequence

1. Validate HD 6850 (Barts, DCE 5.0) on current `radeon_hd` build
   first. The rename should not happen on a regression.
2. Branch: `radeon/rename-dce/radeon_hd-X.Y.Z-rename-dce` (per
   HaikuTools branch naming).
3. Apply changes:
   - PCI table purge in driver.cpp (RAVEN + NAVI entries).
   - `RADEON_FAMILY` enum cleanup if needed.
   - `packaging/PackageInfo.in` updates.
   - README / CHANGELOG / docs body updates.
4. Build .hpkg with new package name; smoke-test install on the
   build server VM.
5. Test driver on real hardware (Cedar baseline + HD 6850 Barts) —
   confirm no functional regression vs. previous .hpkg.
6. Merge to main on the existing repo.
7. Rename GitHub repo via Settings.
8. `git remote set-url fork <new URL>` locally.
9. Update memory: rewrite `project_radeon_hpkg_distribution.md`.
10. Cut release with bumped version.
11. Pin a release-notes issue at the top of the renamed repo
    explaining the migration for existing users.

## Open questions to resolve before execution

- Final repo name (default: `RadeonDCEunofficial`).
- Version bump target (default: 1.0.0).
- Does Haiku `hpkg` support `replaces`/`conflicts` for clean package
  migration?
- Whether to also rename `radeon_hd_unofficial` mentions inside
  internal code comments (low priority — sweep at leisure).

## Risk register

| Risk | Mitigation |
|---|---|
| Existing users miss the rename and stay on the old package | Pin release-notes issue, document `pkgman` swap |
| Rename + DCN purge land in same release, masking which change caused any new bug | Land DCN purge as its own commit on the rename branch so it can be reverted independently |
| Old `radeon_hd_unofficial` package lingers in users' `~/config/packages/` and conflicts at runtime | Migration doc must explicitly say `pkgman uninstall` first |
| GitHub URL redirects degrade if the old repo name ever gets squatted | Reserve the old name as a placeholder repo right after rename |
