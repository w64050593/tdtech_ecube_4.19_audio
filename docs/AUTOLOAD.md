# USB 音频模块 开机自动加载配置与使用说明

本文说明如何让本项目编译的 `snd-usb-audio` 模块栈在 **eCube（Debian 10 buster，systemd）**
开机时自动加载，并给出验证、排错与卸载方法。

---

## 1. 模块清单与依赖关系

| 模块 | 路径 | 说明 |
|---|---|---|
| `snd-usb-audio.ko` | `kernel/sound/usb/` | USB 音频主驱动 |
| `snd-usbmidi-lib.ko` | `kernel/sound/usb/` | USB MIDI 支持库 |
| `snd-rawmidi.ko` | `kernel/sound/core/` | 原始 MIDI 核心 |
| `snd-seq-device.ko` | `kernel/sound/core/` | 音序器设备 |
| `snd-hwdep.ko` | `kernel/sound/core/` | 硬件依赖层 |

依赖链（`modprobe` 会按 `modules.dep` 自动解析，**只需指定顶层模块**）：

```
snd-usb-audio
 ├── snd-usbmidi-lib → snd-rawmidi → snd-seq-device
 └── snd-hwdep
```

> 运行内核里 `snd-pcm`、`snd-timer`、`snd` 等是 builtin，不在本清单内。

---

## 2. 前提条件

1. 5 个 `.ko` 已放到正确目录：
   ```sh
   /lib/modules/$(uname -r)/kernel/sound/core/{snd-hwdep,snd-rawmidi,snd-seq-device}.ko
   /lib/modules/$(uname -r)/kernel/sound/usb/{snd-usb-audio,snd-usbmidi-lib}.ko
   ```
   可用仓库里的 `scripts/install.sh` 完成。
2. 已执行 `depmod -a`，否则找不到依赖信息。
3. 模块 vermagic 与 `uname -r` 完全一致（`4.19.149-00010-g1978b42`）。

检查：

```sh
uname -r
modinfo snd-usb-audio | head -5
```

---

## 3. 配置方式

### 3.1 推荐：`/etc/modules-load.d/`（systemd）

新建 `/etc/modules-load.d/ecube-usb-audio.conf`：

```ini
# 开机自动加载 USB 音频驱动
snd-usb-audio
```

只需写顶层模块名，依赖由 `modprobe` 自动加载。

仓库里已提供该文件，直接执行安装脚本即可：

```sh
sudo ./deploy/install-autoload.sh
```

脚本会把 `deploy/etc/` 下的两个配置文件复制到 `/etc/`，然后重启
`systemd-modules-load` 服务。

> 注意：`modules-load.d` 目录下的文件按**文件名字母顺序**依次加载；
> 本项目文件名 `ecube-usb-audio.conf` 不与其它配置冲突。

### 3.2 Debian 兼容方式：`/etc/modules`

Debian 10 上 `/etc/modules-load.d/modules.conf` 是指向 `/etc/modules` 的软链接。
也可以直接在 `/etc/modules` 末尾追加一行：

```sh
echo 'snd-usb-audio' >> /etc/modules
```

两种方式效果相同，**不要重复配置**（同一条目写两次虽然无害，但容易混淆）。

### 3.3 可选：`/etc/modprobe.d/` 调整参数

仓库提供 `/etc/modprobe.d/ecube-usb-audio.conf`，默认全部注释。按需启用：

- 强制依赖顺序（依赖信息缺失时）：
  ```ini
  softdep snd-usb-audio pre: snd-hwdep snd-usbmidi-lib snd-rawmidi snd-seq-device
  ```
- 固定声卡编号：
  ```ini
  options snd-usb-audio index=1
  ```
  启用后 USB 声卡固定为 `card 1`；若该槽已被占用会加载失败，先看
  `/proc/asound/cards` 再决定。
- 绑定指定 VID/PID（默认 `id_table` 已包含 `0c76:1203`，一般不需要）：
  ```ini
  options snd-usb-audio vid=0x0c76 pid=0x1203
  ```

修改 `modprobe.d` 后建议 `depmod -a` 并重新加载模块（或重启）。

### 3.4 udev 自动匹配（内核默认行为）

`snd-usb-audio` 自身带有 USB `id_table`，插入设备时 udev 会依据 USB modalias
自动 `modprobe snd-usb-audio`。因此**即使不写任何配置，插上声卡通常也会自动加载**。

本项目的配置作用是让模块在**开机时、设备尚未枚举前**就确定加载，
避免依赖 udev 时序（尤其在精简系统、udev 规则被裁剪的情况下）。

