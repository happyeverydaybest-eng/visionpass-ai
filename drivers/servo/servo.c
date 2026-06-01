/*
 * VisionPass SG90舵机软件PWM驱动（GPIO5_IO10）
 *
 * SG90舵机控制原理（初学者必读）：
 * =========================================
 * SG90舵机内部有一个小型直流电机 + 减速齿轮 + 位置反馈。
 * 它不需要知道"角度"，而是通过接收到的脉冲宽度来控制位置。
 *
 * PWM（脉冲宽度调制）信号格式：
 * - 周期（Period）：20ms（即50Hz频率）
 * - 脉宽（Pulse Width）：0.5ms ~ 2.5ms
 *
 * 具体对应关系：
 *   0.5ms  →  0度（最左边）
 *   1.0ms  →  45度
 *   1.5ms  →  90度（中间）
 *   2.0ms  →  135度
 *   2.5ms  →  180度（最右边）
 *
 * 为什么用"软件PWM"而不是硬件PWM？
 * =========================================
 * I.MX6ULL的硬件PWM引脚（如PWM3）不一定方便引出到排针。
 * 软件PWM就是用一个内核定时器/kthread，按照固定时间间隔
 * 拉高→延时→拉低GPIO，模拟PWM波形。
 * 缺点：CPU占用率略高；优点：任意GPIO都能用。
 *
 * 接线说明（JP6排针）：
 * - VCC（红色） → 5V（JP6排针）
 * - GND（棕色） → GND（JP6排针）
 * - 信号（橙色） → GPIO5_IO10（JP6 Pin 28，BOOT_MODE0）
 *
 * 用户空间接口 /dev/servo：
 * - ioctl(fd, SERVO_SET_ANGLE, angle)：设置角度（0~180）
 * - ioctl(fd, SERVO_STOP)：停止PWM输出（释放GPIO）
 *
 * ioctl命令定义（与测试程序rc522.h保持一致）：
 *   #define SERVO_SET_ANGLE _IOW('S', 0, int)
 *   #define SERVO_STOP      _IO('S', 1)
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

#define DEVICE_NAME  "servo"
#define SERVO_SET_ANGLE  _IOW('S', 0, int)   /* 设置角度，参数0~180 */
#define SERVO_STOP       _IO('S', 1)         /* 停止PWM输出 */

/*
 * 舵机设备结构体
 * 每个字段的作用：
 *   gpio:        舵机信号引脚编号（内核GPIO编号，不是引脚号）
 *   angle:       当前目标角度（0~180）
 *   pulse_us:    当前脉宽（微秒），由angle计算得出
 *   running:     PWM线程是否在运行（1=运行中，0=已停止）
 *   task:        kthread结构体指针
 *   lock:        互斥锁，保护angle/pulse_us的并发访问
 *   devid/cdev/class/device: 字符设备相关
 */
struct servo_dev {
	int gpio;
	int angle;
	int pulse_us;
	int running;
	struct task_struct *task;
	struct mutex lock;
	dev_t devid;
	struct cdev cdev;
	struct class *class;
	struct device *device;
};

static struct servo_dev servo;

/*
 * 软件PWM内核线程函数
 * ====================
 * 这个函数是一个无限循环的线程，每20ms执行一次PWM周期。
 *
 * 每个PWM周期的时序（以90度为例，pulse_us=1500）：
 *   t=0ms:     GPIO拉高（PWM信号上升沿）
 *   t=1.5ms:   GPIO拉低（PWM信号下降沿）
 *   t=1.5~20ms:GPIO保持低电平（等待下一个周期）
 *
 * 为什么用kthread而不是高精度定时器（hrtimer）？
 * - kthread更直观，代码更容易理解
 * - SG90对PWM精度要求不高（±0.1ms误差可接受）
 * - hrtimer需要额外处理中断上下文限制
 */
static int servo_pwm_thread(void *data)
{
	struct servo_dev *dev = data;
	int pulse;

	pr_info("servo: PWM thread started\n");

	/*
	 * 线程主循环：只要running=1且线程未被要求停止，就一直执行
	 * kthread_should_stop()：当调用kthread_stop()时返回true
	 */
	while (!kthread_should_stop()) {
		/*
		 * 读取当前脉宽（带锁保护，防止与ioctl并发修改冲突）
		 * mutex_lock/unlock是Linux内核中常用的互斥锁机制
		 */
		mutex_lock(&dev->lock);
		pulse = dev->pulse_us;
		mutex_unlock(&dev->lock);

		/*
		 * 一个PWM周期 = 高电平时间(pulse_us) + 低电平时间(20ms - pulse_us)
		 * GPIO拉高 → 延时pulse_us → GPIO拉低 → 延时剩余时间
		 *
		 * 注意：这里用udelay()做微秒级延时。
		 * 在高精度场景下应使用hrtimer，但SG90精度要求不高，udelay足够。
		 */
		gpio_set_value(dev->gpio, 1);   /* 拉高：PWM上升沿 */
		udelay(pulse);                   /* 保持高电平pulse_us微秒 */
		gpio_set_value(dev->gpio, 0);   /* 拉低：PWM下降沿 */
		udelay(20000 - pulse);            /* 保持低电平直到周期结束（20ms = 20000us） */
	}

	pr_info("servo: PWM thread stopped\n");
	return 0;
}

