#!/bin/sh
#
# 在目标设备（eCube）上安装音频模块
#   用法：把仓库拷到设备上，进入仓库根目录执行
#     ./scripts/install.sh
#
set -e

REV=$(uname -r)
DST=/lib/modules/$REV/kernel/sound

if [ ! -d "$DST" ]; then
	echo "错误：$DST 不存在，内核版本 $REV 与模块不匹配？"
	exit 1
fi

install -m 644 ko/snd-hwdep.ko       "$DST/core/"
install -m 644 ko/snd-rawmidi.ko     "$DST/core/"
install -m 644 ko/snd-seq-device.ko  "$DST/core/"
install -m 644 ko/snd-usbmidi-lib.ko "$DST/usb/"
install -m 644 ko/snd-usb-audio.ko   "$DST/usb/"

depmod -a "$REV"

echo "已安装到 $DST"
echo "重新加载：modprobe -r snd-usb-audio; modprobe snd-usb-audio"
echo "或直接重启设备。"
