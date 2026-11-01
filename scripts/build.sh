#!/usr/bin/env bash
#
# 编译 Rockchip vendor kernel 4.19 上的 USB 音频模块栈
#
#   KDIR=/path/to/linux-4.19.149 ./scripts/build.sh
#
# 可选环境变量：
#   ARCH            默认 arm64
#   CROSS_COMPILE   默认 aarch64-linux-gnu-
#   OUT             输出目录，默认 ./out
#   APPLY_PATCHES   设为 1 时先把 patches/*.patch 打进 KDIR
#
set -euo pipefail

KDIR=${KDIR:?用法: KDIR=/path/to/kernel-source ./scripts/build.sh}
ARCH=${ARCH:-arm64}
CROSS_COMPILE=${CROSS_COMPILE:-aarch64-linux-gnu-}
CC=${CC:-${CROSS_COMPILE}gcc}
OUT=${OUT:-$PWD/out}
APPLY_PATCHES=${APPLY_PATCHES:-0}

ROOT=$(cd "$(dirname "$0")/.." && pwd)

if [ ! -f "$KDIR/.config" ]; then
	echo "错误: $KDIR/.config 不存在，请先把运行内核的 .config 放进去并 make oldconfig" >&2
	exit 1
fi

if [ "$APPLY_PATCHES" = "1" ]; then
	for p in "$ROOT"/patches/*.patch; do
		echo "== 应用 $(basename "$p")"
		patch -d "$KDIR" -p1 < "$p"
	done
fi

build_dir() {
	local d=$1
	echo "== 编译 $d"
	make -C "$KDIR" M="$KDIR/$d" \
		ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" CC="$CC" modules
}

# 依赖顺序：core 先编，usb 后编
build_dir sound/core
build_dir sound/usb

mkdir -p "$OUT"
cp "$KDIR/sound/usb/snd-usb-audio.ko" \
   "$KDIR/sound/usb/snd-usbmidi-lib.ko" \
   "$KDIR/sound/core/snd-hwdep.ko" \
   "$KDIR/sound/core/snd-rawmidi.ko" \
   "$KDIR/sound/core/snd-seq-device.ko" \
   "$OUT/"

echo
echo "== 输出到 $OUT"
ls -l "$OUT"
echo
echo "== vermagic 校验（必须与 uname -r 完全一致）"
for f in "$OUT"/*.ko; do
	printf '%s: ' "$(basename "$f")"
	strings "$f" | grep -m1 'vermagic='
done
