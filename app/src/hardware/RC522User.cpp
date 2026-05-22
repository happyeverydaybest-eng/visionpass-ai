/*
 * VisionPass RC522用户空间封装实现
 *
 * 参考原版 rc522.cpp，但改为面向对象封装：
 * - fd 从全局变量改为成员变量
 * - 添加错误处理和日志
 * - 线程安全（每个RC522User实例独立管理fd）
 *
 * RC522寄存器特殊地址：
 * - 读寄存器：地址 + 0x00
 * - 写寄存器：地址 + 0x00
 * - Set寄存器：地址 + 0x40（原子置位，无需read-modify-write）
 * - Clear寄存器：地址 + 0x80（原子清位，无需read-modify-write）
 */

#include "RC522User.h"
#include <QDebug>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

/* RC522寄存器地址 */
static const uint8_t CommandReg    = 0x01;
static const uint8_t ComIEnReg     = 0x02;  /* 中断使能寄存器 */
static const uint8_t ComIrqReg     = 0x04;
static const uint8_t ErrorReg      = 0x06;
static const uint8_t Status2Reg    = 0x08;
static const uint8_t FIFODataReg   = 0x09;
static const uint8_t FIFOLevelReg  = 0x0A;
static const uint8_t BitFramingReg = 0x0D;
static const uint8_t CollReg       = 0x0E;
static const uint8_t ModeReg       = 0x11;
static const uint8_t TxControlReg  = 0x14;
static const uint8_t TxAutoReg     = 0x15;
static const uint8_t RxSelReg      = 0x17;
static const uint8_t RFCfgReg      = 0x26;
static const uint8_t ControlReg    = 0x0C;
static const uint8_t DivIrqReg     = 0x05;  /* CRC中断寄存器（非ComIrqReg） */
static const uint8_t VersionReg    = 0x37;  /* 固件版本寄存器 */
static const uint8_t CRCResultRegL = 0x22;  /* CRC结果低字节 */
static const uint8_t CRCResultRegM = 0x21;  /* CRC结果高字节 */
static const uint8_t TModeReg      = 0x2A;
static const uint8_t TPrescalerReg = 0x2B;
static const uint8_t TReloadRegH   = 0x2C;
static const uint8_t TReloadRegL   = 0x2D;

/* PCD命令 */
static const uint8_t PCD_IDLE       = 0x00;
static const uint8_t PCD_AUTHENT    = 0x0E;
static const uint8_t PCD_RECEIVE    = 0x08;
static const uint8_t PCD_TRANSMIT   = 0x04;
static const uint8_t PCD_TRANSCEIVE = 0x0C;
static const uint8_t PCD_RESETPHASE = 0x0F;
static const uint8_t PCD_CALCCRC    = 0x03;

/* PICC命令 */
static const uint8_t PICC_REQIDL    = 0x26;
static const uint8_t PICC_ANTICOLL1 = 0x93;
static const uint8_t PICC_HALT      = 0x50;

/* 状态码 */
static const char MI_OK       = 0;
static const char MI_ERR      = -1;

/* 预计算的HALT+CRC命令（PICC_HALT=0x50, 0x00的CRC_A固定为0xCD, 0x38） */
static const uint8_t HALT_CMD[4] = {0x50, 0x00, 0xCD, 0x38};

RC522User::RC522User()
	: m_fd(-1)
{
}

RC522User::~RC522User()
{
	closeDevice();
}

bool RC522User::openDevice()
{
	if (m_fd >= 0)
		return true;

	m_fd = ::open("/dev/rc522", O_RDWR);
	if (m_fd < 0) {
		qWarning() << "RC522: Cannot open /dev/rc522:" << strerror(errno);
		return false;
	}

	qInfo() << "RC522: Device opened";
	return true;
}

void RC522User::closeDevice()
{
	if (m_fd >= 0) {
		::close(m_fd);
		m_fd = -1;
	}
}

bool RC522User::isOpen() const
{
	return m_fd >= 0;
}

/* ===== 底层寄存器读写 ===== */

uint8_t RC522User::readReg(uint8_t addr)
{
	uint8_t buf = addr;
	if (::read(m_fd, &buf, 1) != 1) {
		qWarning() << "RC522: readReg failed for addr 0x" << QString::number(addr, 16);
		return 0;
	}
	return buf;
}

void RC522User::writeReg(uint8_t addr, uint8_t val)
{
	uint8_t buf[2] = {addr, val};
	if (::write(m_fd, buf, 2) != 2) {
		qWarning() << "RC522: writeReg failed for addr 0x" << QString::number(addr, 16);
	}
}

/*
 * setBitMask/clearBitMask优化：
 * 使用RC522专用的Set/Clear寄存器（地址偏移+0x40/+0x80）
 * 原子操作，无需read-modify-write，每次调用节省1次SPI读取
 */
void RC522User::setBitMask(uint8_t reg, uint8_t mask)
{
	writeReg(reg + 0x40, mask);  /* Set寄存器：原子置位 */
}

