>[!NOTE]
>An LLM was used to aid in development of this code.

# Building & Packaging

Maintainer-facing notes for producing a `.hpkg` of the RadeonHD
(unofficial) driver. End users do not need any of this — they just
install the published `.hpkg` per the [README install
section](../README.md#installation). This document is for whoever is
cutting builds.

## How the build is structured

The fork is **driver-only by design** — every change lives inside the
`src/add-ons/{accelerants,kernel/drivers/graphics}/radeon_hd/` and
`headers/private/graphics/radeon_hd/` trees, all paths matching upstream
Haiku exactly. This repo holds **only the modified files** (an overlay),
not the entire Haiku source tree.

Building therefore needs three things:

1. A configured Haiku source tree on a Linux build host (cross-tools
   already set up; this repo doesn't bootstrap that).
2. The two scripts in this repo:
   - `scripts/build.sh` — overlays this repo's modified files onto the
     Haiku source tree and runs `jam` to produce the binaries.
   - `scripts/package.sh` — stages the binaries into the per-user
     packagefs layout and runs `package create` to produce a `.hpkg`.

The output is a single `.hpkg` users drop into
`~/config/packages/` on their Haiku machine.

## One-time setup on the build host

The RadeonHD project's build server is `kevin@192.168.74.122` (a Linux
VM); these notes assume a Linux box with similar setup. Adapt as needed.

### Haiku source tree

The build script defaults to `$HOME/haiku-build/haiku`. Mirror that
layout or pass `HAIKU_SRC=...` as the first argument to `build.sh`.

```bash
cd ~/haiku-build
git clone https://review.haiku-os.org/haiku
cd haiku
mkdir generated.x86_64
cd generated.x86_64
../configure --cross-tools-source ../../buildtools --build-cross-tools x86_64
```

A full bootstrap build is not required, but the cross-tools and the
host-side build tools (notably `package`) **are** required.

### Host `package` tool

`scripts/package.sh` invokes `package create`, which on Haiku is on
`$PATH` but on Linux is a host-side tool built as part of the Haiku
configure step. It lives at:

```
$HAIKU_SRC/generated.$ARCH/objects/linux/x86_64/release/tools/package/package
```

The script auto-discovers it by that path. If you keep your Haiku tree
somewhere unusual, set `HOST_PACKAGE` to the binary location.

### Clone this repo

```bash
git clone https://github.com/KevinAdams05/RadeonHDunofficial.git RadeonHD
cd RadeonHD
chmod +x scripts/*.sh
```

## Cutting a build

The two scripts are run in sequence:

```bash
cd ~/RadeonHD
bash scripts/build.sh
bash scripts/package.sh <version>
```

`<version>` defaults to `0.0.YYYYMMDD` if you omit it. Use a real
semver like `0.4.0` for tagged releases.

### What `build.sh` does

1. Copies headers and sources from this repo's overlay
   (`headers/private/graphics/radeon_hd/`,
    `src/add-ons/accelerants/radeon_hd/`,
    `src/add-ons/kernel/drivers/graphics/radeon_hd/`)
   over the corresponding directories in the Haiku source tree at
   `$HAIKU_SRC`.

2. `cd $HAIKU_SRC/generated.$ARCH && jam -q -j$(nproc) radeon_hd.accelerant radeon_hd`

3. Extracts the two built binaries:
   - Accelerant: `objects/haiku/$ARCH/release/add-ons/accelerants/radeon_hd/radeon_hd.accelerant`
   - Kernel driver: `objects/haiku/$ARCH/release/add-ons/kernel/drivers/graphics/radeon_hd/radeon_hd`

4. Copies them to `build/$ARCH/` in this repo (gitignored).

After this step you have buildable binaries that match what the .hpkg
will ship.

### What `package.sh` does

1. Stages a tree at `build/staging-$ARCH/` with the per-user packagefs
   layout (see "Why per-user, not /system" below):

   ```
   add-ons/
     accelerants/
       radeon_hd.accelerant
     kernel/
       drivers/
         bin/radeon_hd
         dev/graphics/radeon_hd  ->  ../../bin/radeon_hd
   .PackageInfo
   ```

2. Substitutes `@VERSION@` in `packaging/PackageInfo.in` to produce the
   final `.PackageInfo`.

3. Runs `package create` on the staging directory.

4. Drops the resulting `.hpkg` at
   `dist/radeon_hd_unofficial-<version>-$ARCH.hpkg`.

The `dist/` directory is gitignored — `.hpkg` files don't go in the
source repo. They get attached to GitHub releases instead.

## Why per-user, not /system

The package install path matters because Haiku's `radeon_hd` is part of
the `haiku` package itself. We can't replace files inside a system
package without declaring `replaces { haiku }`, which would make the
fork mutually-exclusive with stock Haiku — far too heavy a hammer.

Instead, the package is dropped in `~/config/packages/`. packagefs
union-mounts its contents under `~/config/`, producing
`~/config/add-ons/accelerants/radeon_hd.accelerant` (and similar for
the kernel driver). Haiku's accelerant + driver loaders search user
paths before system paths, so our binaries are picked first at runtime.
The stock haiku-package version stays untouched at
`/system/add-ons/accelerants/radeon_hd.accelerant` — it just never
gets reached.

This also means there's **no file conflict at the package-resolver
layer**: user-packagefs and system-packagefs are distinct unions, so
both packages can declare the same logical path with no collision.

The earlier attempt was a `non-packaged/...` layout inside the .hpkg.
That doesn't work — `non-packaged/` is specifically the path *outside*
the packagefs union, and packages cannot write to it. Files we shipped
under that prefix were silently dropped on install. The fix was the
standard `add-ons/...` layout above.

## Testing the .hpkg

```bash
# From the build host:
scp ~/RadeonHD/dist/radeon_hd_unofficial-<ver>-x86_64.hpkg \
    user@<test-box>:/boot/home/config/packages/

# Then on the test box (or via SSH):
ssh user@<test-box> "/boot/home/reboot.sh"
```

After reboot, verify the override actually took effect:

```bash
ssh user@<test-box> "
    pkgman search radeon
    ls -la /boot/home/config/add-ons/accelerants/radeon_hd.accelerant
    ls -la /boot/home/config/add-ons/kernel/drivers/bin/radeon_hd
    cat /var/log/syslog | grep -i radeon_hd | head
"
```

You should see the package listed, both binaries union-mounted at
`~/config/...`, and the driver init lines in syslog.

For a Caicos card specifically, the cap should fire on any mode above
1080p:

```
radeon_hd: is_mode_supported: rejecting <NxM> on Caicos
   (pixel clock <K> kHz exceeds 165 MHz linear-scanout cap)
```

If you see that line, the fork's accelerant is the one running.

## Uninstall / revert to stock

```bash
rm /boot/home/config/packages/radeon_hd_unofficial-*.hpkg
# reboot
```

That's it. The stock haiku package version comes back as the only one
the loader can see.

## Versioning

The fork is independent of Haiku's versioning. Use semver:

- `0.4.0`, `0.5.0`, ... bumps for substantive changes
- `0.x.0-1`, `0.x.0-2`, ... bumps for repackaging the same source
- `0.0.YYYYMMDD` is a date-tag fallback for ad-hoc dev builds (the
  `package.sh` default if you forget to pass a version)

## CI / release

There's no CI yet. Releases are cut manually:

1. `bash scripts/build.sh && bash scripts/package.sh <version>` on the
   build server.
2. Test on at least the AX5450 (Cedar) and HD 7470 (Caicos) — the
   currently-validated cards.
3. `gh release create v<version> dist/radeon_hd_unofficial-<version>-x86_64.hpkg`
   from the build host (or upload through the GitHub web UI).
4. Bump the README install instructions if the version reference is
   in the install snippet.

## Layout reference

What's in this repo:

| Path | Purpose |
|---|---|
| `README.md` | User-facing project overview + install instructions |
| `docs/technical-documentation.md` | Per-fix technical write-up (Phase 1 → 4) |
| `docs/building-and-packaging.md` | This file |
| `diagrams/*.svg` | Diagrams referenced from the technical docs |
| `headers/private/graphics/radeon_hd/*.h` | Modified headers (overlay) |
| `src/add-ons/accelerants/radeon_hd/*.{cpp,h}` | Modified accelerant sources (overlay) |
| `src/add-ons/kernel/drivers/graphics/radeon_hd/*.cpp` | Modified kernel driver sources (overlay) |
| `scripts/build.sh` | Overlay + jam wrapper |
| `scripts/package.sh` | Stage + `package create` wrapper |
| `scripts/dumpregs.py` | Diagnostic tool (Linux register-state capture) |
| `packaging/PackageInfo.in` | Template for the package metadata |
| `build/`, `dist/` | Gitignored build outputs |
