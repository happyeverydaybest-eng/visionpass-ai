/*
 * RC522 寄存器写入验证诊断
 * 逐条写入并立即读回，确认SPI双向通信正常
 */

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static int fd;

static uint8_t ReadReg(uint8_t addr)
{
	uint8_t buf = addr;
	read(fd, &buf, 1);
	return buf;
}

static void WriteReg(uint8_t addr, uint8_t val)
{
	uint8_t buf[2] = {addr, val};
	write(fd, buf, 2);
}

static void WriteRegVerify(uint8_t addr, uint8_t val, const char *name)
{
	uint8_t before = ReadReg(addr);
	WriteReg(addr, val);
	usleep(1000);  /* 1ms稳定时间 */
	uint8_t after = ReadReg(addr);
	printf("  %-20s addr=0x%02X: before=0x%02X -> write 0x%02X -> readback=0x%02X %s\n",
	       name, addr, before, val, after,
	       (after == val) ? "OK" : "FAIL!!!");
}

int main()
{
	fd = open("/dev/rc522", O_RDWR);
	if (fd < 0) {
		printf("Cannot open /dev/rc522: %s\n", strerror(errno));
		return 1;
	}
	printf("Opened /dev/rc522\n\n");

	/* 先做软复位，回到默认状态 */
	printf("=== Soft Reset ===\n");
	WriteReg(0x01, 0x0F);
	usleep(50000);
	printf("After reset, VersionReg = 0x%02X\n\n", ReadReg(0x37));

	/* 逐条写入关键寄存器并验证 */
	printf("=== Register Write Verification ===\n");

	WriteRegVerify(0x2A, 0x8D, "TModeReg");
	WriteRegVerify(0x2B, 0x3E, "TPrescalerReg");
	WriteRegVerify(0x2C, 0x00, "TReloadRegH");
	WriteRegVerify(0x2D, 0x1E, "TReloadRegL");
	WriteRegVerify(0x15, 0x40, "TxAutoReg");
	WriteRegVerify(0x11, 0x3D, "ModeReg");
	WriteRegVerify(0x17, 0x86, "RxSelReg");
	WriteRegVerify(0x26, 0x7F, "RFCfgReg");
	WriteRegVerify(0x14, 0x03, "TxControlReg");

	/* 最终检查：所有关键寄存器的当前值 */
	printf("\n=== Final Register State ===\n");
	printf("  CommandReg(0x01)    = 0x%02X\n", ReadReg(0x01));
	printf("  ModeReg(0x11)       = 0x%02X\n", ReadReg(0x11));
	printf("  TxControlReg(0x14)  = 0x%02X (should be 0x03)\n", ReadReg(0x14));
	printf("  TxAutoReg(0x15)     = 0x%02X (should be 0x40)\n", ReadReg(0x15));
	printf("  RxSelReg(0x17)      = 0x%02X (should be 0x86)\n", ReadReg(0x17));
	printf("  RFCfgReg(0x26)      = 0x%02X (should be 0x7F)\n", ReadReg(0x26));
	printf("  TModeReg(0x2A)      = 0x%02X (should be 0x8D)\n", ReadReg(0x2A));
	printf("  VersionReg(0x37)    = 0x%02X (should be 0x92)\n", ReadReg(0x37));

	/* 尝试寻卡 */
	printf("\n=== Card Detection Test ===\n");
	printf("TxControlReg = 0x%02X", ReadReg(0x14));
	if (ReadReg(0x14) & 0x03) printf(" -> antenna ON\n");
	else printf(" -> antenna OFF!\n");

	/* 设置BitFramingReg并发送REQALL */
	printf("Sending PICC_REQALL...\n");
	WriteReg(0x02, 0x77 | 0x80);  /* ComIEnReg */
	WriteReg(0x04 + 0x80, 0x80);  /* ClearBitMask ComIrqReg */
	WriteReg(0x01, 0x00);         /* CommandReg = IDLE */
	WriteReg(0x0A + 0x40, 0x80);  /* SetBitMask FIFOLevelReg - flush */
	WriteReg(0x09, 0x52);         /* FIFODataReg = PICC_REQALL */
	WriteReg(0x0D, 0x07);         /* BitFramingReg = 7 bits */
	WriteReg(0x01, 0x0C);         /* CommandReg = TRANSCEIVE */
	WriteReg(0x0D + 0x40, 0x80);  /* SetBitMask BitFramingReg - start */

	/* 等待响应 */
	printf("Waiting for response...\n");
	int timeout = 2000;
	uint8_t irq = 0;
	while (timeout--) {
		irq = ReadReg(0x04);  /* ComIrqReg */
		if (irq & 0x01) {
			printf("Timer IRQ (no card response)\n");
			break;
		}
		if (irq & 0x30) {
			printf("Got response IRQ: 0x%02X\n", irq);
			break;
		}
		usleep(100);
	}
	if (timeout < 0) printf("Timeout!\n");

	printf("ErrorReg = 0x%02X\n", ReadReg(0x06));
	printf("Status1Reg = 0x%02X\n", ReadReg(0x07));
	printf("FIFOLevel = %d bytes\n", ReadReg(0x0A));

	close(fd);
	return 0;
}
