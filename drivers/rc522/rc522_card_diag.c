/*
 * RC522 卡检测诊断程序
 * 步骤：初始化RC522 → 开启天线 → 检查RF场 → 尝试寻卡 → 详细错误诊断
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/* RC522寄存器地址 */
#define CommandReg        0x01
#define ComIEnReg         0x02
#define ComIrqReg         0x04
#define DivIrqReg         0x05
#define ErrorReg          0x06
#define Status1Reg        0x07
#define Status2Reg        0x08
#define FIFODataReg       0x09
#define FIFOLevelReg      0x0A
#define ControlReg        0x0C
#define BitFramingReg     0x0D
#define CollReg           0x0E
#define ModeReg           0x11
#define TxControlReg      0x14
#define TxAutoReg         0x15
#define TxSelReg          0x16
#define RxSelReg          0x17
#define RxThresholdReg    0x18
#define DemodReg          0x19
#define RFCfgReg          0x26
#define GsNReg            0x27
#define CWGsPReg          0x28
#define ModGsPReg         0x29
#define TModeReg          0x2A
#define TPrescalerReg     0x2B
#define TReloadRegH       0x2C
#define TReloadRegL       0x2D
#define VersionReg        0x37
#define AnalogTestReg     0x38
#define TestADCReg        0x3B

/* PCD命令 */
#define PCD_IDLE          0x00
#define PCD_TRANSCEIVE    0x0C
#define PCD_RESETPHASE    0x0F
#define PCD_CALCCRC       0x03

/* PICC命令 */
#define PICC_REQIDL       0x26
#define PICC_REQALL       0x52
#define PICC_ANTICOLL1    0x93

#define MI_OK             0
#define MI_ERR            -1

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

static void SetBitMask(uint8_t reg, uint8_t mask)
{
	WriteReg(reg + 0x40, mask);
}

static void ClearBitMask(uint8_t reg, uint8_t mask)
{
	WriteReg(reg + 0x80, mask);
}

/* 初始化RC522 */
static int rc522_init(void)
{
	uint8_t ver;

	/* 软复位 */
	WriteReg(CommandReg, PCD_RESETPHASE);
	usleep(50000);

	/* 定时器配置 */
	WriteReg(TModeReg, 0x8D);
	WriteReg(TPrescalerReg, 0x3E);
	WriteReg(TReloadRegH, 0x00);
	WriteReg(TReloadRegL, 0x1E);

	/* RF配置 */
	WriteReg(TxAutoReg, 0x40);    /* 100% ASK调制 */
	WriteReg(ModeReg, 0x3D);      /* ISO14443-A模式 */
	WriteReg(RxSelReg, 0x86);     /* 接收选择 */
	WriteReg(RFCfgReg, 0x7F);     /* 最大增益48dB */

	/* 开启天线 */
	WriteReg(TxControlReg, 0x03); /* TX1+TX2使能 */

	usleep(50000);

	/* 验证 */
	ver = ReadReg(VersionReg);
	printf("[INIT] VersionReg = 0x%02X", ver);
	if (ver == 0x91 || ver == 0x92) printf(" (MFRC522)\n");
	else if (ver == 0x88) printf(" (FM17522)\n");
	else printf(" (UNKNOWN!)\n");

	uint8_t tx = ReadReg(TxControlReg);
	printf("[INIT] TxControlReg = 0x%02X", tx);
	if (tx & 0x03) printf(" (antenna ON)\n");
	else printf(" (antenna OFF!)\n");

	printf("[INIT] RFCfgReg = 0x%02X (gain)\n", ReadReg(RFCfgReg));
	printf("[INIT] TxAutoReg = 0x%02X\n", ReadReg(TxAutoReg));
	printf("[INIT] ModeReg = 0x%02X\n", ReadReg(ModeReg));
	printf("[INIT] RxSelReg = 0x%02X\n", ReadReg(RxSelReg));
	printf("[INIT] RxThresholdReg = 0x%02X\n", ReadReg(RxThresholdReg));
	printf("[INIT] DemodReg = 0x%02X\n", ReadReg(DemodReg));

	return (ver == 0x91 || ver == 0x92 || ver == 0x88) ? 0 : -1;
}

