/*
 * VisionPass SG90舵机测试程序
 *
 * 功能：
 * 1. 打开 /dev/servo
 * 2. 通过ioctl设置不同角度（0°, 45°, 90°, 135°, 180°）
 * 3. 观察舵机转动
 * 4. 支持"开锁"和"关锁"预设角度
 *
 * 用法：
 *   ./servo_test           # 循环测试0-180度
 *   ./servo_test <angle>   # 设置指定角度（0~180）
 *   ./servo_test unlock    # 开锁（默认90度）
 *   ./servo_test lock      # 关锁（默认0度）
 *   ./servo_test stop      # 停止PWM
 *
 * 编译命令（交叉编译）：
 *   arm-linux-gnueabihf-gcc servo_test.c -o servo_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

/* ioctl命令定义（必须与驱动中完全一致） */
#define SERVO_SET_ANGLE  _IOW('S', 0, int)
#define SERVO_STOP       _IO('S', 1)

/* 设置角度的封装函数 */
int servo_set_angle(int fd, int angle)
{
	int ret = ioctl(fd, SERVO_SET_ANGLE, angle);
	if (ret < 0) {
		perror("ioctl SERVO_SET_ANGLE failed");
		return -1;
	}
	printf("Set angle = %d\n", angle);
	return 0;
}

/* 停止舵机 */
int servo_stop(int fd)
{
	int ret = ioctl(fd, SERVO_STOP, 0);
	if (ret < 0) {
		perror("ioctl SERVO_STOP failed");
		return -1;
	}
	printf("Servo stopped\n");
	return 0;
}

int main(int argc, char *argv[])
{
	int fd;
	int angle = -1;
	int i;

	/* 打开设备 */
	fd = open("/dev/servo", O_RDWR);
	if (fd < 0) {
		printf("ERROR: Cannot open /dev/servo: %s\n", strerror(errno));
		printf("  请确认已执行: insmod servo.ko\n");
		return -1;
	}
	printf("Opened /dev/servo (fd=%d)\n\n", fd);

	/* 解析命令行参数 */
	if (argc > 1) {
		if (strcmp(argv[1], "unlock") == 0) {
			angle = 90;   /* 开锁：舵机转到90度 */
			printf("Command: UNLOCK (angle=90)\n");
		} else if (strcmp(argv[1], "lock") == 0) {
			angle = 0;    /* 关锁：舵机转到0度 */
			printf("Command: LOCK (angle=0)\n");
		} else if (strcmp(argv[1], "stop") == 0) {
			printf("Command: STOP\n");
			servo_stop(fd);
			close(fd);
			return 0;
		} else {
			angle = atoi(argv[1]);
			if (angle < 0 || angle > 180) {
				printf("Invalid angle: %s (must be 0~180)\n", argv[1]);
				close(fd);
				return -1;
			}
			printf("Command: SET ANGLE = %d\n", angle);
		}
	}

	/* 如果指定了角度，直接设置 */
	if (angle >= 0) {
		servo_set_angle(fd, angle);
		printf("Waiting 2 seconds...\n");
		sleep(2);
	} else {
		/* 循环测试：0° -> 45° -> 90° -> 135° -> 180° -> 90° -> 0° */
		int angles[] = {0, 45, 90, 135, 180, 90, 0};
		int num_angles = sizeof(angles) / sizeof(angles[0]);

		printf("=== Servo angle sweep test ===\n");
		for (i = 0; i < num_angles; i++) {
			servo_set_angle(fd, angles[i]);
			sleep(1);  /* 等待1秒让舵机到位 */
		}
		printf("Sweep test done.\n");
	}

	close(fd);
	return 0;
}