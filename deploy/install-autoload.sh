#!/bin/sh
#
# 在目标设备上配置 USB 音频模块开机自动加载
# 用法（在设备上、仓库根目录执行）：
#   ./deploy/install-autoload.sh
#
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)

install -D -m 644 "$ROOT/deploy/etc/modules-load.d/ecube-usb-audio.conf" \
	/etc/modules-load.d/ecube-usb-audio.conf
install -D -m 644 "$ROOT/deploy/etc/modprobe.d/ecube-usb-audio.conf" \
	/etc/modprobe.d/ecube-usb-audio.conf

if command -v systemctl >/dev/null 2>&1; then
	systemctl restart systemd-modules-load 2>/dev/null || true
fi

echo "已配置开机自动加载："
echo "  /etc/modules-load.d/ecube-usb-audio.conf"
echo "  /etc/modprobe.d/ecube-usb-audio.conf"
echo
echo "当前状态："
lsmod | grep -i 'snd_usb_audio' || echo "  snd_usb_audio 尚未加载"
echo
echo "重启后验证： cat /proc/asound/cards"