/* RC522与卡片核心通信（带详细诊断） */
static char communicate_debug(uint8_t command, uint8_t *inData, uint8_t inLen,
			      uint8_t *outData, uint32_t outBufSize,
			      uint32_t *outLenBit, int debug)
{
	uint8_t irq, irqEn, irqWait, lastBits, n;
	uint32_t i;

	switch (command) {
	case PCD_TRANSCEIVE:
		irqEn = 0x77;
		irqWait = 0x30;
		break;
	default:
		irqEn = 0x00;
		irqWait = 0x00;
		break;
	}

	WriteReg(ComIEnReg, irqEn | 0x80);
	ClearBitMask(ComIrqReg, 0x80);
	WriteReg(CommandReg, PCD_IDLE);
	SetBitMask(FIFOLevelReg, 0x80);

	for (i = 0; i < inLen; i++)
		WriteReg(FIFODataReg, inData[i]);

	if (debug) {
		printf("[COMM] FIFO written %d bytes:", inLen);
		for (i = 0; i < inLen; i++)
			printf(" %02X", inData[i]);
		printf("\n");
	}

	/* 保留调用者设置的BitFramingReg */
	WriteReg(CommandReg, command);
	if (command == PCD_TRANSCEIVE)
		SetBitMask(BitFramingReg, 0x80);

	if (debug)
		printf("[COMM] Command sent, waiting for IRQ...\n");

	/* 等待完成 */
	i = 2000;
	do {
		irq = ReadReg(ComIrqReg);
		if (irq & 0x01) {
			if (debug)
				printf("[COMM] Timer IRQ (timeout), ComIrqReg=0x%02X\n", irq);
			break;
		}
		if (irq & irqWait) {
			if (debug)
				printf("[COMM] Got IRQ: ComIrqReg=0x%02X (expected 0x%02X)\n", irq, irqWait);
			break;
		}
		usleep(100);
	} while (--i);

	ClearBitMask(BitFramingReg, 0x80);

	if (i == 0) {
		if (debug)
			printf("[COMM] ERROR: Polling timeout (2000 iterations)\n");
		return MI_ERR;
	}

	/* 错误检查 */
	uint8_t err = ReadReg(ErrorReg);
	if (err & 0x1B) {
		if (debug) {
			printf("[COMM] ERROR: ErrorReg=0x%02X", err);
			if (err & 0x01) printf(" ProtocolErr");
			if (err & 0x02) printf(" ParityErr");
			if (err & 0x08) printf(" BufferOvfl");
			if (err & 0x10) printf(" CollErr");
			printf("\n");
		}
		return MI_ERR;
	}

	/* 读取接收数据 */
	n = ReadReg(FIFOLevelReg);
	lastBits = ReadReg(ControlReg) & 0x07;
	if (lastBits)
		*outLenBit = (n - 1) * 8 + lastBits;
	else
		*outLenBit = n * 8;

	if (debug) {
		printf("[COMM] Received %d bytes, %d bits (FIFOLevel=%d, ControlReg&7=%d)\n",
		       n, *outLenBit, n, lastBits);
		printf("[COMM] Status1Reg=0x%02X, Status2Reg=0x%02X\n",
		       ReadReg(Status1Reg), ReadReg(Status2Reg));
	}

	uint8_t maxRead = (n > 18) ? 18 : n;
	if (maxRead > outBufSize)
		maxRead = outBufSize;

	for (i = 0; i < maxRead; i++) {
		outData[i] = ReadReg(FIFODataReg);
		if (debug)
			printf("[COMM] FIFO[%d] = 0x%02X\n", i, outData[i]);
	}

	SetBitMask(ControlReg, 0x80);
	WriteReg(CommandReg, PCD_IDLE);

	if (n == 0 && debug)
		printf("[COMM] WARNING: No data received (FIFO empty)\n");

	return MI_OK;
}

