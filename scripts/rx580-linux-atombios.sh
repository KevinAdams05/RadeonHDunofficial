#!/bin/sh
# OPTIONAL deep-analysis capture on LINUX — only if the tester can boot Linux
# with the RX 580 installed. Grabs the card's AtomBIOS ROM + a register dump
# for offline comparison (same artifacts we captured for Bonaire).
#
# Run with root:   sudo sh rx580-linux-atombios.sh
# Keep dumpregs.py in the same folder as this script.
# Send back the whole 'rx580-linux' folder it creates.

DIR=`dirname "$0" 2>/dev/null`; [ -z "$DIR" ] && DIR=.
OUT="$DIR/rx580-linux"
mkdir -p "$OUT"

# locate the AMD (vendor 0x1002) VGA-class device
DEV=""
for d in /sys/bus/pci/devices/*; do
	v=`cat "$d/vendor" 2>/dev/null`
	c=`cat "$d/class" 2>/dev/null`
	if [ "$v" = "0x1002" ] && echo "$c" | grep -q "^0x0300"; then DEV="$d"; break; fi
done
if [ -z "$DEV" ]; then echo "No AMD VGA device (0x1002, class 0x0300) found"; exit 1; fi

ADDR=`basename "$DEV"`
DID=`cat "$DEV/device" 2>/dev/null | sed 's/0x//'`
echo "AMD VGA at $ADDR  (1002:$DID)"

# lspci detail
lspci -vnn -s "$ADDR" > "$OUT/lspci_1002_${DID}.txt" 2>&1 || true

# AtomBIOS / VBIOS ROM (must be enabled for reading, then disabled)
echo 1 > "$DEV/rom" 2>/dev/null || true
cat "$DEV/rom" > "$OUT/radeon_hd_bios_1002_${DID}_0.bin" 2>/dev/null \
	&& echo "ROM dumped: radeon_hd_bios_1002_${DID}_0.bin" \
	|| echo "(ROM read failed — may need the card to be the active/primary GPU)"
echo 0 > "$DEV/rom" 2>/dev/null || true
ls -la "$OUT/radeon_hd_bios_1002_${DID}_0.bin" 2>/dev/null

# register dump via the repo's dumpregs.py (mmaps BAR2)
if [ -f "$DIR/dumpregs.py" ]; then
	python3 "$DIR/dumpregs.py" > "$OUT/dumpregs_1002_${DID}.txt" 2>&1 \
		&& echo "registers dumped: dumpregs_1002_${DID}.txt" \
		|| echo "(dumpregs.py failed)"
else
	echo "(dumpregs.py not found alongside this script — skipping register dump)"
fi

echo ""
echo "Done. Send back the whole folder: $OUT"
