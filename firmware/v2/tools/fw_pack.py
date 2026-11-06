#!/usr/bin/env python3
"""fw_pack.py IN.bin OUT.bin - fill size and CRC-32 of the image information block
(see app/fwu.h: img_info_t at offset 0x200) so the bootloader and the panel accept it.
Prints the values the panel needs for HR_FWU_SIZE / HR_FWU_CRC."""
import struct, sys, zlib
OFS, MAGIC, HW = 0x200, 0x56524443, 0x0200
img = bytearray(open(sys.argv[1], "rb").read())
img += b"\xff" * ((-len(img)) % 8)
magic, hw, ver, _, _ = struct.unpack_from("<IHHII", img, OFS)
assert magic == MAGIC and hw == HW, "image information block not found at 0x200"
struct.pack_into("<I", img, OFS + 8, len(img))
cpos = OFS + 12
crc = zlib.crc32(bytes(img[:cpos]) + bytes(img[cpos + 4:])) & 0xFFFFFFFF
struct.pack_into("<I", img, cpos, crc)
open(sys.argv[2], "wb").write(img)
print(f"version {ver >> 8}.{ver & 255}  size {len(img)}  info-crc 0x{crc:08x}  "
      f"file-crc 0x{zlib.crc32(bytes(img)) & 0xFFFFFFFF:08x} (HR_FWU_CRC)")