/*
 * 启动PWM线程
 * 只有当线程未运行（running==0）时才启动
 */
static int servo_start_pwm(struct servo_dev *dev)
{
	if (dev->running)
		return 0;

	dev->running = 1;

	/*
	 * kthread_run()：创建并启动一个内核线程
	 * 参数1：线程函数（servo_pwm_thread）
	 * 参数2：传递给线程的参数（dev结构体指针）
	 * 参数3：线程名称（在ps命令中可见）
	 */
	dev->task = kthread_run(servo_pwm_thread, dev, "servo_pwm");
	if (IS_ERR(dev->task)) {
		pr_err("servo: failed to create PWM thread\n");
		dev->running = 0;
		return PTR_ERR(dev->task);
	}

	return 0;
}

/*
 * 停止PWM线程
 * 向线程发送停止信号，等待线程退出
 */
static void servo_stop_pwm(struct servo_dev *dev)
{
	if (!dev->running)
		return;

	dev->running = 0;

	/*
	 * kthread_stop()：请求线程停止
	 * 线程中的kthread_should_stop()会返回true，从而退出循环
	 * 返回值0表示线程正常退出
	 */
	if (dev->task) {
		kthread_stop(dev->task);
		dev->task = NULL;
	}
}

/* ===== 文件操作 ===== */

static int servo_open(struct inode *inode, struct file *filp)
{
	filp->private_data = &servo;
	pr_info("servo: device opened\n");
	return 0;
}

/*
 * ioctl：用户空间控制舵机的主要接口
 *
 * 用法（C语言）：
 *   int fd = open("/dev/servo", O_RDWR);
 *   ioctl(fd, SERVO_SET_ANGLE, 90);   // 设置90度
 *   ioctl(fd, SERVO_STOP);             // 停止PWM
 */
