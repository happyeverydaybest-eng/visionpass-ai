# VisionPass SG90舵机驱动 — 外部内核模块编译
#
# 编译命令：make
# 清理命令：make clean
# 编译产物：servo.ko（拷贝到开发板后 insmod servo.ko 加载）

KERNELDIR := /home/viper/linux/IMX6ULL/linux/alientek_linux_2026_4_26
CURRENT_PATH := $(shell pwd)

obj-m := servo.o

build: kernel_modules

kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) clean