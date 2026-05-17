/*
 * VisionPass 红外传感器测试程序
 *
 * 功能：
 * 1. 打开 /dev/ir_sensor
 * 2. 读取当前GPIO电平（0=门关闭，1=门打开）
 * 3. 使用poll()等待状态变化（上升沿/下降沿触发）
 * 4. 循环打印门状态
 *
 * 用法：
 *   ./ir_sensor_test           # 持续轮询打印状态
 *   ./ir_sensor_test --poll     # 使用poll()等待变化（推荐）
 *   ./ir_sensor_test --once     # 只读取一次
 *
 * 编译命令（交叉编译）：
 *   arm-linux-gnueabihf-gcc ir_sensor_test.c -o ir_sensor_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

/* 读取当前GPIO状态（0或1） */
int read_ir_state(int fd)
{
	char buf[4];
	int n;

	n = read(fd, buf, sizeof(buf));
	if (n <= 0)
		return -1;

	/* buf[0] 是 '0' 或 '1' */
	return (buf[0] == '1') ? 1 : 0;
}

int main(int argc, char *argv[])
{
	int fd;
	int state;
	int poll_mode = 0;
	int once_mode = 0;
	int i;

	/* 解析命令行参数 */
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--poll") == 0)
			poll_mode = 1;
		else if (strcmp(argv[i], "--once") == 0)
			once_mode = 1;
	}

	/* 打开设备 */
	fd = open("/dev/ir_sensor", O_RDWR);
	if (fd < 0) {
		printf("ERROR: Cannot open /dev/ir_sensor: %s\n", strerror(errno));
		printf("  请确认已执行: insmod ir_sensor.ko\n");
		return -1;
	}
	printf("Opened /dev/ir_sensor (fd=%d)\n", fd);

	if (once_mode) {
		/* 只读取一次 */
		state = read_ir_state(fd);
		printf("Door state: %s (%d)\n",
		       state ? "OPEN" : "CLOSED", state);
		close(fd);
		return 0;
	}

	if (poll_mode) {
		/* poll模式：等待GPIO变化 */
		struct pollfd pfd;
		int ret;

		printf("\n=== Poll mode (waiting for door state change) ===\n");
		printf("Current state: %s\n",
		       read_ir_state(fd) ? "OPEN" : "CLOSED");
		printf("Waiting... (Ctrl+C to exit)\n\n");

		pfd.fd = fd;
		pfd.events = POLLIN;  /* 等待可读事件 */

		while (1) {
			ret = poll(&pfd, 1, -1);  /* -1 = 无限等待 */
			if (ret < 0) {
				perror("poll failed");
				break;
			}

			if (pfd.revents & POLLIN) {
				state = read_ir_state(fd);
				printf("[Change detected] Door state: %s (%d)\n",
				       state ? "OPEN" : "CLOSED", state);
			}
		}
	} else {
		/* 轮询模式：每秒读取一次 */
		printf("\n=== Polling mode (Ctrl+C to exit) ===\n");
		while (1) {
			state = read_ir_state(fd);
			printf("Door state: %s (%d)\n",
			       state ? "OPEN" : "CLOSED", state);
			sleep(1);
		}
	}

	close(fd);
	return 0;
}