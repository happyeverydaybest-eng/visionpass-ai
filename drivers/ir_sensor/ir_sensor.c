/*
 * VisionPass 红外避障模块GPIO驱动（GPIO5_IO02）
 *
 * 模块功能：
 * =========
 * 红外避障模块（又称"红外传感器"）用于检测门的状态（开/关）。
 * 当门打开时，红外光路被切断，模块输出高电平；
 * 当门关闭时，红外光路连通，模块输出低电平。
 *
 * 为什么用红外避障模块而不是门磁开关？
 * =========================================
 * 门磁开关（干簧管）需要磁铁配合，安装位置固定。
 * 红外避障模块安装更灵活，只需保证发射端和接收端对准即可。
 * 两者都是数字输出，代码层面没有区别。
 *
 * 接线说明（JP6排针）：
 * - VCC（+） → 3.3V（JP6排针）
 * - GND（-） → GND（JP6排针）
 * - OUT      → GPIO5_IO02（JP6 Pin 22，SNVS_TAMPER2）
 *
 * 用户空间接口 /dev/ir_sensor：
 * - read(fd, buf, 2)：读取当前电平（返回"0\n"或"1\n"）
 * - poll()/select()：等待状态变化（中断驱动）
 *   当GPIO电平变化时，poll会立即返回
 *
 * 中断触发方式：IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
 *   即上升沿（门打开）和下降沿（门关闭）都会触发中断
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/sched.h>

#define DEVICE_NAME  "ir_sensor"

/*
 * IR传感器设备结构体
 *
 * gpio:           GPIO引脚编号（内核编号）
 * irq:            中断号（由gpio_to_irq(gpio)转换得到）
 * state:          当前GPIO电平状态（0或1）
 * changed:        状态是否变化（中断中置1，read/poll中清零）
 * wait_queue:     等待队列（用于poll/select阻塞等待）
 * lock:           互斥锁（保护state/changed的并发访问）
 */
struct ir_dev {
	int gpio;
	int irq;
	int state;
	int changed;
	wait_queue_head_t wait_queue;
	struct mutex lock;
	dev_t devid;
	struct cdev cdev;
	struct class *class;
	struct device *device;
};

static struct ir_dev irdev;

/*
 * 中断处理函数（顶半部）
 * =====================
 * 当GPIO电平变化时，这个函数会被硬件自动调用。
 *
 * 为什么叫"顶半部"（Top Half）？
 * Linux中断处理分为顶半部和底半部：
 * - 顶半部（本函数）：快速执行，不能睡眠，只做最关键的事情
 * - 底半部（如tasklet/workqueue）：可延迟执行，可以做耗时操作
 *
 * 我们的中断处理很简单（只读GPIO+唤醒等待队列），所以顶半部就够了。
 *
 * 返回值：IRQ_HANDLED 表示中断已被正确处理
 */
static irqreturn_t ir_irq_handler(int irq, void *dev_id)
{
	struct ir_dev *dev = dev_id;

	/*
	 * 读取当前GPIO电平
	 * gpio_get_value()：读取GPIO输入值，返回0（低电平）或1（高电平）
	 */
	dev->state = gpio_get_value(dev->gpio);
	dev->changed = 1;

	/*
	 * 唤醒所有在等待队列上睡眠的进程
	 * 这使得poll()或select()能立即返回
	 */
	wake_up_interruptible(&dev->wait_queue);

	return IRQ_HANDLED;
}

/* ===== 文件操作 ===== */

static int ir_open(struct inode *inode, struct file *filp)
{
	filp->private_data = &irdev;
	pr_info("ir_sensor: device opened\n");
	return 0;
}

/*
 * read函数：读取当前GPIO电平
 *
 * 返回值格式（字符串，含换行符）：
 *   "0\n" → 低电平（门关闭）
 *   "1\n" → 高电平（门打开）
 *
 * 用法（C语言）：
 *   char buf[4];
 *   read(fd, buf, sizeof(buf));
 *   // buf[0] == '0' 或 '1'
 */
static ssize_t ir_read(struct file *filp, char __user *buf, size_t cnt, loff_t *ppos)
{
	struct ir_dev *dev = filp->private_data;
	char out[4];
	int len;

	/* 读取当前状态（带锁保护） */
	mutex_lock(&dev->lock);
	dev->state = gpio_get_value(dev->gpio);
	mutex_unlock(&dev->lock);

	/* 格式化为"0\n"或"1\n"字符串 */
	len = snprintf(out, sizeof(out), "%d\n", dev->state);

	/* 拷贝到用户空间 */
	if (copy_to_user(buf, out, len))
		return -EFAULT;

	return len;
}

/*
 * poll函数：支持select/poll/epoll
 *
 * 当应用程序调用poll()或select()时，内核会调用这个函数。
 * 如果GPIO状态已经变化（changed==1），立即返回POLLIN。
 * 如果没有变化，将进程加入等待队列，阻塞直到中断发生。
 *
 * 用法（C语言）：
 *   struct pollfd pfd = {fd, POLLIN, 0};
 *   poll(&pfd, 1, -1);  // -1表示无限等待
 *   // 返回后 pfd.revents & POLLIN 为真
 */
static unsigned int ir_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct ir_dev *dev = filp->private_data;
	unsigned int mask = 0;

	/* 将当前进程加入等待队列（poll_wait不会阻塞，只是注册） */
	poll_wait(filp, &dev->wait_queue, wait);

	/* 检查状态是否已变化 */
	mutex_lock(&dev->lock);
	if (dev->changed) {
		dev->changed = 0;      /* 消费掉这个事件 */
		mask |= POLLIN | POLLRDNORM;  /* 标记有数据可读 */
	}
	mutex_unlock(&dev->lock);

	return mask;
}

