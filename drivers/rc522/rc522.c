/*
 * VisionPass RC522 RFID SPI字符设备驱动
 *
 * 基于原版驱动改进：
 * 1. 使用SPI子系统自动CS管理（不手动GPIO片选，spi_write_then_read自动管理CS）
 * 2. 使用spi_write_then_read()简化单字节操作（比原版spi_transfer+spi_message更高效）
 * 3. 通过设备树compatible匹配（不硬编码"/soc/..."节点路径）
 * 4. RST GPIO可选（设备树未指定时用软复位，用户可把RST引脚接3.3V）
 * 5. 适用于ECSPI3 CS1（与ICM20608共享SPI总线，CS由SPI框架管理）
 *
 * 用户空间接口 /dev/rc522：
 * - read(fd, buf, 1)：buf[0]放寄存器地址，返回后buf[0]为寄存器值
 * - write(fd, buf, 2)：buf[0]=寄存器地址，buf[1]=写入值
 *
 * RC522 SPI地址格式：
 * - 读：地址字节 = ((reg << 1) & 0x7E) | 0x80
 * - 写：地址字节 = (reg << 1) & 0x7E
 *   bit7=1表示读，bit7=0表示写，bit6始终为0（地址在bit1-bit5）
 *
 * 注意：Linux 4.1.15中class_create()为2参数版本 class_create(owner, name)
 *       新版内核(6.4+)改为 class_create(name)，编译时需注意API变化
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#define DEVICE_NAME    "rc522"
#define DEVICE_COUNT   1

/* RC522设备结构体 */
struct rc522_dev {
	dev_t devid;                /* 设备号 */
	struct cdev cdev;           /* 字符设备结构体 */
	struct class *class;        /* 设备类 */
	struct device *device;      /* 设备节点 /dev/rc522 */
	struct spi_device *spi;     /* SPI设备指针，由probe函数传入 */
	int rst_gpio;               /* RST复位引脚（可选，<0表示未使用） */
};

static struct rc522_dev rc522dev;

/*
 * spi_write_then_read() vs 原版spi_sync()+spi_transfer的区别：
 * 原版：手动分配spi_transfer → 手动拉低CS → spi_sync发地址 → spi_sync读数据 → 手动拉高CS → 释放transfer
 * 本版：spi_write_then_read()一条调用完成，CS由SPI框架自动管理，无需手动GPIO操作
 *
 * 这在ECSPI3共享总线场景下特别重要：ICM20608(CS0)和RC522(CS1)共用同一SPI总线，
 * SPI框架根据spi_device的reg=<0>或reg=<1>自动选择正确的CS GPIO，不会冲突
 */

/* 读取一个RC522寄存器 */
static int rc522_read_reg(struct spi_device *spi, u8 reg, u8 *val)
{
	u8 addr_byte = ((reg << 1) & 0x7E) | 0x80;  /* bit7=1表示读操作 */
	/* spi_write_then_read：先发1字节地址，再读1字节数据，CS全程保持低电平 */
	return spi_write_then_read(spi, &addr_byte, 1, val, 1);
}

/* 写入一个RC522寄存器 */
static int rc522_write_reg(struct spi_device *spi, u8 reg, u8 val)
{
	u8 tx_buf[2];
	tx_buf[0] = (reg << 1) & 0x7E;   /* bit7=0表示写操作 */
	tx_buf[1] = val;
	/* spi_write：发送2字节（地址+数据），CS全程保持低电平 */
	return spi_write(spi, tx_buf, 2);
}

/* 硬件复位：RST GPIO拉低→等待→拉高 */
static void rc522_hw_reset(void)
{
	if (rc522dev.rst_gpio < 0)
		return;  /* 无RST GPIO，跳过硬件复位（用户需将RST引脚接3.3V） */

	gpio_set_value(rc522dev.rst_gpio, 0);  /* 拉低：触发复位 */
	msleep(10);                             /* 保持10ms，确保复位生效 */
	gpio_set_value(rc522dev.rst_gpio, 1);  /* 拉高：释放复位，RC522开始工作 */
	msleep(10);                             /* 等待RC522内部初始化完成 */
}

/* 软复位：通过CommandReg发送PCD_RESETPHASE(0x0F)命令 */
static void rc522_soft_reset(struct spi_device *spi)
{
	int ret;
	u8 ver;

	ret = rc522_write_reg(spi, 0x01, 0x0F);  /* CommandReg = PCD_RESETPHASE */
	pr_info("rc522: soft_reset write ret=%d\n", ret);
	msleep(50);                                /* 等待RC522内部复位完成 */

	/* 读取VersionReg验证SPI通信 */
	ret = rc522_read_reg(spi, 0x37, &ver);
	pr_info("rc522: soft_reset VersionReg ret=%d, val=0x%02X\n", ret, ver);
}

