#!/usr/bin/env bash
#
# Bundle the built radeon_hd accelerant + kernel driver into a .hpkg.
#
# Run scripts/build.sh first to produce the binaries under build/<ARCH>/.
#
# Layout inside the .hpkg (standard packagefs paths):
#   add-ons/accelerants/radeon_hd.accelerant
#   add-ons/kernel/drivers/bin/radeon_hd
#   add-ons/kernel/drivers/dev/graphics/radeon_hd  -> ../../bin/radeon_hd
#
# Install path: per-user, by dropping the .hpkg in ~/config/packages/.
# packagefs union-mounts the contents at ~/config/add-ons/..., which the
# kernel/accelerant loader checks BEFORE /system/add-ons/...  The stock
# haiku package's radeon_hd at /system/add-ons/... is never reached, so
# our binaries override it without a file-level conflict at the package-
# resolver layer (different filesystem paths in the union mount).
#
# Usage:
#   scripts/package.sh [VERSION]
#
# VERSION defaults to a date-based tag like 0.0.YYYYMMDD if not given.
#
# A pre-release gate runs before staging: style-check must report no new
# findings, and neither binary may be older than the newest tracked
# source. SKIP_GATE=1 bypasses it (loudly) for emergencies.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ARCH="${ARCH:-x86_64}"
VERSION="${1:-${VERSION:-0.0.$(date +%Y%m%d)}}"

# `package create` is a host tool built as part of Haiku. On Linux it is
# not on $PATH; locate the host-build copy alongside the cross-tools.
HAIKU_SRC="${HAIKU_SRC:-$HOME/haiku-build/haiku}"
HOST_OBJECTS="$HAIKU_SRC/generated.$ARCH/objects/linux/x86_64/release"
HOST_PACKAGE="${HOST_PACKAGE:-$HOST_OBJECTS/tools/package/package}"
if ! command -v package >/dev/null 2>&1; then
	if [ -x "$HOST_PACKAGE" ]; then
		PACKAGE_CMD="$HOST_PACKAGE"
	else
		echo "ERROR: 'package' tool not found." >&2
		echo "       Looked for: $HOST_PACKAGE" >&2
		echo "       Set HOST_PACKAGE to the host-built 'package'." >&2
		exit 1
	fi
else
	PACKAGE_CMD="$(command -v package)"
fi

BUILD_DIR="${REPO_ROOT}/build/${ARCH}"
DIST_DIR="${REPO_ROOT}/dist"
STAGING="${REPO_ROOT}/build/staging-${ARCH}"

ACCELERANT="${BUILD_DIR}/radeon_hd.accelerant"
KERNEL_DRV="${BUILD_DIR}/radeon_hd"

if [ ! -f "$ACCELERANT" ] || [ ! -f "$KERNEL_DRV" ]; then
	echo "ERROR: Built binaries missing in $BUILD_DIR" >&2
	echo "       Run scripts/build.sh first." >&2
	exit 1
fi

# ---------------------------------------------------------------------
# Pre-release gate
#
# Two checks, each guarding a mistake that has actually shipped or
# nearly shipped from this repo:
#
#   style      scripts/style-check.py must report no *new* findings
#              against scripts/style-baseline.txt.
#
#   staleness  neither binary may be older than the newest tracked
#              source file. jam prints little on a no-op build, so a
#              failed or skipped compile looks exactly like a good one
#              and it is easy to package the previous binary by
#              accident.
#
# Set SKIP_GATE=1 to bypass both. That is for emergencies only: it is
# reported loudly and should never be how a release is cut.
# ---------------------------------------------------------------------
if [ "${SKIP_GATE:-0}" = "1" ]; then
	echo "!!! PRE-RELEASE GATE BYPASSED (SKIP_GATE=1)" >&2
	echo "!!! Style and staleness were NOT verified." >&2
	echo ""
else
	echo "==> Pre-release gate: style"
	if ! "$REPO_ROOT/scripts/style-check.py"; then
		echo "" >&2
		echo "ERROR: style-check reported new findings (above)." >&2
		echo "       Fix them, or if they are genuinely acceptable," >&2
		echo "       record them with:" >&2
		echo "         scripts/style-check.py --update-baseline" >&2
		exit 1
	fi

	echo "==> Pre-release gate: artifact staleness"
	NEWEST_SOURCE=""
	NEWEST_MTIME=0
	# Prefer git, but fall back to find: a release may be built from an
	# unpacked tarball that has no .git directory.
	if git -C "$REPO_ROOT" rev-parse --git-dir >/dev/null 2>&1; then
		list_sources() {
			git -C "$REPO_ROOT" ls-files 'src/*' 'headers/*'
		}
	else
		list_sources() {
			cd "$REPO_ROOT" && find src headers \
				\( -name '*.c' -o -name '*.cpp' -o -name '*.h' \) -type f
		}
	fi
	while IFS= read -r source_file; do
		[ -f "$REPO_ROOT/$source_file" ] || continue
		mtime="$(stat -c %Y "$REPO_ROOT/$source_file")"
		if [ "$mtime" -gt "$NEWEST_MTIME" ]; then
			NEWEST_MTIME="$mtime"
			NEWEST_SOURCE="$source_file"
		fi
	done < <(list_sources)

	for binary in "$ACCELERANT" "$KERNEL_DRV"; do
		binary_mtime="$(stat -c %Y "$binary")"
		if [ "$binary_mtime" -lt "$NEWEST_MTIME" ]; then
			echo "ERROR: $(basename "$binary") is older than" >&2
			echo "       $NEWEST_SOURCE." >&2
			echo "       The binary predates the source it should" >&2
			echo "       contain. Re-run scripts/build.sh and confirm" >&2
			echo "       it exits 0." >&2
			exit 1
		fi
	done
	echo "    both binaries are newer than $NEWEST_SOURCE"
	echo ""
fi

echo "==> Staging package contents in $STAGING"
rm -rf "$STAGING"
mkdir -p "$STAGING/add-ons/accelerants"
mkdir -p "$STAGING/add-ons/kernel/drivers/bin"
mkdir -p "$STAGING/add-ons/kernel/drivers/dev/graphics"

# Accelerant
cp -v "$ACCELERANT" \
	"$STAGING/add-ons/accelerants/radeon_hd.accelerant"

# Kernel driver binary
cp -v "$KERNEL_DRV" \
	"$STAGING/add-ons/kernel/drivers/bin/radeon_hd"

# Per-Haiku convention, the device node lives in dev/graphics/ as a
# symlink to the actual binary in bin/. The kernel driver glue uses
# this symlink for hot-plug detection.
ln -sf "../../bin/radeon_hd" \
	"$STAGING/add-ons/kernel/drivers/dev/graphics/radeon_hd"

echo ""
echo "==> Generating .PackageInfo (version $VERSION)"
PKGINFO="$STAGING/.PackageInfo"
sed "s/@VERSION@/${VERSION}/g" \
	"$REPO_ROOT/packaging/PackageInfo.in" > "$PKGINFO"

echo "==> Building .hpkg"
mkdir -p "$DIST_DIR"
HPKG="${DIST_DIR}/radeon_hd_unofficial-${VERSION}-${ARCH}.hpkg"
rm -f "$HPKG"

# `package create` reads .PackageInfo from the directory and packs
# everything else under it (relative paths preserved).
cd "$STAGING"
"$PACKAGE_CMD" create -C . "$HPKG"

echo ""
echo "==> Package built: $HPKG"
ls -la "$HPKG"
echo ""
echo "==> Contents:"
"$PACKAGE_CMD" list "$HPKG" | head -20
