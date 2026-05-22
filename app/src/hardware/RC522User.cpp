/*
 * VisionPass RC522用户空间封装实现
 *
 * 参考原版 rc522.cpp，但改为面向对象封装：
 * - fd 从全局变量改为成员变量
 * - 添加错误处理和日志
 * - 线程安全（每个RC522User实例独立管理fd）
 */

#include "RC522User.h"
#include <QDebug>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

/* RC522寄存器地址 */
static const uint8_t CommandReg   = 0x01;
static const uint8_t ComIEnReg    = 0x02;  /* 中断使能寄存器 */
static const uint8_t ComIrqReg    = 0x04;
static const uint8_t ErrorReg     = 0x06;
static const uint8_t Status2Reg   = 0x08;
static const uint8_t FIFODataReg  = 0x09;
static const uint8_t FIFOLevelReg = 0x0A;
static const uint8_t BitFramingReg = 0x0D;
static const uint8_t CollReg      = 0x0E;
static const uint8_t ModeReg      = 0x11;
static const uint8_t TxControlReg = 0x14;
static const uint8_t TxAutoReg    = 0x15;
static const uint8_t RxSelReg     = 0x17;
static const uint8_t RFCfgReg     = 0x26;
static const uint8_t ControlReg   = 0x0C;
static const uint8_t TModeReg     = 0x2A;
static const uint8_t TPrescalerReg = 0x2B;
static const uint8_t TReloadRegH  = 0x2C;
static const uint8_t TReloadRegL  = 0x2D;

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
static const char MI_NOTAGERR = -2;

RC522User::RC522User()
	: m_fd(-1), m_opened(false)
{
}

RC522User::~RC522User()
{
	closeDevice();
}

bool RC522User::openDevice()
{
	if (m_opened)
		return true;

	m_fd = ::open("/dev/rc522", O_RDWR);
	if (m_fd < 0) {
		qWarning() << "RC522: Cannot open /dev/rc522:" << strerror(errno);
		return false;
	}

	m_opened = true;
	qInfo() << "RC522: Device opened";
	return true;
}

void RC522User::closeDevice()
{
	if (m_fd >= 0) {
		::close(m_fd);
		m_fd = -1;
	}
	m_opened = false;
}

bool RC522User::isOpen() const
{
	return m_opened;
}

/* ===== 底层寄存器读写 ===== */

uint8_t RC522User::readReg(uint8_t addr)
{
	uint8_t buf = addr;
	/* read() 接口：buf[0]传入地址，返回时buf[0]为寄存器值 */
	::read(m_fd, &buf, 1);
	return buf;
}

void RC522User::writeReg(uint8_t addr, uint8_t val)
{
	uint8_t buf[2] = {addr, val};
	::write(m_fd, buf, 2);
}

void RC522User::setBitMask(uint8_t reg, uint8_t mask)
{
	uint8_t tmp = readReg(reg);
	writeReg(reg, tmp | mask);
}

void RC522User::clearBitMask(uint8_t reg, uint8_t mask)
{
	uint8_t tmp = readReg(reg);
	writeReg(reg, tmp & (~mask));
}

void RC522User::antennaOn()
{
	uint8_t tmp = readReg(TxControlReg);
	if (!(tmp & 0x03))
		setBitMask(TxControlReg, 0x03);
}

void RC522User::antennaOff()
{
	clearBitMask(TxControlReg, 0x03);
}

/* ===== 初始化 ===== */

bool RC522User::init()
{
	if (!m_opened) {
		qWarning() << "RC522: Device not opened";
		return false;
	}

	/* 软复位 */
	writeReg(CommandReg, PCD_RESETPHASE);
	usleep(50000);  /* 50ms */

	/* 配置定时器 */
	writeReg(TModeReg, 0x8D);
	writeReg(TPrescalerReg, 0x3E);
	writeReg(TReloadRegH, 0x00);
	writeReg(TReloadRegL, 0x1E);  /* 30, 而非0x1F=31 */

	/* 配置TxAutoReg: 100% ASK调制 */
	writeReg(TxAutoReg, 0x40);

	/* 配置ModeReg */
	writeReg(ModeReg, 0x3D);

	/* 配置接收灵敏度 */
	writeReg(RxSelReg, 0x86);

	/* 配置RF增益 */
	writeReg(RFCfgReg, 0x7F);

	/* 开启天线 */
	antennaOn();

	/* 验证初始化：读取VersionReg */
	uint8_t version = readReg(0x37);
	qInfo() << "RC522: VersionReg =" << QString("0x%1").arg(version, 2, 16, QChar('0'));

	return true;
}

/* ===== 核心通信 ===== */

