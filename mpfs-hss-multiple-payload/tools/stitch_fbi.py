#!/usr/bin/env python3
import sys

SLOT_SIZE = 0x10000  # 64 KiB

def pad_and_append(out_f, in_path):
    with open(in_path, "rb") as f:
        data = f.read()
    if len(data) > SLOT_SIZE:
        raise ValueError(f"{in_path} size ({len(data)}) exceeds slot size ({SLOT_SIZE})")
    out_f.write(data)
    out_f.write(b'\x00' * (SLOT_SIZE - len(data)))

with open("multi_app.bin", "wb") as out:
    pad_and_append(out, "hart1-cli/build/ddr/mpfs-baremetal.bin")
    pad_and_append(out, "hart2-math/hart2-math.bin")
    pad_and_append(out, "hart3-memstress/hart3-memstress.bin")
    pad_and_append(out, "hart4-watchdog/hart4-watchdog.bin")

print("[OK] Created multi_app.bin (262,144 bytes / 256 KiB)")