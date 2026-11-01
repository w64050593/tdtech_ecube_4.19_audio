# TD Tech eCube（PX30 / Rockchip kernel 4.19）USB 声卡驱动编译项目

在 **TD Tech eCube**（Rockchip PX30 平台，运行内核 `4.19.149-00010-g1978b42`，aarch64，Debian 10 buster）上
让免驱 USB 声卡 `0c76:1203`（JMTek / Solid State System "USB PnP Audio Device"）正常工作的
**snd-usb-audio 编译工程**。

包含：

- 可直接安装的 5 个 `.ko` 模块（`ko/`）
- 与运行内核 ABI 对齐后的头文件（`headers/`）
- 基于主线 `linux-4.19.149` 的 4 个补丁（`patches/`）
- 运行内核 ABI 探针模块（`tools/abi-probe/`）
- 交叉编译 / 安装脚本（`scripts/`）

最终结果：`/proc/asound/cards` 出现 `card 1 [Device]: USB-Audio - USB PnP Audio Device`，
`aplay` 播放与 `arecord` 录音均正常，`dmesg` 无 `Oops / WARN / spinlock bad magic`。

---

## 1. 目录结构

```
tdtech_ecube_4.19_audio/
├── ko/                         编译好的模块（可直接部署）
│   ├── snd-usb-audio.ko
│   ├── snd-usbmidi-lib.ko
│   ├── snd-rawmidi.ko
│   ├── snd-hwdep.ko
│   └── snd-seq-device.ko
├── headers/                    与运行内核 ABI 对齐后的头文件（最终版）
│   ├── include/linux/pm_qos.h
│   ├── include/linux/rwsem.h
│   ├── include/sound/core.h
│   └── include/sound/pcm.h
├── patches/                    基于 vanilla linux-4.19.149 的补丁
│   ├── 0001-pm_qos-add-vendor-fields.patch
│   ├── 0002-rwsem-add-m_count.patch
│   ├── 0003-snd-card-add-offline-fields.patch
│   └── 0004-pcm-sync-vendor-layout.patch
├── tools/abi-probe/            运行内核 ABI 探针（insmod 打印真实布局）
├── scripts/build.sh            交叉编译脚本
└── scripts/install.sh          目标设备安装脚本
```

---

## 2. 快速安装（只需要成品模块）

前提：目标设备内核版本必须是 `4.19.149-00010-g1978b42`（模块 vermagic 必须完全一致）。

```sh
# 在 eCube 上执行
uname -r
# 4.19.149-00010-g1978b42

cp ko/snd-hwdep.ko       /lib/modules/$(uname -r)/kernel/sound/core/
cp ko/snd-rawmidi.ko     /lib/modules/$(uname -r)/kernel/sound/core/
cp ko/snd-seq-device.ko  /lib/modules/$(uname -r)/kernel/sound/core/
cp ko/snd-usbmidi-lib.ko /lib/modules/$(uname -r)/kernel/sound/usb/
cp ko/snd-usb-audio.ko   /lib/modules/$(uname -r)/kernel/sound/usb/
depmod -a
reboot
```

或者直接把仓库拷到设备上，执行 `./scripts/install.sh`。

重启后验证：

```sh
cat /proc/asound/cards
#  0 [siliconlabsfxs]: siliconlabs_fxs - siliconlabs,fxs
#  1 [Device        ]: USB-Audio - USB PnP Audio Device
#                      Solid State System Co.,Ltd. USB PnP Audio Device at usb-ff300000.usb-1

aplay -l                       # 能看到 card 1, device 0
aplay -D plughw:1,0 test.wav   # 播放
arecord -D plughw:1,0 -f S16_LE -r 48000 -c 1 -d 3 rec.wav   # 录音
```

> `aplay` 只支持 WAV。真实歌曲需先转换：
> `ffmpeg -i song.mp3 -ac 1 -ar 48000 -sample_fmt s16 song.wav`

---

## 3. 问题现象（修复前的原始故障）

加载自己编译的 `snd-usb-audio.ko` 后，USB 声卡一插入就触发内核崩溃 / 告警：

```
Internal error: Oops: 96000005 [#1] SMP
...
 kobject_uevent(...)
 device_add(...)
 snd_card_register(...)
 usb_audio_probe(...) [snd_usb_audio]
---[ end trace ... ]---
```

还会出现：

- `spinlock bad magic`
- `kobject_get()` / `refcount_t` 相关 WARN
- `Unable to handle kernel NULL pointer dereference`，调用链落在 `snd_usb_pcm_open`
- `/proc/asound/cards` 里始终只有 `card 0`（蓝牙 SCO 声卡），USB 声卡注册失败

