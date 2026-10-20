#!/usr/bin/env python3
"""
mkbootimg.py - Standalone Android Boot Image Generator (v0, v1, v2)
Compatible with Python 3.8+ without external dependencies.
"""

import argparse
import hashlib
import os
import struct
import sys

BOOT_MAGIC = b"ANDROID!"
BOOT_MAGIC_SIZE = 8
BOOT_NAME_SIZE = 16
BOOT_ARGS_SIZE = 512
BOOT_EXTRA_ARGS_SIZE = 1024


def pad_file(f, page_size):
    pad = (page_size - (f.tell() & (page_size - 1))) & (page_size - 1)
    f.write(b"\0" * pad)


def main():
    parser = argparse.ArgumentParser(description="Create Android Boot Image")
    parser.add_argument("--kernel", required=True, help="Path to kernel image")
    parser.add_argument("--ramdisk", required=True, help="Path to ramdisk image")
    parser.add_argument("--second", default=None, help="Path to second stage image")
    parser.add_argument("--dtb", default=None, help="Path to DTB image")
    parser.add_argument("--recovery_dtbo", default=None, help="Path to recovery DTBO image")
    parser.add_argument("--cmdline", default="", help="Kernel command line")
    parser.add_argument("--base", default="0x10000000", help="Base address")
    parser.add_argument("--kernel_offset", default="0x00008000", help="Kernel offset")
    parser.add_argument("--ramdisk_offset", default="0x01000000", help="Ramdisk offset")
    parser.add_argument("--second_offset", default="0x00f00000", help="Second stage offset")
    parser.add_argument("--tags_offset", default="0x00000100", help="Tags offset")
    parser.add_argument("--dtb_offset", default="0x01f00000", help="DTB offset")
    parser.add_argument("--os_version", default="0", help="OS version")
    parser.add_argument("--os_patch_level", default="0", help="OS patch level")
    parser.add_argument("--board", default="", help="Board name")
    parser.add_argument("--pagesize", type=int, default=2048, choices=[2048, 4096], help="Page size")
    parser.add_argument("--header_version", type=int, default=0, choices=[0, 1, 2], help="Boot image header version")
    parser.add_argument("-o", "--output", required=True, help="Output boot image path")

    args = parser.parse_args()

    base = int(args.base, 0)
    kernel_addr = base + int(args.kernel_offset, 0)
    ramdisk_addr = base + int(args.ramdisk_offset, 0)
    second_addr = base + int(args.second_offset, 0)
    tags_addr = base + int(args.tags_offset, 0)
    dtb_addr = base + int(args.dtb_offset, 0)

    kernel_data = open(args.kernel, "rb").read()
    ramdisk_data = open(args.ramdisk, "rb").read() if args.ramdisk else b""
    second_data = open(args.second, "rb").read() if args.second else b""
    dtb_data = open(args.dtb, "rb").read() if args.dtb else b""
    recovery_dtbo_data = open(args.recovery_dtbo, "rb").read() if args.recovery_dtbo else b""

    # Calculate SHA1 hash of kernel + ramdisk + second + recovery_dtbo + dtb
    sha1 = hashlib.sha1()
    sha1.update(kernel_data)
    sha1.update(struct.pack("<I", len(kernel_data)))
    sha1.update(ramdisk_data)
    sha1.update(struct.pack("<I", len(ramdisk_data)))
    sha1.update(second_data)
    sha1.update(struct.pack("<I", len(second_data)))
    if args.header_version >= 1:
        sha1.update(recovery_dtbo_data)
        sha1.update(struct.pack("<I", len(recovery_dtbo_data)))
    if args.header_version >= 2:
        sha1.update(dtb_data)
        sha1.update(struct.pack("<I", len(dtb_data)))
    id_hash = sha1.digest()

    cmdline_bytes = args.cmdline.encode("utf-8")
    cmdline_part1 = cmdline_bytes[:BOOT_ARGS_SIZE]
    cmdline_part2 = cmdline_bytes[BOOT_ARGS_SIZE:BOOT_ARGS_SIZE + BOOT_EXTRA_ARGS_SIZE]

    board_bytes = args.board.encode("utf-8")[:BOOT_NAME_SIZE]

    os_version = int(args.os_version, 0)
    os_patch_level = int(args.os_patch_level, 0)
    os_version_field = (os_version << 11) | os_patch_level

    # Header v0 structure: 1632 bytes
    header = bytearray()
    header.extend(BOOT_MAGIC)
    header.extend(struct.pack("<I", len(kernel_data)))
    header.extend(struct.pack("<I", kernel_addr))
    header.extend(struct.pack("<I", len(ramdisk_data)))
    header.extend(struct.pack("<I", ramdisk_addr))
    header.extend(struct.pack("<I", len(second_data)))
    header.extend(struct.pack("<I", second_addr))
    header.extend(struct.pack("<I", tags_addr))
    header.extend(struct.pack("<I", args.pagesize))
    header.extend(struct.pack("<I", args.header_version))
    header.extend(struct.pack("<I", os_version_field))
    header.extend(board_bytes.ljust(BOOT_NAME_SIZE, b"\0"))
    header.extend(cmdline_part1.ljust(BOOT_ARGS_SIZE, b"\0"))
    header.extend(id_hash.ljust(32, b"\0"))
    header.extend(cmdline_part2.ljust(BOOT_EXTRA_ARGS_SIZE, b"\0"))

    if args.header_version >= 1:
        header.extend(struct.pack("<I", len(recovery_dtbo_data)))
        header.extend(struct.pack("<Q", 0))  # offset will be computed
        header.extend(struct.pack("<I", len(header) + 16))  # header size
    if args.header_version >= 2:
        header.extend(struct.pack("<I", len(dtb_data)))
        header.extend(struct.pack("<Q", dtb_addr))

    with open(args.output, "wb") as f:
        f.write(header)
        pad_file(f, args.pagesize)

        f.write(kernel_data)
        pad_file(f, args.pagesize)

        if len(ramdisk_data) > 0:
            f.write(ramdisk_data)
            pad_file(f, args.pagesize)

        if len(second_data) > 0:
            f.write(second_data)
            pad_file(f, args.pagesize)

        if args.header_version >= 1 and len(recovery_dtbo_data) > 0:
            f.write(recovery_dtbo_data)
            pad_file(f, args.pagesize)

        if args.header_version >= 2 and len(dtb_data) > 0:
            f.write(dtb_data)
            pad_file(f, args.pagesize)

    print(f"[+] Successfully wrote Android boot image: {args.output}")
    print(f"    Kernel Size: {len(kernel_data):,} bytes (addr {hex(kernel_addr)})")
    print(f"    Ramdisk Size: {len(ramdisk_data):,} bytes (addr {hex(ramdisk_addr)})")
    print(f"    Page Size: {args.pagesize}")
    print(f"    Header Version: {args.header_version}")
    print(f"    Total Image Size: {os.path.getsize(args.output):,} bytes")


if __name__ == "__main__":
    main()
