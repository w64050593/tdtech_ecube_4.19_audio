# ABI 探针模块
#
# KDIR 指向“与运行内核版本一致”的内核源码树（必须已配置 .config）。
# 例：make KDIR=/path/to/linux-4.19.149 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-

KDIR ?= /workspace/rtl8192eu-build/linux-4.19.149
ARCH ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
CC := $(CROSS_COMPILE)gcc

obj-m += abi_probe.o

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) CC=$(CC) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

.PHONY: all clean