static int rc522_open(struct inode *inode, struct file *filp)
{
	filp->private_data = &rc522dev;

	/* 复位RC522：先硬件复位（如果可用），再软复位 */
	rc522_hw_reset();
	rc522_soft_reset(rc522dev.spi);

	pr_info("rc522: device opened, reset done\n");
	return 0;
}

/*
 * 用户空间读操作协议：
 * 用法：u8 addr = 0x37; read(fd, &addr, 1);
 *       执行后addr变为寄存器0x37的值
 * 说明：buf[0]传入寄存器地址，返回时buf[0]为寄存器值
 *       与原版驱动接口兼容（原版用户空间代码无需修改）
 */
static ssize_t rc522_read(struct file *filp, char __user *buf,
			   size_t cnt, loff_t *ppos)
{
	u8 addr, val;
	int ret;
	struct rc522_dev *dev = filp->private_data;

	if (cnt < 1)
		return -EINVAL;

	/* 从用户空间获取寄存器地址 */
	if (copy_from_user(&addr, buf, 1))
		return -EFAULT;

	/* 通过SPI读取寄存器 */
	ret = rc522_read_reg(dev->spi, addr, &val);
	if (ret < 0) {
		pr_err("rc522: SPI read reg 0x%02X failed: %d\n", addr, ret);
		return ret;
	}

	/* 将寄存器值返回用户空间 */
	if (copy_to_user(buf, &val, 1))
		return -EFAULT;

	return 1;
}

/*
 * 用户空间写操作协议：
 * 用法：u8 buf[2] = {0x01, 0x0F}; write(fd, buf, 2);
 *       向CommandReg(0x01)写入PCD_RESETPHASE(0x0F)
 * 说明：buf[0]=寄存器地址，buf[1]=写入值
 *       与原版驱动接口兼容
 */
static ssize_t rc522_write(struct file *filp, const char __user *buf,
			    size_t cnt, loff_t *ppos)
{
	u8 data[2];
	int ret;
	struct rc522_dev *dev = filp->private_data;

	if (cnt < 2)
		return -EINVAL;

	/* 从用户空间获取寄存器地址和写入值 */
	if (copy_from_user(data, buf, 2))
		return -EFAULT;

	/* 通过SPI写入寄存器 */
	ret = rc522_write_reg(dev->spi, data[0], data[1]);
	if (ret < 0) {
		pr_err("rc522: SPI write reg 0x%02X val 0x%02X failed: %d\n",
		       data[0], data[1], ret);
		return ret;
	}

	return 2;
}

static int rc522_release(struct inode *inode, struct file *filp)
{
	/* 关闭时软复位RC522（天线关闭，进入低功耗） */
	rc522_soft_reset(rc522dev.spi);
	pr_info("rc522: device closed\n");
	return 0;
}

/* 字符设备文件操作集合 */
static const struct file_operations rc522_fops = {
	.owner   = THIS_MODULE,
	.open    = rc522_open,
	.read    = rc522_read,
	.write   = rc522_write,
	.release = rc522_release,
};

/*
 * probe函数：SPI框架匹配到设备树rc522@1节点后调用
 * spi_device已由SPI框架初始化，包含了reg=<1>对应的CS GPIO信息
 */
