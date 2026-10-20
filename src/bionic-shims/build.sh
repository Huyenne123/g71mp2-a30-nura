#!/bin/sh
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="${1:-$DIR/bin}"
mkdir -p "$OUT_DIR"

echo "[*] Compiling Bionic shims for aarch64..."

clang -target aarch64-linux-gnu -shared -fPIC -nostdlib -fuse-ld=lld \
    "$DIR/nativewindow_stub.c" -o "$OUT_DIR/libnativewindow.so"

clang -target aarch64-linux-gnu -shared -fPIC -nostdlib -fuse-ld=lld \
    "$DIR/hardware_stub.c" -o "$OUT_DIR/libhardware.so"

clang -target aarch64-linux-gnu -shared -fPIC -nostdlib -fuse-ld=lld \
    "$DIR/cutils_stub.c" -o "$OUT_DIR/libcutils.so"

clang++ -target aarch64-linux-gnu -shared -fPIC -nostdlib -fuse-ld=lld \
    "$DIR/utils_stub.cpp" -o "$OUT_DIR/libutils.so"

clang -target aarch64-linux-gnu -shared -fPIC -nostdlib -fuse-ld=lld \
    "$DIR/sync_stub.c" -o "$OUT_DIR/libsync.so"

echo "[+] Successfully built all Bionic shims in $OUT_DIR:"
ls -lh "$OUT_DIR"