void RC522User::clearBitMask(uint8_t reg, uint8_t mask)
{
	writeReg(reg + 0x80, mask);  /* Clear寄存器：原子清位 */
}

void RC522User::antennaOn()
{
	setBitMask(TxControlReg, 0x03);
}

void RC522User::antennaOff()
{
	clearBitMask(TxControlReg, 0x03);
}

/* ===== 初始化 ===== */

bool RC522User::init()
{
	if (m_fd < 0) {
		qWarning() << "RC522: Device not opened";
		return false;
	}

	/* 软复位 */
	writeReg(CommandReg, PCD_RESETPHASE);
	usleep(50000);  /* 50ms */

	/* 配置定时器：TAuto=1, TPrescaleExt=1, TGating=00, TReload=01 */
	writeReg(TModeReg, 0x8D);
	/* 预分频器值：0x3E (62)，与TModeReg配合得到40kHz时钟 */
	writeReg(TPrescalerReg, 0x3E);
	writeReg(TReloadRegH, 0x00);
	/* 定时器重载值：30 → ~4.8ms超时（参考值，可根据实际调整） */
	writeReg(TReloadRegL, 0x1E);

	/* TxAutoReg: Force100ASK=1 → 100% ASK调制（必须用于Mifare卡） */
	writeReg(TxAutoReg, 0x40);

	/* ModeReg: MSBFirst=0, TR=01, CRCPreset=111101=0x3D */
	writeReg(ModeReg, 0x3D);

	/* RxSelReg: RxWait=0x06, UartSelRx=0 */
	writeReg(RxSelReg, 0x86);

	/* RFCfgReg: RxGain=111 (最大增益48dB) */
	writeReg(RFCfgReg, 0x7F);

	/* 开启天线 */
	antennaOn();

	/* 验证初始化：读取VersionReg */
	uint8_t version = readReg(VersionReg);
	qInfo() << "RC522: VersionReg = 0x" << QString::number(version, 16);

	return true;
}

/* ===== 核心通信 ===== */

char RC522User::communicate(uint8_t command, uint8_t *inData, uint8_t inLen,
			    uint8_t *outData, uint32_t outBufSize, uint32_t &outLenBit)
{
	uint8_t irq = 0x00;
	uint8_t irqEn = 0x00;
	uint8_t irqWait = 0x00;
	uint8_t lastBits = 0;
	uint32_t i = 0;

	/*
	 * 根据命令类型设置中断使能和等待标志：
	 * PCD_AUTHENT:  等待IdleIRq (bit4) + ErrIRq (bit1)
	 * PCD_TRANSCEIVE: 等待TxIRq+RxIRq (bits 4,5) + IdleIRq (bit4)
	 *                 + ErrIRq (bit1) + TimerIRq (bit0) + LoAlertIRq (bit2)
	 * 0x77 = 01110111b: TxIErr+RxIErr+IdleIRqEn+LoAlertIErr+ErrIErr+TimerIErr
	 */
	switch (command) {
	case PCD_AUTHENT:
		irqEn = 0x12;    /* ErrIErr(1) + IdleIRqEn(4) */
		irqWait = 0x10;  /* 等待IdleIRq */
		break;
	case PCD_TRANSCEIVE:
		irqEn = 0x77;    /* TxIErr+RxIErr+IdleIRq+LoAlertIErr+ErrIErr+TimerIErr */
		irqWait = 0x30;  /* 等待TxIRq+RxIRq */
		break;
	default:
		irqEn = 0x00;
		irqWait = 0x00;
		break;
	}

	/* IRQInv=1 → 使能中断请求；同时设置irqEn指定的中断源 */
	writeReg(ComIEnReg, irqEn | 0x80);
	/* 清除所有中断标志 */
	clearBitMask(ComIrqReg, 0x80);
	/* 取消当前命令 */
	writeReg(CommandReg, PCD_IDLE);
	/* 清空FIFO */
	setBitMask(FIFOLevelReg, 0x80);

	/* 写入发送数据到FIFO */
	for (i = 0; i < inLen; i++)
		writeReg(FIFODataReg, inData[i]);

	/*
	 * 执行命令：
	 * 注意：不要在这里覆盖BitFramingReg，调用者已根据协议设置正确值
	 * （例如request设置0x07表示7位有效，anticoll设置0x00表示8位有效）
	 */
	writeReg(CommandReg, command);

	/* TRANSCEIVE命令需要手动触发发送（置StartSend位） */
	if (command == PCD_TRANSCEIVE)
		setBitMask(BitFramingReg, 0x80);

	/* 等待命令完成（轮询ComIrqReg） */
	i = 2000;
	do {
		irq = readReg(ComIrqReg);
		if (irq & 0x01)       /* 定时器超时IRQ */
			break;
		if (irq & irqWait)    /* 等待的IRQ已发生 */
			break;
		usleep(100);
	} while (--i);

	/* 清除StartSend位 */
	clearBitMask(BitFramingReg, 0x80);

	if (i == 0)
		return MI_ERR;

	/* 错误检查：检查ProtocolErr, ParityErr, BufferOvfl, CollErr */
	if (readReg(ErrorReg) & 0x1B)
		return MI_ERR;

	/* 读取接收数据 */
	uint8_t n = readReg(FIFOLevelReg);
	lastBits = readReg(ControlReg) & 0x07;
	if (lastBits)
		outLenBit = (n - 1) * 8 + lastBits;
	else
		outLenBit = n * 8;

	/* 缓冲区溢出保护：限制读取字节数不超过调用者缓冲区大小 */
	uint8_t maxRead = (n > 18) ? 18 : n;
	if (maxRead > outBufSize)
		maxRead = outBufSize;

	for (i = 0; i < maxRead; i++)
		outData[i] = readReg(FIFODataReg);

	/* 清理：停止定时器并返回空闲状态 */
	setBitMask(ControlReg, 0x80);
	writeReg(CommandReg, PCD_IDLE);

	return MI_OK;
}

