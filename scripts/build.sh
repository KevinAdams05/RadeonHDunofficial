#!/usr/bin/env bash
#
# Build the RadeonHD (unofficial) accelerant and kernel driver by
# overlaying this repo's modified driver files onto a Haiku source tree
# and running jam.
#
# Usage:
#   scripts/build.sh [HAIKU_SRC]
#
# Arguments / environment:
#   HAIKU_SRC  Path to a Haiku source tree with cross-tools already
#              configured (defaults to $HOME/haiku-build/haiku, the path
#              the RadeonHD project uses on its build server).
#   ARCH       Target arch (defaults to x86_64).
#
# Outputs:
#   build/<ARCH>/radeon_hd               kernel driver binary
#   build/<ARCH>/radeon_hd.accelerant    accelerant binary
#
# These outputs feed scripts/package.sh, which wraps them in a .hpkg.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HAIKU_SRC="${1:-${HAIKU_SRC:-$HOME/haiku-build/haiku}}"
ARCH="${ARCH:-x86_64}"
OUTPUT_DIR="${REPO_ROOT}/build/${ARCH}"

if [ ! -d "$HAIKU_SRC" ]; then
	echo "ERROR: HAIKU_SRC='$HAIKU_SRC' does not exist." >&2
	echo "Pass a Haiku source tree path as the first argument, or set" >&2
	echo "the HAIKU_SRC environment variable." >&2
	exit 1
fi

GENERATED="$HAIKU_SRC/generated.$ARCH"
if [ ! -d "$GENERATED" ]; then
	echo "ERROR: '$GENERATED' does not exist." >&2
	echo "Configure the Haiku tree for $ARCH cross-tools first." >&2
	exit 1
fi

echo "==> Overlaying RadeonHD source onto $HAIKU_SRC"

# Headers
cp -v "$REPO_ROOT/headers/private/graphics/radeon_hd/"*.h \
	"$HAIKU_SRC/headers/private/graphics/radeon_hd/"

# Accelerant sources (.cpp + .h)
cp -v "$REPO_ROOT/src/add-ons/accelerants/radeon_hd/"* \
	"$HAIKU_SRC/src/add-ons/accelerants/radeon_hd/"

# Kernel driver sources
cp -v "$REPO_ROOT/src/add-ons/kernel/drivers/graphics/radeon_hd/"* \
	"$HAIKU_SRC/src/add-ons/kernel/drivers/graphics/radeon_hd/"

echo ""
echo "==> Building accelerant + kernel driver"
cd "$GENERATED"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
jam -q -j"$JOBS" radeon_hd.accelerant radeon_hd

echo ""
echo "==> Extracting binaries to $OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

OBJECTS="$GENERATED/objects/haiku/$ARCH/release"
ACCELERANT_OUT="$OBJECTS/add-ons/accelerants/radeon_hd/radeon_hd.accelerant"
KERNEL_OUT="$OBJECTS/add-ons/kernel/drivers/graphics/radeon_hd/radeon_hd"

if [ ! -f "$ACCELERANT_OUT" ]; then
	echo "ERROR: accelerant binary not found at $ACCELERANT_OUT" >&2
	exit 1
fi
if [ ! -f "$KERNEL_OUT" ]; then
	echo "ERROR: kernel driver binary not found at $KERNEL_OUT" >&2
	exit 1
fi

cp -v "$ACCELERANT_OUT" "$OUTPUT_DIR/radeon_hd.accelerant"
cp -v "$KERNEL_OUT" "$OUTPUT_DIR/radeon_hd"

echo ""
echo "==> Build complete:"
ls -la "$OUTPUT_DIR/"