static int rc522_probe(struct spi_device *spi)
{
	int ret;
	struct device_node *np = spi->dev.of_node;

	pr_info("rc522: probe, cs=%d, max_freq=%d\n",
		spi->chip_select, spi->max_speed_hz);

	/* 设置SPI模式：RC522使用Mode 0（CPOL=0, CPHA=0），最高5MHz */
	spi->mode = SPI_MODE_0;
	spi->max_speed_hz = 1000000;   /* 降低到1MHz，提高信号完整性 */
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "SPI setup failed: %d\n", ret);
		return ret;
	}

	rc522dev.spi = spi;

	/* 获取RST GPIO（可选属性，设备树未指定时rst_gpio<0，仅用软复位） */
	rc522dev.rst_gpio = of_get_named_gpio(np, "rst-gpio", 0);
	if (rc522dev.rst_gpio < 0) {
		dev_info(&spi->dev, "No RST GPIO, using soft reset only\n");
	} else {
		ret = gpio_request(rc522dev.rst_gpio, "rc522_rst");
		if (ret < 0) {
			dev_err(&spi->dev, "RST GPIO request failed: %d\n", ret);
			return ret;
		}
		ret = gpio_direction_output(rc522dev.rst_gpio, 1);
		if (ret < 0) {
			dev_err(&spi->dev, "RST GPIO direction failed: %d\n", ret);
			gpio_free(rc522dev.rst_gpio);
			return ret;
		}
		dev_info(&spi->dev, "RST GPIO = %d\n", rc522dev.rst_gpio);
	}

	/* ===== 注册字符设备 ===== */

	/* 1. 动态分配设备号 */
	ret = alloc_chrdev_region(&rc522dev.devid, 0, DEVICE_COUNT, DEVICE_NAME);
	if (ret < 0) {
		dev_err(&spi->dev, "alloc_chrdev_region failed: %d\n", ret);
		goto err_gpio;
	}

	/* 2. 初始化cdev并添加到系统 */
	cdev_init(&rc522dev.cdev, &rc522_fops);
	rc522dev.cdev.owner = THIS_MODULE;
	ret = cdev_add(&rc522dev.cdev, rc522dev.devid, DEVICE_COUNT);
	if (ret < 0) {
		dev_err(&spi->dev, "cdev_add failed: %d\n", ret);
		goto err_chrdev;
	}

	/* 3. 创建设备类（Linux 4.1.15用2参数版本：class_create(owner, name)） */
	rc522dev.class = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(rc522dev.class)) {
		ret = PTR_ERR(rc522dev.class);
		dev_err(&spi->dev, "class_create failed: %d\n", ret);
		goto err_cdev;
	}

	/* 4. 创建设备节点 /dev/rc522 */
	rc522dev.device = device_create(rc522dev.class, &spi->dev,
					rc522dev.devid, NULL, DEVICE_NAME);
	if (IS_ERR(rc522dev.device)) {
		ret = PTR_ERR(rc522dev.device);
		dev_err(&spi->dev, "device_create failed: %d\n", ret);
		goto err_class;
	}

	/* 初始化复位：确保RC522处于工作状态 */
	rc522_hw_reset();
	rc522_soft_reset(spi);

	/* 打印SPI模式和速度，用于调试 */
	dev_info(&spi->dev, "SPI mode=%d, speed=%d Hz, CS=%d\n",
		 spi->mode, spi->max_speed_hz, spi->chip_select);

	dev_info(&spi->dev, "RC522 driver ready, /dev/rc522 created\n");
	return 0;

err_class:
	class_destroy(rc522dev.class);
err_cdev:
	cdev_del(&rc522dev.cdev);
err_chrdev:
	unregister_chrdev_region(rc522dev.devid, DEVICE_COUNT);
err_gpio:
	if (rc522dev.rst_gpio >= 0)
		gpio_free(rc522dev.rst_gpio);
	return ret;
}

static int rc522_remove(struct spi_device *spi)
{
	/* 销毁字符设备 */
	device_destroy(rc522dev.class, rc522dev.devid);
	class_destroy(rc522dev.class);
	cdev_del(&rc522dev.cdev);
	unregister_chrdev_region(rc522dev.devid, DEVICE_COUNT);

	/* 释放RST GPIO */
	if (rc522dev.rst_gpio >= 0)
		gpio_free(rc522dev.rst_gpio);

	dev_info(&spi->dev, "RC522 driver removed\n");
	return 0;
}

/* SPI设备ID表（用于非设备树匹配） */
static const struct spi_device_id rc522_id[] = {
	{ "alientek,rc522", 0 },
	{ /* sentinel */ }
};

/* 设备树匹配表（与imx6ull-14x14-evk.dts中rc522@1节点compatible一致） */
static const struct of_device_id rc522_of_match[] = {
	{ .compatible = "alientek,rc522" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rc522_of_match);

/* SPI驱动结构体 */
static struct spi_driver rc522_driver = {
	.driver = {
		.name = "visionpass_rc522",
		.owner = THIS_MODULE,
		.of_match_table = rc522_of_match,
	},
	.probe = rc522_probe,
	.remove = rc522_remove,
	.id_table = rc522_id,
};

/* 驱动入口：注册SPI驱动 */
static int __init rc522_init(void)
{
	return spi_register_driver(&rc522_driver);
}

/* 驺动出口：注销SPI驱动 */
static void __exit rc522_exit(void)
{
	spi_unregister_driver(&rc522_driver);
}

module_init(rc522_init);
module_exit(rc522_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("VisionPass");
MODULE_DESCRIPTION("RC522 RFID SPI driver for I.MX6ULL VisionPass");