不需要自动加载时，可反向屏蔽：

```sh
# /etc/modprobe.d/blacklist.conf
blacklist snd-usb-audio
```

---

## 4. 生效与验证

### 4.1 不重启，立即验证

```sh
# 手动加载（等价于开机时 modules-load 做的事）
modprobe snd-usb-audio

lsmod | grep -i snd
cat /proc/asound/cards
dmesg | tail -n 20
```

期望输出：

```
snd_usb_audio         200704  1
snd_hwdep              20480  1 snd_usb_audio
snd_usbmidi_lib        32768  1 snd_usb_audio
snd_rawmidi            32768  1 snd_usbmidi_lib
snd_seq_device         16384  1 snd_rawmidi

 1 [Device]: USB-Audio - USB PnP Audio Device
```

### 4.2 重启验证

```sh
sudo reboot
# 重新登录后
lsmod | grep -i snd
cat /proc/asound/cards
```

若 `snd_usb_audio` 已在重启后自动出现、`/proc/asound/cards` 里有 `USB PnP Audio Device`，
说明自动加载配置生效。

### 4.3 检查 systemd 服务状态

```sh
systemctl status systemd-modules-load --no-pager
journalctl -b -u systemd-modules-load --no-pager | tail -n 20
```

日志里应有类似 `Loading module: snd-usb-audio` 的记录。若文件写错（语法/模块名拼错），
这里会报 `Failed to find module 'xxx'`。

---

## 5. 常用操作

```sh
# 查看是否加载
lsmod | grep snd

# 卸载（按依赖逆序；一般直接用 -r 让内核解析）
modprobe -r snd-usb-audio
modprobe -r snd-usbmidi-lib snd-hwdep snd-rawmidi snd-seq-device

# 重新加载
modprobe -r snd-usb-audio && modprobe snd-usb-audio

# 查看模块参数与版本
modinfo snd-usb-audio | head -10

# 播放测试
amixer -c 1 sset 'Speaker' 100%
aplay -D plughw:1,0 test.wav

# 录音测试
arecord -D plughw:1,0 -f S16_LE -r 48000 -c 1 -d 3 rec.wav
```

> `modprobe -r snd-usb-audio` 可能因声卡正被占用（引用计数 > 0）而失败，
> 先用 `lsof /dev/snd/*` 或关闭播放器，必要时可直接重启。

---

## 6. 常见问题

| 现象 | 可能原因 | 处理 |
|---|---|---|
| 重启后 `lsmod` 里没有 `snd_usb_audio` | 配置文件模块名拼错 / 文件未生效 | `journalctl -b -u systemd-modules-load` 查看报错 |
| `modprobe: FATAL: Module snd-usb-audio not found` | `.ko` 未安装或未 `depmod` | 执行 `scripts/install.sh`，确认 `depmod -a` |
| `invalid module format` | vermagic 不匹配 | 确认 `uname -r` 与编译时 `CONFIG_LOCALVERSION` 一致 |
| `Unknown symbol snd_rawmidi_xxx` | 依赖模块缺失 | 补齐 5 个 `.ko` 后 `depmod -a` |
| 开机后 card 号变化（1 变 2 等） | 设备枚举顺序不固定 | 用 `options snd-usb-audio index=1` 固定编号 |
| `options ... index=1` 后加载失败 | 1 号槽被其它声卡占用 | 换一个空闲编号，或去掉 index 参数 |
| 插入声卡无反应 | udev 未匹配 / 驱动未编译进 id_table | `dmesg` 看是否有 `usb 1-1: new full-speed USB device` |
| 开机有 5~10 秒延迟 | 模块加载等待设备枚举 | 正常现象；可改为纯 udev 触发、不在开机预加载 |

---

## 7. 关闭自动加载

```sh
sudo rm -f /etc/modules-load.d/ecube-usb-audio.conf
sudo sed -i '/snd-usb-audio/d' /etc/modules
sudo modprobe -r snd-usb-audio
```

如需连插卡自动加载也禁用：

```sh
echo 'blacklist snd-usb-audio' > /etc/modprobe.d/blacklist-usb-audio.conf
```

---

## 8. 说明

- 不要用 `/etc/rc.local` 或自定义脚本 `insmod` 来“自动加载”：
  那样不会解析依赖、不会更新 `/proc/asound`，且容易在开机早期失败。
- `modules-load.d` 在开机早期由 systemd 执行，适合本项目的“确定性加载”需求。
- 配置只影响“是否加载”，不改变驱动行为；若播放/录音异常，请回到
  `README.md` 的 ABI 对齐部分排查。
