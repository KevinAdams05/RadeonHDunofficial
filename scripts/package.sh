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

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ARCH="${ARCH:-x86_64}"
VERSION="${1:-${VERSION:-0.0.$(date +%Y%m%d)}}"

# `package create` is a host tool built as part of Haiku. On Linux it is
# not on $PATH; locate the host-build copy alongside the cross-tools.
HAIKU_SRC="${HAIKU_SRC:-$HOME/haiku-build/haiku}"
HOST_PACKAGE="${HOST_PACKAGE:-$HAIKU_SRC/generated.$ARCH/objects/linux/x86_64/release/tools/package/package}"
if ! command -v package >/dev/null 2>&1; then
	if [ -x "$HOST_PACKAGE" ]; then
		PACKAGE_CMD="$HOST_PACKAGE"
	else
		echo "ERROR: 'package' tool not found." >&2
		echo "       Looked for: $HOST_PACKAGE" >&2
		echo "       Set HOST_PACKAGE to the host-built Haiku 'package' binary." >&2
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