/* ===== 寻卡 ===== */

char RC522User::request(uint8_t reqCode, uint8_t *tagType)
{
	uint8_t buf[2];
	uint32_t len = 0;

	/* 清除MFCrypto1Active标志（如果之前认证过） */
	clearBitMask(Status2Reg, 0x08);

	/* BitFramingReg=0x07: 最后字节7位有效（REQ命令规范） */
	writeReg(BitFramingReg, 0x07);
	buf[0] = reqCode;

	return communicate(PCD_TRANSCEIVE, buf, 1, tagType, 2, len);
}

/* ===== 防冲撞（获取UID） ===== */

char RC522User::anticoll(uint8_t *uid)
{
	uint8_t buf[5];
	uint8_t snr_check = 0;
	uint32_t len = 0;
	uint8_t i;

	/* BitFramingReg=0x00: 所有字节8位有效（防冲突命令规范） */
	writeReg(BitFramingReg, 0x00);
	clearBitMask(CollReg, 0x80);
	buf[0] = PICC_ANTICOLL1;
	buf[1] = 0x20;

	if (communicate(PCD_TRANSCEIVE, buf, 2, uid, 5, len) != MI_OK)
		return MI_ERR;

	/* 校验UID（4字节异或校验） */
	for (i = 0; i < 4; i++)
		snr_check ^= uid[i];
	if (snr_check != uid[4])
		return MI_ERR;

	/* 重新使能冲突检测 */
	setBitMask(CollReg, 0x80);

	return MI_OK;
}

/* ===== CRC计算 ===== */

void RC522User::calculateCRC(uint8_t *data, uint8_t len, uint8_t *crc)
{
	uint8_t i, n;

	/* 取消当前命令 */
	writeReg(CommandReg, PCD_IDLE);

	/* 清除CRCIRq中断标志（在DivIrqReg，非ComIrqReg） */
	clearBitMask(DivIrqReg, 0x04);

	/* 复位FIFO指针 */
	setBitMask(FIFOLevelReg, 0x80);

	/* 写入数据到FIFO */
	for (i = 0; i < len; i++)
		writeReg(FIFODataReg, data[i]);

	/* 执行CRC计算命令 */
	writeReg(CommandReg, PCD_CALCCRC);

	/* 等待CRC计算完成（轮询CRCIRq位，添加usleep避免忙等待） */
	i = 50;
	do {
		usleep(50);
		n = readReg(DivIrqReg);
		i--;
	} while ((i != 0) && !(n & 0x04));

	/* 读取CRC结果 */
	crc[0] = readReg(CRCResultRegL);
	crc[1] = readReg(CRCResultRegM);

	/* 恢复空闲状态 */
	writeReg(CommandReg, PCD_IDLE);
}

/* ===== 休眠 ===== */

char RC522User::halt()
{
	uint8_t buf[4];
	uint32_t len = 0;

	/* 使用预计算的HALT+CRC命令，避免每次计算CRC（节省~12次SPI操作） */
	memcpy(buf, HALT_CMD, 4);

	communicate(PCD_TRANSCEIVE, buf, 4, buf, 4, len);
	return MI_OK;
}

/* ===== 高层接口 ===== */

QString RC522User::detectCard()
{
	QString uidStr;
	uint8_t tagType[2];
	uint8_t uid[5];

	if (m_fd < 0)
		return uidStr;

	/* 寻卡 */
	if (request(PICC_REQIDL, tagType) != MI_OK)
		return uidStr;

	/* 防冲撞获取UID */
	if (anticoll(uid) != MI_OK)
		return uidStr;

	/* 格式化UID为字符串（使用snprintf避免链式QString临时对象） */
	char hex[9];
	snprintf(hex, sizeof(hex), "%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
	uidStr = QString::fromLatin1(hex);

	/* 让卡片休眠 */
	halt();

	return uidStr;
}

bool RC522User::readBlock(uint8_t block, uint8_t *data)
{
	/* TODO: 实现读块（需要从fork移植PcdRead） */
	(void)block;
	(void)data;
	return false;
}

bool RC522User::writeBlock(uint8_t block, const uint8_t *data)
{
	/* TODO: 实现写块（需要从fork移植PcdWrite） */
	(void)block;
	(void)data;
	return false;
}