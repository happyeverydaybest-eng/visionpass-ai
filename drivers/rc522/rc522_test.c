/*
 * VisionPass RC522驱动测试程序
 *
 * 功能：
 * 1. 打开 /dev/rc522
 * 2. 读取VersionReg(0x37)验证SPI通信是否正常
 *    - RC522正常时VersionReg值为0x91或0x92（MFRC522版本号）
 *    - FM17522（国产替代）版本号为0x88
 * 3. 写入+回读TxControlReg(0x14)验证寄存器读写路径完整
 * 4. 执行寻卡操作（PcdRequest + PcdAnticoll），验证完整RFID读卡流程
 *
 * 编译命令（交叉编译）：
 *   arm-linux-gnueabihf-gcc rc522_test.c -o rc522_test
 *
 * 部署到开发板：
 *   cp rc522_test /opt/visionpass/bin/
 *   或通过NFS：cp rc522_test ~/linux/nfs/rootfs/opt/visionpass/bin/
 *
 * 运行前提：
 *   1. 已加载rc522.ko驱动（insmod rc522.ko）
 *   2. /dev/rc522设备节点已创建
 *   3. RC522模块已正确接线（SPI4线+RST接3.3V或GPIO）
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/* RC522关键寄存器地址（与rc522.h中定义一致） */
#define CommandReg        0x01
#define ComIEnReg         0x02
#define ComIrqReg         0x04
#define ErrorReg          0x06
#define Status2Reg        0x08
#define FIFODataReg       0x09
#define FIFOLevelReg      0x0A
#define ControlReg        0x0C
#define BitFramingReg     0x0D
#define ModeReg           0x11
#define TxControlReg      0x14
#define RxModeReg         0x13
#define RxThresholdReg    0x18
#define TModeReg          0x2A
#define TPrescalerReg     0x2B
#define TReloadRegH       0x2C
#define TReloadRegL       0x2D
#define CollReg             0x0E
#define VersionReg        0x37

/* RC522命令定义 */
#define PCD_IDLE          0x00
#define PCD_AUTHENT       0x0E
#define PCD_RECEIVE       0x08
#define PCD_TRANSMIT      0x04
#define PCD_TRANSCEIVE    0x0C
#define PCD_RESETPHASE    0x0F
#define PCD_CALCCRC       0x03

/* PICC命令定义 */
#define PICC_REQIDL      0x26
#define PICC_REQALL      0x52
#define PICC_ANTICOLL1   0x93

/* 状态码 */
#define MI_OK            0
#define MI_ERR           -1
#define MI_NOTAGERR      -2

/* 通过/dev/rc522读取一个寄存器 */
static uint8_t ReadRawRC(int fd, uint8_t addr)
{
	uint8_t buf = addr;
	/* read()接口：buf[0]传入地址，返回时buf[0]为寄存器值 */
	read(fd, &buf, 1);
	return buf;
}

/* 通过/dev/rc522写入一个寄存器 */
static void WriteRawRC(int fd, uint8_t addr, uint8_t val)
{
	uint8_t buf[2] = {addr, val};
	/* write()接口：buf[0]=地址，buf[1]=值 */
	write(fd, buf, 2);
}

/* 置位寄存器中的指定bit */
static void SetBitMask(int fd, uint8_t reg, uint8_t mask)
{
	uint8_t tmp = ReadRawRC(fd, reg);
	WriteRawRC(fd, reg, tmp | mask);
}

/* 清位寄存器中的指定bit */
static void ClearBitMask(int fd, uint8_t reg, uint8_t mask)
{
	uint8_t tmp = ReadRawRC(fd, reg);
	WriteRawRC(fd, reg, tmp & (~mask));
}

/* 开启天线 */
static void PcdAntennaOn(int fd)
{
	uint8_t tmp = ReadRawRC(fd, TxControlReg);
	if (!(tmp & 0x03))
		SetBitMask(fd, TxControlReg, 0x03);
}

/* 关闭天线 */
static void PcdAntennaOff(int fd)
{
	ClearBitMask(fd, TxControlReg, 0x03);
}

