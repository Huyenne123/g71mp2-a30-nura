#!/usr/bin/env python3
"""
unpackbootimg.py - Standalone Android Boot Image Extractor (v0, v1, v2)
Compatible with Python 3.8+ without external dependencies.
"""

import argparse
import os
import struct
import sys

BOOT_MAGIC = b"ANDROID!"


def main():
    parser = argparse.ArgumentParser(description="Unpack Android Boot Image")
    parser.add_argument("-i", "--input", required=True, help="Input boot image path")
    parser.add_argument("-o", "--output", default="./extracted_boot", help="Output directory")

    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)

    with open(args.input, "rb") as f:
        magic = f.read(8)
        if magic != BOOT_MAGIC:
            print(f"[-] Error: Invalid boot magic: {magic}")
            sys.exit(1)

        raw_header = f.read(1624)
        kernel_size, kernel_addr, ramdisk_size, ramdisk_addr, second_size, second_addr, tags_addr, page_size, header_version, os_version = struct.unpack('<IIIIIIIIII', raw_header[0:40])
        name = raw_header[40:56].split(b'\0')[0].decode('utf-8', errors='ignore')
        cmdline = raw_header[56:568].split(b'\0')[0].decode('utf-8', errors='ignore')
        id_hash = raw_header[568:600]
        extra_cmdline = raw_header[600:1624].split(b'\0')[0].decode('utf-8', errors='ignore')

        print("=== Android Boot Image Header ===")
        print(f"Header Version: {header_version}")
        print(f"Page Size:      {page_size}")
        print(f"Kernel Size:    {kernel_size:,} bytes (addr {hex(kernel_addr)})")
        print(f"Ramdisk Size:   {ramdisk_size:,} bytes (addr {hex(ramdisk_addr)})")
        print(f"Second Size:    {second_size:,} bytes (addr {hex(second_addr)})")
        print(f"Tags Addr:      {hex(tags_addr)}")
        print(f"OS Version:     {hex(os_version)}")
        print(f"Board Name:     '{name}'")
        print(f"Cmdline:        '{cmdline}'")
        if extra_cmdline:
            print(f"Extra Cmdline:  '{extra_cmdline}'")

        # Save header info to text file
        with open(os.path.join(args.output, "bootimg.info"), "w") as info:
            info.write(f"header_version={header_version}\n")
            info.write(f"page_size={page_size}\n")
            info.write(f"kernel_size={kernel_size}\n")
            info.write(f"kernel_addr={hex(kernel_addr)}\n")
            info.write(f"ramdisk_size={ramdisk_size}\n")
            info.write(f"ramdisk_addr={hex(ramdisk_addr)}\n")
            info.write(f"tags_addr={hex(tags_addr)}\n")
            info.write(f"cmdline={cmdline}\n")
            info.write(f"extra_cmdline={extra_cmdline}\n")

        # Calculate offsets based on page alignment
        def page_align(val):
            return (val + page_size - 1) // page_size * page_size

        header_pages = 1
        kernel_offset = header_pages * page_size
        ramdisk_offset = kernel_offset + page_align(kernel_size)
        second_offset = ramdisk_offset + page_align(ramdisk_size)

        # Extract kernel
        f.seek(kernel_offset)
        kernel_data = f.read(kernel_size)
        kernel_path = os.path.join(args.output, "kernel")
        with open(kernel_path, "wb") as kf:
            kf.write(kernel_data)
        print(f"[+] Extracted kernel: {kernel_path} ({len(kernel_data):,} bytes)")

        # Extract ramdisk
        if ramdisk_size > 0:
            f.seek(ramdisk_offset)
            ramdisk_data = f.read(ramdisk_size)
            ramdisk_path = os.path.join(args.output, "ramdisk.cpio.gz")
            with open(ramdisk_path, "wb") as rf:
                rf.write(ramdisk_data)
            print(f"[+] Extracted ramdisk: {ramdisk_path} ({len(ramdisk_data):,} bytes)")


if __name__ == "__main__":
    main()