这些现象**不是驱动逻辑错误**，而是我们编译模块时使用的结构体布局与设备上运行的内核不一致。

---

## 4. 根因：vendor 内核与主线头文件的 ABI 不一致

设备跑的是 **Rockchip 厂商魔改的 4.19 内核**（`kernel-develop-4.19`），而我们交叉编译时用的是
**主线 vanilla `linux-4.19.149` 源码 + 厂商 `.config`**。
两者的 `.config` 一致（所以 vermagic 相同、模块能加载），但**头文件里结构体字段不一样**。

模块按自己（主线）的偏移去读写内核（厂商）分配的对象，结果是越界写、字段错位、函数指针乱跳。

### 4.1 实测差异表（全部来自运行内核探针）

| 结构体 | vanilla 4.19.149 | 运行内核（Rockchip vendor） | 差异 | 造成的后果 |
|---|---|---|---|---|
| `struct rw_semaphore` | 64 | **72** | +8（多了 `long m_count`） | `snd_card.controls_rwsem` 变大，后面的 `card_dev` 整体后移 8 |
| `struct snd_card` | 2856 | **2920** | +64（`offline` / `offline_change` / `offline_poll_wait`） | `card_dev` 由 +1696 变为 **+1704**（致命） |
| `struct pm_qos_request` | 184 | **280** | +96（`type` / `cpus_affine` / `irq` / `irq_notify`） | `snd_pcm_substream` 内该成员变大，其后所有字段后移 |
| `struct snd_pcm_substream` | 592 | **712** | +120（96 + 新增 `spinlock_t runtime_lock` 24） | `link_list` 由 360 变 **480**、`self_group` 496、`group` 600 |
| `struct snd_pcm_str` | 1088 | **1104** | +16（`vol_kctl` / `usr_kctl`） | `dev` 位于 +72；`snd_pcm` 随之变大 |
| `struct snd_pcm` | 2480 | **2512** | +32（两个 stream 各 +16） | `streams`@184、`private_data`@2488、`private_free`@2496 |
| `struct snd_pcm_ops` | 15 个函数指针 | **19 个** | +`compat_ioctl`、`delay_blk`、`wall_clock`、`restart` | **函数指针整体错位**，内核调用 `ops->open` 时跳到垃圾地址 |
| `struct snd_pcm_runtime` | — | 多 `unsigned int render_flag` | +4 | `sw_params` 之后字段偏移 |
| `struct device` | — | **1032** | 运行内核 arm64 的 `device.h` 与主线不同 | 所有内嵌 `struct device` 的结构（`snd_pcm_str`、`snd_card`）都受影响 |

### 4.2 为什么 `spinlock bad magic` / `kobject WARN` 会出现

`CONFIG_DEBUG_SPINLOCK=y` 时 `spinlock_t` 是 **24 字节**（含 `magic = 0xdead4ead`），而不是 4 字节。
这会让 `wait_queue_head_t`（spinlock + list_head）从 20 字节变成 **40 字节**，
进而改变所有内嵌等待队列的结构。

一旦结构体偏移错开，内核把普通内存当成 spinlock 读到 magic，就会打印：

```
BUG: spinlock bad magic on CPU#0, xxx
```

同样，`snd_card.card_dev` 偏移错 8 字节时，`device_add()` / `kobject_uevent()` 拿到的是错位的
`struct device`，就会在 `kobject_get` / `device_add` 路径上报 WARN 或直接 Oops。

### 4.3 最隐蔽的一类：`snd_pcm_ops` 函数表错位

修复了 `snd_card`、`substream` 之后，卡能注册，但一 `aplay` 就崩：

```
Internal error: SP/PC alignment exception: 8a000000
pc : 0x90113
lr : snd_pcm_detach_substream+0x34/0xa8
```

`pc=0x90113` 不是任何函数地址，且低两位不为 0（ARM64 指令必须 4 字节对齐）——
说明内核**跳转到了一个非函数指针的值**。
原因就是 vendor 的 `struct snd_pcm_ops` 比主线多 4 个成员，
我们编译的 ops 表被内核按错误偏移解读，`open/hw_params/close` 全部读串了。

这类问题的特征：不是 NULL 解引用，而是**随机 PC / 对齐异常 / 跳到奇怪的地址**。

---

## 5. 修复内容

### 5.1 头文件补丁（`patches/`）

