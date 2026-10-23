#!/bin/sh
# Build decon-backend.so (Weston 14 Direct-DECON) natively on the Samsung A30.
#
# The target must have gcc, wayland-scanner, wayland-protocols, the libinput/
# libudev/drm headers, and the weston-decon bundle installed at
# /opt/weston-decon (libweston-14, gl-renderer, libhybris + Mali EGL).
#
# Usage:
#   scripts/build_decon_native.sh [user@host] [output.so]
#
# Environment:
#   REMOTE_DIR   writable dir on the target            (default /home/user)
#   SSH_OPTS     extra ssh/scp options, e.g. -o ConnectTimeout=8
#   SSH_ASKPASS  password helper, if the target uses password auth
#
# The build is staged from this repository and shipped to the target, so the
# target does not need a git checkout.  Two headers are shimmed because the
# backend includes them under <libweston/...> and <hybris/hwcomposerwindow/...>
# paths that differ from their locations in the repository.
set -eu

REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET=${1:-user@172.16.42.1}
OUTPUT=${2:-$REPO_DIR/scratch/decon-backend-aarch64.so}
REMOTE_DIR=${REMOTE_DIR:-/home/user}

SSH=${SSH:-ssh}
SCP=${SCP:-scp}
SSH_OPTS=${SSH_OPTS:-}

STAGE=$(mktemp -d)
cleanup_stage() { rm -rf "$STAGE"; }
trap cleanup_stage EXIT

say() { printf '[build] %s\n' "$*"; }

say "staging sources from $REPO_DIR"

mkdir -p "$STAGE/decon-src/libweston" \
         "$STAGE/hybris-include/hybris/hwcomposerwindow"

cp "$REPO_DIR/src/weston/backend-decon/decon.c" \
   "$REPO_DIR/src/weston/backend-decon/decon_internal.h" \
   "$REPO_DIR/src/weston/backend-decon/backend-decon.h" \
   "$STAGE/decon-src/"
# decon_internal.h includes <libweston/backend-decon.h>
cp "$REPO_DIR/src/weston/backend-decon/backend-decon.h" \
   "$STAGE/decon-src/libweston/"
cp "$REPO_DIR/src/compositor/decon_backend.c" \
   "$REPO_DIR/src/compositor/decon_backend.h" \
   "$STAGE/decon-src/"

# Weston's libinput seat/device are private libweston sources; compile them in
# when BUILD_DECON_INPUT=1 so the backend gets its own input seat.
cp "$REPO_DIR/scratch/weston/libweston/libinput-seat.c" \
   "$REPO_DIR/scratch/weston/libweston/libinput-seat.h" \
   "$REPO_DIR/scratch/weston/libweston/libinput-device.c" \
   "$REPO_DIR/scratch/weston/libweston/libinput-device.h" \
   "$STAGE/decon-src/"

# Full weston tree for the internal headers (pixel-formats.h, color.h,
# backend.h, libweston-internal.h, renderer-gl/, shared/, ...).
cp -a "$REPO_DIR/scratch/weston" "$STAGE/weston-src"
# Public weston/pixman/wayland headers from the sysroot.
cp -a "$REPO_DIR/scratch/sysroot/usr/include" "$STAGE/sysroot-include"
# hybris + EGL/GLES headers, plus the hwcomposer window shim used by decon.c.
cp -a "$REPO_DIR/scratch/libhybris/hybris/include/." "$STAGE/hybris-include/"
cp "$REPO_DIR/scratch/libhybris/hybris/egl/platforms/hwcomposer/hwcomposer.h" \
   "$STAGE/hybris-include/hybris/hwcomposerwindow/hwcomposer.h"

# NOTE: tar the named members only.  Never tar "." here: extracting an entry
# for "." as root would rewrite the ownership/mode of the destination
# directory (this previously broke /tmp on the target).
tar -czf "$STAGE/decon-build.tar.gz" -C "$STAGE" \
    decon-src weston-src sysroot-include hybris-include

cat > "$STAGE/build-remote.sh" <<'REMOTE'
#!/bin/sh
set -eu
WORK=${WORK:-$HOME/decon-build}
TARBALL=${1:?usage: build-remote.sh <tarball>}

rm -rf "$WORK"
mkdir -p "$WORK"
tar xzf "$TARBALL" -C "$WORK"
touch "$WORK/weston-src/config.h"

wayland-scanner server-header \
	/usr/share/wayland-protocols/unstable/tablet/tablet-unstable-v2.xml \
	"$WORK/decon-src/tablet-unstable-v2-server-protocol.h"

# libweston-14 and libhybris live in the bundle; pixman has no .so symlink on
# this rootfs, so link the versioned object directly.
gcc -shared -fPIC -O2 -g \
	-I"$WORK/decon-src" \
	-I"$WORK/sysroot-include/libweston-14" \
	-I"$WORK/sysroot-include" \
	-I"$WORK/sysroot-include/pixman-1" \
	-I"$WORK/weston-src" \
	-I"$WORK/weston-src/libweston" \
	-I"$WORK/weston-src/shared" \
	-I"$WORK/hybris-include" \
	-I/usr/include/drm \
	-include "$WORK/weston-src/config.h" \
	-DENABLE_EGL=1 -DENABLE_DECON_GL=1 -DBUILD_DECON_INPUT=1 \
	-Wno-deprecated-declarations \
	"$WORK/decon-src/decon.c" \
	"$WORK/decon-src/decon_backend.c" \
	"$WORK/decon-src/libinput-seat.c" \
	"$WORK/decon-src/libinput-device.c" \
	-L/opt/weston-decon/usr/lib \
	-lweston-14 /usr/lib/libpixman-1.so.0 -lwayland-server -ludev -linput \
	-lhybris-hwcomposerwindow -lhybris-common \
	-Wl,-rpath,/opt/weston-decon/usr/lib \
	-o "$WORK/decon-backend.so"

readelf -h "$WORK/decon-backend.so" | grep -E 'Class|Machine'
printf 'libinput/udev undefined symbols: '
nm -D -u "$WORK/decon-backend.so" | grep -icE 'libinput|udev'
printf 'sha256: '
sha256sum "$WORK/decon-backend.so"
cp -f "$WORK/decon-backend.so" "$HOME/decon-backend.so"
REMOTE

say "shipping to $TARGET:$REMOTE_DIR"
# shellcheck disable=SC2086
$SCP $SSH_OPTS -O "$STAGE/decon-build.tar.gz" "$STAGE/build-remote.sh" \
    "$TARGET:$REMOTE_DIR/"

say "compiling on target"
# shellcheck disable=SC2086
$SSH $SSH_OPTS "$TARGET" \
    "sh '$REMOTE_DIR/build-remote.sh' '$REMOTE_DIR/decon-build.tar.gz'" | tee "$STAGE/build.log"

say "fetching artifact"
# shellcheck disable=SC2086
$SCP $SSH_OPTS -O "$TARGET:$REMOTE_DIR/decon-backend.so" "$OUTPUT"

say "wrote $OUTPUT ($(wc -c < "$OUTPUT") bytes)"