static int ir_release(struct inode *inode, struct file *filp)
{
	pr_info("ir_sensor: device closed\n");
	return 0;
}

static const struct file_operations ir_fops = {
	.owner   = THIS_MODULE,
	.open    = ir_open,
	.read    = ir_read,
	.poll    = ir_poll,      /* 支持poll/select/epoll */
	.release = ir_release,
};

/* ===== probe/remove ===== */

static int ir_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;

	pr_info("ir_sensor: probe start\n");

	/* 初始化等待队列和互斥锁 */
	init_waitqueue_head(&irdev.wait_queue);
	mutex_init(&irdev.lock);

	/*
	 * 从设备树获取GPIO引脚
	 * "ir-gpios" 与设备树中 ir-gpios = <&gpio5 2 GPIO_ACTIVE_LOW> 对应
	 */
	irdev.gpio = of_get_named_gpio(np, "ir-gpios", 0);
	if (irdev.gpio < 0) {
		dev_err(&pdev->dev, "cannot get ir-gpios: %d\n", irdev.gpio);
		return irdev.gpio;
	}
	dev_info(&pdev->dev, "IR sensor GPIO = %d\n", irdev.gpio);

	/* 申请GPIO */
	ret = gpio_request(irdev.gpio, "ir_sensor");
	if (ret < 0) {
		dev_err(&pdev->dev, "GPIO request failed: %d\n", ret);
		return ret;
	}

	/*
	 * 设置GPIO方向为输入
	 * 红外模块输出数字信号，开发板读取即可
	 */
	ret = gpio_direction_input(irdev.gpio);
	if (ret < 0) {
		dev_err(&pdev->dev, "GPIO direction input failed: %d\n", ret);
		goto err_gpio;
	}

	/*
	 * 读取初始状态
	 */
	irdev.state = gpio_get_value(irdev.gpio);
	irdev.changed = 0;
	dev_info(&pdev->dev, "IR sensor initial state = %d (%s)\n",
		 irdev.state, irdev.state ? "门打开" : "门关闭");

	/*
	 * 申请中断
	 * gpio_to_irq(gpio)：将GPIO编号转换为中断号
	 * ir_irq_handler：中断发生时调用的函数
	 * IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING：上升沿+下降沿都触发
	 * "ir_sensor"：中断名称（/proc/interrupts中可见）
	 * &irdev：传递给中断处理函数的参数
	 */
	irdev.irq = gpio_to_irq(irdev.gpio);
	ret = request_irq(irdev.irq, ir_irq_handler,
			  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			  "ir_sensor", &irdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "IRQ request failed: %d\n", ret);
		goto err_gpio;
	}
	dev_info(&pdev->dev, "IRQ = %d\n", irdev.irq);

	/* ===== 注册字符设备 ===== */

	ret = alloc_chrdev_region(&irdev.devid, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		dev_err(&pdev->dev, "alloc_chrdev_region failed: %d\n", ret);
		goto err_irq;
	}

	cdev_init(&irdev.cdev, &ir_fops);
	irdev.cdev.owner = THIS_MODULE;
	ret = cdev_add(&irdev.cdev, irdev.devid, 1);
	if (ret < 0) {
		dev_err(&pdev->dev, "cdev_add failed: %d\n", ret);
		goto err_chrdev;
	}

	irdev.class = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(irdev.class)) {
		ret = PTR_ERR(irdev.class);
		dev_err(&pdev->dev, "class_create failed: %d\n", ret);
		goto err_cdev;
	}

	irdev.device = device_create(irdev.class, &pdev->dev,
				    irdev.devid, NULL, DEVICE_NAME);
	if (IS_ERR(irdev.device)) {
		ret = PTR_ERR(irdev.device);
		dev_err(&pdev->dev, "device_create failed: %d\n", ret);
		goto err_class;
	}

	dev_info(&pdev->dev, "IR sensor driver ready, /dev/%s created\n", DEVICE_NAME);
	return 0;

err_class:
	class_destroy(irdev.class);
err_cdev:
	cdev_del(&irdev.cdev);
err_chrdev:
	unregister_chrdev_region(irdev.devid, 1);
err_irq:
	free_irq(irdev.irq, &irdev);
err_gpio:
	gpio_free(irdev.gpio);
	return ret;
}

static int ir_remove(struct platform_device *pdev)
{
	/* 释放中断 */
	free_irq(irdev.irq, &irdev);

	/* 销毁字符设备 */
	device_destroy(irdev.class, irdev.devid);
	class_destroy(irdev.class);
	cdev_del(&irdev.cdev);
	unregister_chrdev_region(irdev.devid, 1);

	/* 释放GPIO */
	gpio_free(irdev.gpio);

	pr_info("ir_sensor: driver removed\n");
	return 0;
}

/* 设备树匹配表 */
static const struct of_device_id ir_of_match[] = {
	{ .compatible = "visionpass,ir-sensor" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ir_of_match);

/* 平台驱动结构体 */
static struct platform_driver ir_driver = {
	.driver = {
		.name = "visionpass_ir",
		.owner = THIS_MODULE,
		.of_match_table = ir_of_match,
	},
	.probe = ir_probe,
	.remove = ir_remove,
};

static int __init ir_init(void)
{
	return platform_driver_register(&ir_driver);
}

static void __exit ir_exit(void)
{
	platform_driver_unregister(&ir_driver);
}

module_init(ir_init);
module_exit(ir_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("VisionPass");
MODULE_DESCRIPTION("IR sensor GPIO driver for I.MX6ULL door detection");