| 补丁 | 文件 | 内容 |
|---|---|---|
| `0001` | `include/linux/pm_qos.h` | 加入厂商 `enum pm_qos_req_type`，`struct pm_qos_request` 补 `type`/`cpus_affine`/`irq`/`irq_notify`，恢复到 280 字节 |
| `0002` | `include/linux/rwsem.h` | 恢复 `long m_count;`，`struct rw_semaphore` 回到 72 字节 |
| `0003` | `include/sound/core.h` | `struct snd_card` 尾部补 `offline` / `offline_change` / `offline_poll_wait`，恢复到 2920 字节 |
| `0004` | `include/sound/pcm.h` | 同步厂商布局：`snd_pcm_ops` 四个新成员、`snd_pcm_runtime.render_flag`、`snd_pcm_substream.runtime_lock` + `hw_no_buffer`、`snd_pcm_str.vol_kctl/usr_kctl`、厂商 PCM volume/usr 控制 API 声明 |

应用方式：

```sh
cd linux-4.19.149
for p in /path/to/tdtech_ecube_4.19_audio/patches/*.patch; do
    patch -p1 < "$p"
done
```

也可以直接用 `headers/` 下的成品头文件覆盖对应路径（内容与打完补丁后一致）。

> **顺序很重要**：`0004`（pcm.h）必须在 `0001`（pm_qos.h）之后，因为 `snd_pcm_substream`
> 内嵌了 `struct pm_qos_request`。

### 5.2 驱动源码

`sound/usb/` 下的主线 `snd-usb-audio` 驱动**逻辑不需要改**，只需在 ABI 对齐后重新编译。
（调试期间在 `sound/usb/card.c` 里加过的 `USBADBG` 打印已全部删除。）

---

## 6. ABI 探针：怎么确认布局真的对了

只靠“编译通过”是不够的，必须验证**运行内核**的实际布局。
`tools/abi-probe/` 提供了探针模块：它同时打印

1. **编译期**偏移（模块头文件算出来的 `sizeof` / `offsetof`）；
2. **运行时**对象指针（用内核真正的 `snd_card_new()` / `snd_pcm_new()` 创建对象后读取）。

实测输出（eCube 上 `insmod abi_probe.ko` 的结果）：

```
ABI: sizeof(pm_qos_request)=280 sizeof(rw_semaphore)=72
ABI: sizeof(snd_card)=2920 ctl_dev=432 dev=1696 card_dev=1704 dev_groups=2736 registered=2768 remove_sleep=2776 offline=2816
ABI: sizeof(snd_pcm)=2512 streams=184 private_data=2488 private_free=2496
ABI: sizeof(snd_pcm_str)=1104 dev=72 substream=24
ABI: sizeof(snd_pcm_substream)=712 pm_qos=64 ops=408 runtime=416 runtime_lock=424 timer=448 wait_time=464 link_list=480 self_group=496 group=600
ABI: sizeof(snd_pcm_group)=104 lock=0 mutex=24 substreams=80 count=96
ABI: runtime card=ffffffc03c8a7000 ksize=4096 pcm=ffffffc03c8a6000
ABI: runtime pstr=ffffffc03c8a60b8 subs=ffffffc03d087800
ABI: runtime subs->pcm=ffffffc03c8a6000 (expect ffffffc03c8a6000) MATCH
ABI: runtime subs->pstr=ffffffc03c8a60b8 (expect ffffffc03c8a60b8) MATCH
ABI: runtime subs->name='subdevice #0' number=0 stream=0
ABI: probe done
```

`MATCH` 表示模块的 `offsetof(snd_pcm_substream, pcm/pstr)` 与内核写入的位置一致。

编译 / 运行：

```sh
cd tools/abi-probe
make KDIR=/path/to/linux-4.19.149
# 拷到设备后
insmod abi_probe.ko && dmesg | grep ABI; rmmod abi_probe
```

> ⚠️ 探针里 `snd_card_new()` 的 `parent` **不能传 NULL**。
> 厂商内核在 `snd_card_new()` 里会执行 `dev_driver_string(card->dev)`，
> `parent == NULL` 时会直接空指针解引用（表现为 `dev_driver_string+0x18` 处 Oops）。
> 本项目探针传的是 `&platform_bus`。真实驱动传的是 `&intf->dev`，天然安全。

---

## 7. 从零编译

### 7.1 环境

| 项 | 值 |
|---|---|
| 编译主机 | x86_64 Linux |
| 交叉工具链 | `aarch64-linux-gnu-gcc` 13.3.0 |
| 内核源码 | 主线 `linux-4.19.149`（已应用 `patches/`） |
| 内核配置 | 必须使用**设备内核的 `.config`** |
| 目标内核 | `4.19.149-00010-g1978b42`（aarch64，SMP，Debian 10） |

