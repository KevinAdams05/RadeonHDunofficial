#!/usr/bin/env python3
import mmap, os, struct, sys, glob

# Find first radeon
pci = None
for d in glob.glob("/sys/bus/pci/devices/*"):
    try:
        v = open(d+"/vendor").read().strip()
        c = open(d+"/class").read().strip()
        if v == "0x1002" and c.startswith("0x0300"):
            pci = d; break
    except: pass
if not pci: sys.exit("No radeon found")
print(f"# device: {os.path.basename(pci)}", file=sys.stderr)

# BAR2 = registers MMIO
fd = os.open(pci+"/resource2", os.O_RDONLY|os.O_SYNC)
sz = os.fstat(fd).st_size
mm = mmap.mmap(fd, sz, mmap.MAP_SHARED, mmap.PROT_READ)

def r32(off):
    return struct.unpack("<I", mm[off:off+4])[0]

groups = [
    ("CRTC0 surface programming", [
        0x6800, 0x6804, 0x6808, 0x680c, 0x6810, 0x6814, 0x6818, 0x681c,
        0x6820, 0x6824, 0x6828, 0x6844, 0x6d70, 0x6d74,
    ]),
    ("CRTC0 LB / priority / watermarks", [
        0x6b0c, 0x6b18, 0x6b1c, 0x6cc8, 0x6ccc,
    ]),
    ("MC + HDP framebuffer mapping", [
        0x2024, 0x2c04, 0x2c08, 0x2c0c, 0x2f48, 0x0bd4,
        0x6904, 0x6908, 0x690c,
    ]),
    ("DMIF / arbitration (DCE 4-style)", [
        0x0bf0, 0x0bf4, 0x0ca0,
    ]),
    ("MC channel map / pixel clock", [
        0x2004, 0x0418, 0x0488,
    ]),
    ("MC_HUB / display priority", [
        0x2658, 0x265c, 0x2660, 0x2664, 0x2668, 0x266c, 0x2670, 0x2674,
    ]),
    ("HDP non-surface info", [
        0x2c14, 0x2c18, 0x2c1c, 0x2c20, 0x2c24,
    ]),
]

for label, regs in groups:
    print(f"\n=== {label} ===")
    for off in regs:
        try:
            print(f"  0x{off:04x} : 0x{r32(off):08x}")
        except Exception as e:
            print(f"  0x{off:04x} : ERROR {e}")