char RC522User::communicate(uint8_t command, uint8_t *inData, uint8_t inLen,
			    uint8_t *outData, uint32_t &outLenBit)
{
	uint8_t irq = 0x00;
	uint8_t irqWait = 0x00;
	uint8_t lastBits = 0;
	uint32_t i = 0;

	if (command == PCD_AUTHENT) {
		irqWait = 0x10;
	} else if (command == PCD_TRANSCEIVE) {
		irqWait = 0x30;
	}

	writeReg(ComIEnReg, irqWait | 0x80);  /* C1: 写入中断使能寄存器 */
	clearBitMask(ComIrqReg, 0x80);
	writeReg(CommandReg, PCD_IDLE);
	setBitMask(FIFOLevelReg, 0x80);

	/* 写入发送数据 */
	for (i = 0; i < inLen; i++)
		writeReg(FIFODataReg, inData[i]);

	/* 启动命令 */
	writeReg(BitFramingReg, inLen == 0 ? 0x00 : 0x07);
	writeReg(CommandReg, command);

	if (command == PCD_TRANSCEIVE)
		setBitMask(BitFramingReg, 0x80);

	/* 等待命令完成 */
	i = 2000;
	do {
		irq = readReg(ComIrqReg);
		if (irq & 0x01)
			break;
		if (irq & irqWait)
			break;
		usleep(100);
	} while (--i);

	clearBitMask(BitFramingReg, 0x80);

	if (i == 0)
		return MI_ERR;

	/* C2: 错误检查 - 如果有任何错误位被设置则返回错误 */
	if (readReg(ErrorReg) & 0x1B)
		return MI_ERR;

	/* 读取接收数据 */
	uint8_t n = readReg(FIFOLevelReg);
	lastBits = readReg(ControlReg) & 0x07;
	if (lastBits)
		outLenBit = (n - 1) * 8 + lastBits;  /* I6: 修正位长度计算 */
	else
		outLenBit = n * 8;

	if (n == 0)
		n = 1;
	if (n > 18)
		n = 18;

	for (i = 0; i < n; i++)
		outData[i] = readReg(FIFODataReg);

	/* I3: 清理 - 停止定时器并返回空闲状态 */
	setBitMask(ControlReg, 0x80);
	writeReg(CommandReg, PCD_IDLE);

	return MI_OK;
}

/* ===== 寻卡 ===== */

char RC522User::request(uint8_t reqCode, uint8_t *tagType)
{
	uint8_t buf[2];
	uint32_t len = 0;

	/* C4: 清除MFCrypto1Active标志 */
	clearBitMask(Status2Reg, 0x08);

	writeReg(BitFramingReg, 0x07);
	buf[0] = reqCode;

	return communicate(PCD_TRANSCEIVE, buf, 1, tagType, len);
}

/* ===== 防冲撞（获取UID） ===== */

char RC522User::anticoll(uint8_t *uid)
{
	uint8_t buf[5];
	uint8_t snr_check = 0;
	uint32_t len = 0;
	uint8_t i;

	writeReg(BitFramingReg, 0x00);
	clearBitMask(CollReg, 0x80);
	buf[0] = PICC_ANTICOLL1;
	buf[1] = 0x20;

	if (communicate(PCD_TRANSCEIVE, buf, 2, uid, len) != MI_OK)
		return MI_ERR;

	/* 校验UID（4字节异或校验） */
	for (i = 0; i < 4; i++)
		snr_check ^= uid[i];
	if (snr_check != uid[4])
		return MI_ERR;

	/* I5: 重新使能冲突检测 */
	setBitMask(CollReg, 0x80);

	return MI_OK;
}

/* ===== CRC计算 ===== */

void RC522User::calculateCRC(uint8_t *data, uint8_t len, uint8_t *crc)
{
	uint8_t i, n;

	/* 清除CRC中断标志 */
	clearBitMask(ComIrqReg, 0x04);

	/* 复位FIFO指针 */
	setBitMask(FIFOLevelReg, 0x80);

	/* 写入数据到FIFO */
	for (i = 0; i < len; i++)
		writeReg(FIFODataReg, data[i]);

	/* 执行CRC计算命令 */
	writeReg(CommandReg, PCD_CALCCRC);

	/* 等待计算完成 */
	i = 255;
	do {
		n = readReg(ComIrqReg);
		i--;
	} while ((i != 0) && !(n & 0x04));

	/* 读取CRC结果 */
	crc[0] = readReg(0x22);  /* CRCResultRegL */
	crc[1] = readReg(0x21);  /* CRCResultRegM */

	/* 恢复空闲状态 */
	writeReg(CommandReg, PCD_IDLE);
}

/* ===== 休眠 ===== */

char RC522User::halt()
{
	uint8_t buf[4];
	uint32_t len = 0;

	buf[0] = PICC_HALT;
	buf[1] = 0x00;

	/* C5: 计算并添加CRC */
	calculateCRC(buf, 2, &buf[2]);

	communicate(PCD_TRANSCEIVE, buf, 4, buf, len);
	return MI_OK;
}

/* ===== 高层接口 ===== */

QString RC522User::detectCard()
{
	QString uidStr;
	uint8_t tagType[2];
	uint8_t uid[5];

	if (!m_opened)
		return uidStr;

	/* 寻卡 */
	if (request(PICC_REQIDL, tagType) != MI_OK)
		return uidStr;

	/* 防冲撞获取UID */
	if (anticoll(uid) != MI_OK)
		return uidStr;

	/* 格式化UID为字符串 */
	uidStr = QString("%1%2%3%4")
		.arg(uid[0], 2, 16, QChar('0')).toUpper()
		.arg(uid[1], 2, 16, QChar('0')).toUpper()
		.arg(uid[2], 2, 16, QChar('0')).toUpper()
		.arg(uid[3], 2, 16, QChar('0')).toUpper();

	/* 让卡片休眠 */
	halt();

	return uidStr;
}

bool RC522User::readBlock(uint8_t block, uint8_t *data)
{
	/* TODO: 实现读块 */
	(void)block;
	(void)data;
	return false;
}

bool RC522User::writeBlock(uint8_t block, const uint8_t *data)
{
	/* TODO: 实现写块 */
	(void)block;
	(void)data;
	return false;
}