/* 复位RC522 */
static int PcdReset(int fd)
{
	WriteRawRC(fd, CommandReg, PCD_RESETPHASE);
	usleep(50000);  /* 50ms等待复位完成 */

	/* 配置ISO14443-A模式 */
	WriteRawRC(fd, ModeReg, 0x3D);        /* 初始化ModeReg */
	WriteRawRC(fd, RxModeReg, 0x00);      /* 初始化RxModeReg */

	/* 配置定时器：TAuto=1, TPrescaler=0x3E=62, TReload=500 */
	WriteRawRC(fd, TModeReg, 0x8D);
	WriteRawRC(fd, TPrescalerReg, 0x3E);
	WriteRawRC(fd, TReloadRegH, 0x00);
	WriteRawRC(fd, TReloadRegL, 0x1F);    /* 31 → ~4.8ms超时 */

	WriteRawRC(fd, TxControlReg, 0x03);   /* 开启天线TX1+TX2 */

	PcdAntennaOn(fd);
	return MI_OK;
}

/*
 * RC522与卡片核心通信函数
 * ucCommand: PCD命令字
 * pInData: 发送数据
 * ucInLenByte: 发送数据字节数
 * pOutData: 接收数据缓冲区
 * pOutLenBit: 接收数据位长度（输出）
 */
static char PcdComMF522(int fd, uint8_t ucCommand, uint8_t *pInData,
			uint8_t ucInLenByte, uint8_t *pOutData,
			uint32_t *pOutLenBit)
{
	uint8_t irq = 0x00;
	uint8_t irq_wait = 0x00;
	uint8_t last_bits = 0;
	uint8_t n = 0;
	uint32_t i = 0;

	/* 根据命令设置等待的IRQ类型 */
	if (ucCommand == PCD_AUTHENT) {
		irq_wait = 0x10;  /* 等待认证完成IRQ */
	} else if (ucCommand == PCD_TRANSCEIVE) {
		irq_wait = 0x30;  /* 等待发送完成+接收完成IRQ */
	}

	WriteRawRC(fd, ComIEnReg, irq_wait | 0x80);  /* 允许IRQ中断 */
	ClearBitMask(fd, ComIrqReg, 0x80);           /* 清除所有IRQ标志 */
	WriteRawRC(fd, CommandReg, PCD_IDLE);         /* 取消当前命令 */
	SetBitMask(fd, FIFOLevelReg, 0x80);           /* 清空FIFO */

	/* 写入发送数据到FIFO */
	for (i = 0; i < ucInLenByte; i++)
		WriteRawRC(fd, FIFODataReg, pInData[i]);

	/* 执行命令 */
	WriteRawRC(fd, BitFramingReg, ucInLenByte == 0 ? 0x00 : 0x07);
	WriteRawRC(fd, CommandReg, ucCommand);

	/* TRANSCEIVE命令需要手动启动发送（置BitFramingReg的StartSend位） */
	if (ucCommand == PCD_TRANSCEIVE)
		SetBitMask(fd, BitFramingReg, 0x80);

	/* 等待命令完成（轮询ComIrqReg） */
	i = 2000;  /* 最大等待循环次数 */
	do {
		irq = ReadRawRC(fd, ComIrqReg);
		if (irq & 0x01)  /* 定时器超时IRQ */
			break;
		if (irq & irq_wait)  /* 等待的IRQ已发生 */
			break;
		usleep(100);  /* 100μs延迟 */
	} while (--i);

	/* 清除StartSend位 */
	ClearBitMask(fd, BitFramingReg, 0x80);

	/* 超时检查 */
	if (i == 0)
		return MI_ERR;

	/* 错误检查 */
	if (ReadRawRC(fd, ErrorReg) & 0x1D)
		return MI_ERR;

	/* 读取接收数据 */
	n = ReadRawRC(fd, FIFOLevelReg);
	last_bits = ReadRawRC(fd, ControlReg) & 0x07;
	if (last_bits)
		*pOutLenBit = n * 8 + last_bits;
	else
		*pOutLenBit = n * 8;

	if (n == 0)
		n = 1;
	if (n > 18)  /* FIFO最大25字节，取18避免溢出 */
		n = 18;

	for (i = 0; i < n; i++)
		pOutData[i] = ReadRawRC(fd, FIFODataReg);

	return MI_OK;
}

/* 寻卡 */
static char PcdRequest(int fd, uint8_t req_code, uint8_t *pTagType)
{
	uint8_t buf[2];
	uint32_t len = 0;

	WriteRawRC(fd, BitFramingReg, 0x07);
	buf[0] = req_code;

	return PcdComMF522(fd, PCD_TRANSCEIVE, buf, 1, pTagType, &len);
}

