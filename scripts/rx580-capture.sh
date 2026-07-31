#!/bin/sh
# RadeonHD (unofficial) 0.6.5 — RX 580 / Polaris 10 bring-up capture (Haiku).
#
# Run in Terminal after booting Haiku with the 0.6.5 .hpkg installed and the
# RX 580 as the display card:
#     sh rx580-capture.sh
#
# Produces ONE file next to the script (or /boot/home): rx580-capture_out.txt
# Send that file back. Read-only — it changes nothing on the system.
#
# If the screen is black/garbled (driver didn't bring up the display) the
# system is often still running: connect over SSH, or run this script blind
# and retrieve the file — the syslog it captures is exactly what we need.

SCRIPT_DIR=`dirname "$0" 2>/dev/null`; [ -z "$SCRIPT_DIR" ] && SCRIPT_DIR=.
OUT="$SCRIPT_DIR/rx580-capture_out.txt"
if ! ( : > "$OUT" ) 2>/dev/null; then OUT="/boot/home/rx580-capture_out.txt"; : > "$OUT"; fi

sec()  { echo ""; echo "==================== $1 ===================="; } >> "$OUT"
run()  { sec "$1"; shift; if command -v "$1" >/dev/null 2>&1; then echo "\$ $*" >>"$OUT"; "$@" >>"$OUT" 2>&1; else echo "(command '$1' not found)" >>"$OUT"; fi; }

echo "RX 580 capture -> $OUT"

sec "META"
{ echo "date : `date 2>/dev/null`"; echo "user : `whoami 2>/dev/null`"; } >> "$OUT"

run "uname -a (Haiku revision)" uname -a

sec "RadeonHD package installed? (expect radeon_hd_unofficial 0.6.5)"
ls -la /boot/home/config/packages/*radeon* /boot/system/packages/*radeon* 2>/dev/null >> "$OUT"
ls -la /boot/home/config/non-packaged/add-ons/accelerants/*radeon* 2>/dev/null >> "$OUT"

# THE key question: did radeon_hd claim the RX 580 (1002:67df), or did it
# fall back to VESA/framebuffer?
run "listdev (look for AMD 1002 / device 67df + the display controller)" listdev

sec "/dev/graphics (radeon_hd node = driver active; vesa/framebuffer = fallback)"
ls -lR /dev/graphics 2>/dev/null >> "$OUT"

run "screenmode (current resolution/depth)" screenmode
run "screenmode --list (modes the accelerant offers)" screenmode --list

sec "SYSLOG - radeon / DCE / Polaris / connector / EDID highlights"
grep -iE "radeon|polaris|CardDetect|init_hardware|no supported|\\bDCE\\b|dce1[012]|atom|connector|encoder|\\bEDID\\b|\\bCRTC\\b|\\bPLL\\b|displayport|\\bDP\\b|hdmi|dvi|framebuffer|vesa|accelerant|mode set|detect_displays" /var/log/syslog >> "$OUT" 2>&1

sec "SYSLOG - FULL (current boot)"
cat /var/log/syslog >> "$OUT" 2>&1

sec "SYSLOG - previous boot (if present)"
cat /var/log/previous_syslog >> "$OUT" 2>&1

sec "END"
echo "Done. Send back the file:  $OUT"
echo "Done -> $OUT"