/* 寻卡 */
static char request(uint8_t reqCode, uint8_t *tagType, int debug)
{
	uint8_t buf[2];
	uint32_t len = 0;

	ClearBitMask(Status2Reg, 0x08);
	WriteReg(BitFramingReg, 0x07);
	buf[0] = reqCode;

	return communicate_debug(PCD_TRANSCEIVE, buf, 1, tagType, 2, &len, debug);
}

/* 防冲撞 */
static char anticoll(uint8_t *uid, int debug)
{
	uint8_t buf[5];
	uint8_t snr_check = 0;
	uint32_t len = 0;
	uint8_t i;

	WriteReg(BitFramingReg, 0x00);
	ClearBitMask(CollReg, 0x80);
	buf[0] = PICC_ANTICOLL1;
	buf[1] = 0x20;

	if (communicate_debug(PCD_TRANSCEIVE, buf, 2, uid, 5, &len, debug) != MI_OK)
		return MI_ERR;

	for (i = 0; i < 4; i++)
		snr_check ^= uid[i];
	if (snr_check != uid[4]) {
		if (debug)
			printf("[ANTICOLL] CRC check failed\n");
		return MI_ERR;
	}

	SetBitMask(CollReg, 0x80);
	return MI_OK;
}

int main(int argc, char *argv[])
{
	uint8_t tagType[2];
	uint8_t uid[5];
	int debug = 1;
	int attempt;

	fd = open("/dev/rc522", O_RDWR);
	if (fd < 0) {
		printf("ERROR: Cannot open /dev/rc522\n");
		return -1;
	}
	printf("Opened /dev/rc522 (fd=%d)\n\n", fd);

	/* 初始化 */
	printf("=== Step 1: Initialize RC522 ===\n");
	if (rc522_init() < 0) {
		printf("INIT FAILED!\n");
		close(fd);
		return -1;
	}

	/* 等待RF场稳定 */
	usleep(100000);
	printf("\n=== Step 2: RF field check ===\n");
	printf("TxControlReg = 0x%02X", ReadReg(TxControlReg));
	if (ReadReg(TxControlReg) & 0x03)
		printf(" -> RF field should be ON\n");
	else
		printf(" -> RF field is OFF!\n");

	/* 尝试寻卡（带详细诊断） */
	printf("\n=== Step 3: Card detection (5 attempts with debug) ===\n");
	for (attempt = 1; attempt <= 5; attempt++) {
		printf("\n--- Attempt %d ---\n", attempt);

		/* 先关闭再开天线，确保干净状态 */
		WriteReg(TxControlReg, 0x00);
		usleep(10000);
		WriteReg(TxControlReg, 0x03);
		usleep(10000);

		/* 先试REQALL（检测所有卡，包括休眠的） */
		printf("[REQ] Sending PICC_REQALL (0x52)...\n");
		char ret = request(PICC_REQALL, tagType, debug);
		if (ret == MI_OK) {
			printf("[REQ] SUCCESS! TagType: %02X %02X\n", tagType[0], tagType[1]);

			/* 尝试防冲撞获取UID */
			printf("[ANTICOLL] Sending anticoll...\n");
			if (anticoll(uid, debug) == MI_OK) {
				printf("\n*** CARD DETECTED! UID: %02X %02X %02X %02X ***\n\n",
				       uid[0], uid[1], uid[2], uid[3]);
			} else {
				printf("[ANTICOLL] Failed\n");
			}
		} else {
			printf("[REQ] FAILED - no card response\n");
			printf("[REQ] ErrorReg=0x%02X, Status1Reg=0x%02X\n",
			       ReadReg(ErrorReg), ReadReg(Status1Reg));
		}

		usleep(500000);  /* 500ms间隔 */
	}

	/* 天线关闭测试：检查环境噪声 */
	printf("\n=== Step 4: Antenna OFF test (noise check) ===\n");
	WriteReg(TxControlReg, 0x00);
	usleep(50000);
	printf("TxControlReg after OFF = 0x%02X\n", ReadReg(TxControlReg));
	printf("RxSelReg = 0x%02X\n", ReadReg(RxSelReg));

	close(fd);
	return 0;
}
