# VisionPass RC522 RFID驱动 — 外部内核模块编译
#
# 编译命令：make
# 清理命令：make clean
# 编译产物：rc522.ko（拷贝到开发板后 insmod rc522.ko 加载）
#
# KERNELDIR指向正点原子官方内核源码（Linux 4.1.15）
# 交叉编译工具链需提前配置（export ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-）

KERNELDIR := /home/viper/linux/IMX6ULL/linux/alientek_linux_2026_4_26
CURRENT_PATH := $(shell pwd)

# 交叉编译工具链配置
export ARCH := arm
export CROSS_COMPILE := arm-linux-gnueabihf-

obj-m := rc522.o

build: kernel_modules

kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) clean