/* 防冲撞（获取卡片4字节UID） */
static char PcdAnticoll(int fd, uint8_t *pSnr)
{
	uint8_t buf[5];
	uint8_t snr_check = 0;
	uint32_t len = 0;
	uint8_t i;

	WriteRawRC(fd, BitFramingReg, 0x00);
	ClearBitMask(fd, CollReg, 0x80);
	buf[0] = PICC_ANTICOLL1;
	buf[1] = 0x20;

	if (PcdComMF522(fd, PCD_TRANSCEIVE, buf, 2, pSnr, &len) != MI_OK)
		return MI_ERR;

	/* 校验UID（4字节异或校验） */
	for (i = 0; i < 4; i++)
		snr_check ^= pSnr[i];
	if (snr_check != pSnr[4])
		return MI_ERR;

	return MI_OK;
}

/* ===== 测试主函数 ===== */

int main(int argc, char *argv[])
{
	int fd;
	uint8_t version;
	uint8_t tx_val, rx_val;
	uint8_t card_type[2];
	uint8_t uid[5];
	int loop = 0;
	int read_only = 0;  /* 0=持续循环读卡，1=只做寄存器测试 */

	if (argc > 1 && strcmp(argv[1], "--test-only") == 0)
		read_only = 1;

	/* 打开RC522字符设备 */
	fd = open("/dev/rc522", O_RDWR);
	if (fd < 0) {
		printf("ERROR: Cannot open /dev/rc522: %s\n", strerror(errno));
		printf("  请确认已执行: insmod rc522.ko\n");
		return -1;
	}
	printf("Opened /dev/rc522 (fd=%d)\n", fd);

	/* ===== 测试1：读取VersionReg验证SPI通信 ===== */
	PcdReset(fd);
	version = ReadRawRC(fd, VersionReg);
	printf("VersionReg(0x37) = 0x%02X", version);
	if (version == 0x91 || version == 0x92)
		printf("  → MFRC522 (NXP原版) ✅\n");
	else if (version == 0x88)
		printf("  → FM17522 (国产替代) ✅\n");
	else if (version == 0x00 || version == 0xFF)
		printf("  → SPI通信失败 ❌ (检查接线)\n");
	else
		printf("  → 未知芯片，值非0 ❌\n");

	/* ===== 测试2：写入+回读TxControlReg验证寄存器读写 ===== */
	WriteRawRC(fd, TxControlReg, 0x83);
	tx_val = ReadRawRC(fd, TxControlReg);
	printf("TxControlReg write 0x83 → readback 0x%02X", tx_val);
	if (tx_val == 0x83)
		printf(" ✅ 写读一致\n");
	else
		printf(" ❌ 写读不一致（SPI或驱动问题）\n");

	WriteRawRC(fd, TxControlReg, 0x00);
	rx_val = ReadRawRC(fd, TxControlReg);
	printf("TxControlReg write 0x00 → readback 0x%02X", rx_val);
	if (rx_val == 0x00)
		printf(" ✅ 写读一致\n");
	else
		printf(" ❌ 写读不一致\n");

	/* 重新初始化，恢复天线 */
	PcdReset(fd);

	if (read_only) {
		printf("Register test completed (use without --test-only for card reading)\n");
		close(fd);
		return 0;
	}

	/* ===== 测试3：循环寻卡（PcdRequest + PcdAnticoll） ===== */
	printf("\n--- Card reading loop (press Ctrl+C to stop) ---\n");
	while (1) {
		/* 寻卡：检测区域内未休眠的卡 */
		if (PcdRequest(fd, PICC_REQIDL, card_type) == MI_OK) {
			/* 防冲撞：获取4字节UID */
			if (PcdAnticoll(fd, uid) == MI_OK) {
				loop++;
				printf("[%d] Card detected! UID: %02X %02X %02X %02X  Type: %02X %02X\n",
				       loop, uid[0], uid[1], uid[2], uid[3],
				       card_type[0], card_type[1]);
				/* 让卡进入休眠，避免重复读取同一张卡 */
				PcdAntennaOff(fd);
				usleep(200000);  /* 200ms间隔 */
				PcdAntennaOn(fd);
			}
		}
		usleep(100000);  /* 100ms轮询间隔 */
	}

	close(fd);
	return 0;
}