### 7.2 关键 `.config` 项（与结构体大小直接相关）

```
CONFIG_SMP=y
CONFIG_NR_CPUS=8
CONFIG_DEBUG_SPINLOCK=y          # spinlock_t = 24 字节（不是 4）
# CONFIG_DEBUG_LOCK_ALLOC is not set   # 没有 lockdep_map
# CONFIG_MODVERSIONS is not set
CONFIG_SND_VERBOSE_PROCFS=y
CONFIG_SND_DYNAMIC_MINORS=y
CONFIG_SND_MAX_CARDS=32
CONFIG_LOCALVERSION="-00010-g1978b42"   # 决定 vermagic
CONFIG_SND_USB_AUDIO=m
CONFIG_SND_RAWMIDI=m
CONFIG_SND_HWDEP=m
CONFIG_SND_SEQ_DEVICE=m
```

如果 `CONFIG_LOCALVERSION` 不对，编出来的模块会 `invalid module format`；
如果 `CONFIG_DEBUG_SPINLOCK` / `DEBUG_LOCK_ALLOC` / `SMP` / `NR_CPUS` 与设备不一致，
`spinlock_t`、`cpumask` 的大小会变，结构体又会错位。

### 7.3 编译

```sh
export KDIR=/path/to/linux-4.19.149
./scripts/build.sh
```

脚本等价于：

```sh
make -C $KDIR M=$KDIR/sound/core ARCH=arm64 \
     CROSS_COMPILE=aarch64-linux-gnu- CC=aarch64-linux-gnu-gcc modules

make -C $KDIR M=$KDIR/sound/usb  ARCH=arm64 \
     CROSS_COMPILE=aarch64-linux-gnu- CC=aarch64-linux-gnu-gcc modules
```

产物与校验：

```
snd-usb-audio.ko    3053912 bytes
snd-usbmidi-lib.ko   306480 bytes
snd-rawmidi.ko       373472 bytes
snd-hwdep.ko         243232 bytes
snd-seq-device.ko    187880 bytes

vermagic=4.19.149-00010-g1978b42 SMP mod_unload aarch64
```

### 7.4 模块依赖链

```
snd-usb-audio
 ├── snd-usbmidi-lib
 │    └── snd-rawmidi
 │         └── snd-seq-device
 └── snd-hwdep
```

运行内核里 `snd-pcm` / `snd-timer` / `snd` 等是 **builtin**（`lsmod` 里看不到），
因此只需要编上面 5 个 `.ko`。

---

## 8. 部署与验证

```sh
./scripts/install.sh     # cp 到 /lib/modules/$(uname -r)/kernel/sound/... + depmod -a
reboot
```

验证：

```sh
cat /proc/asound/cards
dmesg | grep -iE 'snd|usb 1-1'
aplay -l
aplay  -D plughw:1,0 test.wav
arecord -D plughw:1,0 -f S16_LE -r 48000 -c 1 -d 3 rec.wav
```

本项目实测结果：

- `card 1 [Device]: USB-Audio - USB PnP Audio Device` 正常注册
- `aplay` 播放 21.6 秒测试音乐 `rc=0`
- `arecord` 录音 3 秒 `rc=0`，波形峰值约 1721（真实音频数据，非静音）
- 期间 `dmesg` 中 `WARNING|BUG:|Oops|bad magic` 计数为 **0**

播放音量控制：

```sh
amixer -c 1 controls
amixer -c 1 sset 'Speaker' 100%
```

---

## 9. 针对 Rockchip（vendor kernel 4.19）的编译经验总结

1. **头文件必须跟随设备实际运行的 vendor 源码树**，不要拿主线头文件“猜”。
   本项目就是主线源码 + 厂商 `.config`，`.config` 对了但头文件不对，一样会崩。

2. **差异往往藏在基础结构体的新增字段**：`pm_qos_request`、`rw_semaphore`、`device`、
   `kobject`、`spinlock`、等待队列。它们出现在很多上层结构里，一个字段就能让整条链错位。

3. **`.config` 直接决定结构体大小**：
   - `CONFIG_DEBUG_SPINLOCK=y` → `spinlock_t` 4 → 24 字节，`wait_queue_head_t` 20 → 40 字节
   - `CONFIG_DEBUG_LOCK_ALLOC` → 给 mutex/spinlock 加 `lockdep_map`
   - `CONFIG_SMP` / `CONFIG_NR_CPUS` → 改变 `cpumask` 大小（影响 `pm_qos_request`）
   - `CONFIG_SND_VERBOSE_PROCFS` → 改变 `snd_pcm_substream` / `snd_pcm_str`
   编译前务必逐项比对。