static long servo_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct servo_dev *dev = filp->private_data;
	int angle;

	switch (cmd) {
	case SERVO_SET_ANGLE:
		/*
		 * 直接从用户空间参数获取角度值
		 * _IOW('S', 0, int) 声明参数为int，用户传值而非指针
		 * 例如：ioctl(fd, SERVO_SET_ANGLE, 90) → arg=90
		 */
		angle = (int)arg;

		/* 角度范围校验 */
		if (angle < 0 || angle > 180) {
			pr_err("servo: invalid angle %d (must be 0~180)\n", angle);
			return -EINVAL;
		}

		/*
		 * 角度→脉宽转换公式：
		 * pulse_us = 500 + (angle * 2000 / 180)
		 *          = 500 + (angle * 100 / 9)
		 *
		 * 例如：angle=0   → pulse=500us
		 *       angle=90  → pulse=1500us
		 *       angle=180 → pulse=2500us
		 */
		mutex_lock(&dev->lock);
		dev->angle = angle;
		dev->pulse_us = 500 + (angle * 100 / 9);
		mutex_unlock(&dev->lock);

		pr_info("servo: set angle=%d, pulse_us=%d\n", angle, dev->pulse_us);

		/* 如果PWM线程未运行，启动它 */
		if (!dev->running)
			servo_start_pwm(dev);

		break;

	case SERVO_STOP:
		/* 停止PWM输出 */
		servo_stop_pwm(dev);
		/* 将GPIO置低，防止舵机受残余信号干扰 */
		gpio_set_value(dev->gpio, 0);
		pr_info("servo: stopped\n");
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int servo_release(struct inode *inode, struct file *filp)
{
	pr_info("servo: device closed\n");
	return 0;
}

static const struct file_operations servo_fops = {
	.owner          = THIS_MODULE,
	.open           = servo_open,
	.unlocked_ioctl = servo_ioctl,   /* unlocked_ioctl：新版内核推荐，不需要大内核锁 */
	.release        = servo_release,
};

/* ===== probe/remove（平台驱动标准流程） ===== */

static int servo_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;

	pr_info("servo: probe start\n");

	/*
	 * 初始化互斥锁
	 * mutex_init()必须在任何可能使用锁的代码之前调用
	 */
	mutex_init(&servo.lock);

	/*
	 * 从设备树获取GPIO引脚
	 * of_get_named_gpio()参数说明：
	 *   np: 设备节点指针（来自设备树）
	 *   "servo-gpios": 设备树属性名（与imx6ull-14x14-evk.dts中 servo-gpios 一致）
	 *   0: 属性中的第0个GPIO（属性可包含多个GPIO）
	 */
	servo.gpio = of_get_named_gpio(np, "servo-gpios", 0);
	if (servo.gpio < 0) {
		dev_err(&pdev->dev, "cannot get servo-gpios: %d\n", servo.gpio);
		return servo.gpio;
	}
	dev_info(&pdev->dev, "servo GPIO = %d\n", servo.gpio);

	/*
	 * 申请GPIO（标记为"servo_pwm"，便于调试时识别用途）
	 * gpio_request()：申请独占使用，防止其他驱动冲突
	 */
	ret = gpio_request(servo.gpio, "servo_pwm");
	if (ret < 0) {
		dev_err(&pdev->dev, "GPIO request failed: %d\n", ret);
		return ret;
	}

	/*
	 * 设置GPIO方向为输出，初始低电平
	 * gpio_direction_output(gpio, 0) = 输出模式，初始值0（低电平）
	 */
	ret = gpio_direction_output(servo.gpio, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "GPIO direction failed: %d\n", ret);
		goto err_gpio;
	}

	/* 初始化默认角度为90度（中间位置） */
	servo.angle = 90;
	servo.pulse_us = 1500;   /* 90度 = 1.5ms脉宽 */
	servo.running = 0;
	servo.task = NULL;

	/* ===== 注册字符设备 ===== */

	/* 1. 动态分配设备号 */
	ret = alloc_chrdev_region(&servo.devid, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		dev_err(&pdev->dev, "alloc_chrdev_region failed: %d\n", ret);
		goto err_gpio;
	}

	/* 2. 初始化并添加cdev */
	cdev_init(&servo.cdev, &servo_fops);
	servo.cdev.owner = THIS_MODULE;
	ret = cdev_add(&servo.cdev, servo.devid, 1);
	if (ret < 0) {
		dev_err(&pdev->dev, "cdev_add failed: %d\n", ret);
		goto err_chrdev;
	}

	/* 3. 创建设备类（Linux 4.1.15用2参数版本） */
	servo.class = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(servo.class)) {
		ret = PTR_ERR(servo.class);
		dev_err(&pdev->dev, "class_create failed: %d\n", ret);
		goto err_cdev;
	}

	/* 4. 创建设备节点 /dev/servo */
	servo.device = device_create(servo.class, &pdev->dev,
				servo.devid, NULL, DEVICE_NAME);
	if (IS_ERR(servo.device)) {
		ret = PTR_ERR(servo.device);
		dev_err(&pdev->dev, "device_create failed: %d\n", ret);
		goto err_class;
	}

	dev_info(&pdev->dev, "SG90 servo driver ready, /dev/%s created\n", DEVICE_NAME);
	return 0;

err_class:
	class_destroy(servo.class);
err_cdev:
	cdev_del(&servo.cdev);
err_chrdev:
	unregister_chrdev_region(servo.devid, 1);
err_gpio:
	gpio_free(servo.gpio);
	return ret;
}

static int servo_remove(struct platform_device *pdev)
{
	/* 停止PWM线程（如果还在运行） */
	servo_stop_pwm(&servo);

	/* 销毁字符设备 */
	device_destroy(servo.class, servo.devid);
	class_destroy(servo.class);
	cdev_del(&servo.cdev);
	unregister_chrdev_region(servo.devid, 1);

	/* 释放GPIO */
	gpio_free(servo.gpio);

	pr_info("servo: driver removed\n");
	return 0;
}

/* 设备树匹配表 */
static const struct of_device_id servo_of_match[] = {
	{ .compatible = "visionpass,servo" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, servo_of_match);

/* 平台驱动结构体 */
static struct platform_driver servo_driver = {
	.driver = {
		.name = "visionpass_servo",
		.owner = THIS_MODULE,
		.of_match_table = servo_of_match,
	},
	.probe = servo_probe,
	.remove = servo_remove,
};

static int __init servo_init(void)
{
	return platform_driver_register(&servo_driver);
}

static void __exit servo_exit(void)
{
	platform_driver_unregister(&servo_driver);
}

module_init(servo_init);
module_exit(servo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("VisionPass");
MODULE_DESCRIPTION("SG90 servo software PWM driver for I.MX6ULL");