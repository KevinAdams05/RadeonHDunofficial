#!/usr/bin/env bash
#
# Bundle the built radeon_hd accelerant + kernel driver into a .hpkg.
#
# Run scripts/build.sh first to produce the binaries under build/<ARCH>/.
#
# Layout inside the .hpkg:
#   non-packaged/add-ons/accelerants/radeon_hd.accelerant
#   non-packaged/add-ons/kernel/drivers/bin/radeon_hd
#   non-packaged/add-ons/kernel/drivers/dev/graphics/radeon_hd  -> ../../bin/radeon_hd
#
# Files install into /boot/system/non-packaged/, where the loader picks
# them ahead of the stock /boot/system/add-ons/ paths from the haiku
# package — this overrides the bundled driver without a `replaces`
# declaration or any conflict with the haiku package itself.
#
# Usage:
#   scripts/package.sh [VERSION]
#
# VERSION defaults to a date-based tag like 0.0.YYYYMMDD if not given.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ARCH="${ARCH:-x86_64}"
VERSION="${1:-${VERSION:-0.0.$(date +%Y%m%d)}}"

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

echo "==> Staging package contents in $STAGING"
rm -rf "$STAGING"
mkdir -p "$STAGING/non-packaged/add-ons/accelerants"
mkdir -p "$STAGING/non-packaged/add-ons/kernel/drivers/bin"
mkdir -p "$STAGING/non-packaged/add-ons/kernel/drivers/dev/graphics"

# Accelerant
cp -v "$ACCELERANT" \
	"$STAGING/non-packaged/add-ons/accelerants/radeon_hd.accelerant"

# Kernel driver binary
cp -v "$KERNEL_DRV" \
	"$STAGING/non-packaged/add-ons/kernel/drivers/bin/radeon_hd"

# Per-Haiku convention, the device node lives in dev/graphics/ as a
# symlink to the actual binary in bin/. The kernel driver glue uses
# this symlink for hot-plug detection.
ln -sf "../../bin/radeon_hd" \
	"$STAGING/non-packaged/add-ons/kernel/drivers/dev/graphics/radeon_hd"

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
package create -C . "$HPKG"

echo ""
echo "==> Package built: $HPKG"
ls -la "$HPKG"
echo ""
echo "==> Contents:"
package list "$HPKG" | head -20