4. **先写 ABI 探针，再写业务驱动**。`insmod` 打印 `sizeof`/`offsetof` + 用内核 API
   创建真实对象比对指针，是最快、最可靠的验证手段，比读源码猜测高效得多。

5. **学会读 Oops**：
   - `NULL pointer dereference` + `snd_usb_pcm_open` → `pcm->private_data` 偏移错
   - `spinlock bad magic` → 某个内嵌 spinlock 的结构错位
   - `kobject_get` WARN / `device_add` 崩溃 → `snd_card.card_dev` 偏移错
   - **随机 `pc` / `SP/PC alignment` / 跳到非法地址** → 函数指针表（`ops`）错位
   - `dev_driver_string+0x18` → 给 `snd_card_new()` 传了 `parent = NULL`

6. **vermagic 必须一字不差**：`4.19.149-00010-g1978b42 SMP mod_unload aarch64`。
   由 `CONFIG_LOCALVERSION="-00010-g1978b42"` 决定；否则 `invalid module format`。

7. **`CONFIG_MODVERSIONS=n` 时** MODPOST 会提示 `Module.symvers is missing`，
   属于正常警告，可忽略；符号仍按内核导出表解析。

8. **`M=` 的目录必须在目标内核树内部**，且要先编 `sound/core` 再编 `sound/usb`，
   否则 MODPOST 找不到依赖符号。

9. **确认哪些是 builtin、哪些是模块**：`lsmod` 里没有 `snd_pcm` 说明它是内置的，
   就不用（也不能）重复编译，否则会和内核里的定义冲突。

10. **vendor 内核常有私有字段和私有 API**（例如 `snd_pcm_add_volume_ctls`、
    `snd_pcm_add_usr_ctls`、`runtime_lock`、`render_flag`），必须按厂商头文件同步，
    不能只补“看起来缺的”那几个字段。

11. **调试流程建议**：
    ```sh
    dmesg -c > /dev/null     # 清空
    insmod xxx.ko            # 只触发一次
    dmesg | tail -n 100      # 只看本次
    ```
    设备无串口时通过 SSH 调试，命令加短超时并重试，避免内核崩溃后终端卡死。

12. **不要用向后兼容 hack 掩盖问题**：结构体偏移错了就是错了，
    加 padding、改回调、绕开某条路径都会留下更难查的随机崩溃。

---

## 10. 常见错误速查

| 现象 | 根因 | 处理 |
|---|---|---|
| `Unknown symbol snd_rawmidi_xxx` | 依赖模块未编/未装 | 按依赖链补齐并 `depmod -a` |
| `invalid module format` | vermagic 不匹配 | 检查 `CONFIG_LOCALVERSION` 与设备 `uname -r` |
| `spinlock bad magic` | 含 spinlock 的结构体错位 | 用 ABI 探针核对 `sizeof`/`offsetof` |
| `kobject_get WARN`、`device_add` Oops | `snd_card.card_dev` 偏移错 | 检查 `rw_semaphore` / `snd_card` 布局 |
| `NULL deref @ snd_usb_pcm_open` | `snd_pcm.private_data` 偏移错 | 检查 `snd_pcm_str` 的 `vol_kctl/usr_kctl` |
| `pc` 为非法值、SP/PC 对齐异常 | `snd_pcm_ops` 函数表错位 | 同步厂商 `snd_pcm_ops` 成员 |
| `dev_driver_string+0x18` Oops | `snd_card_new(NULL, ...)` | `parent` 传有效 `struct device` |

---

## 11. 文件校验

```
8d68309f56ccbc1ef132071b4bf0cad0  ko/snd-hwdep.ko
4f714bc0cc6d76e465af4c332f034d1c  ko/snd-rawmidi.ko
80f97b192067ef6d9b2f3026dc1e89f2  ko/snd-seq-device.ko
b756e11c42e18ffcea04ca07bb635b80  ko/snd-usb-audio.ko
a67e04a8489095656a59d62608bb98e1  ko/snd-usbmidi-lib.ko
```

---

## 12. 说明

- 模块遵循内核 GPL-2.0 许可（`MODULE_LICENSE("GPL")`）。
- `headers/` 与 `patches/` 仅用于**匹配该型号设备运行内核**的 ABI；
  换内核版本时必须重新生成探针数据并重新对齐，不能直接复用。
- 本项目只做“让驱动在该 vendor 内核上正确加载与工作”，不修改音频驱动本身的业务逻辑。
