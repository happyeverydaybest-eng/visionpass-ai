/*
 * VisionPass RC522用户空间封装
 *
 * 功能：封装对 /dev/rc522 字符设备的 read/write 操作
 * 提供高层接口：初始化、检测卡片、读写块
 */

#ifndef RC522USER_H
#define RC522USER_H

#include <QString>
#include <QVector>

/*
 * RC522用户空间操作类
 *
 * 使用方法：
 *   RC522User rc522;
 *   if (rc522.openDevice()) {
 *       rc522.init();
 *       QString uid = rc522.detectCard();
 *       if (!uid.isEmpty()) qDebug() << "Card:" << uid;
 *       rc522.closeDevice();
 *   }
 */
class RC522User
{
public:
	RC522User();
	~RC522User();

	/* 打开 /dev/rc522 设备 */
	bool openDevice();
	/* 关闭设备 */
	void closeDevice();

	/* 设备是否已打开 */
	bool isOpen() const;

	/*
	 * 初始化RC522（复位+配置ISO14443-A）
	 * 返回值：true=成功
	 */
	bool init();

	/*
	 * 检测卡片并返回UID
	 * 返回值：UID字符串（如 "AABBCCDD"），未检测到则返回空字符串
	 */
	QString detectCard();

	/*
	 * 读取指定块的数据
	 * 参数 block：块地址（0~63）
	 * 参数 data：输出缓冲区（16字节）
	 * 返回值：true=成功
	 */
	bool readBlock(uint8_t block, uint8_t *data);

	/*
	 * 写入指定块的数据
	 * 参数 block：块地址（0~63）
	 * 参数 data：输入缓冲区（16字节）
	 * 返回值：true=成功
	 */
	bool writeBlock(uint8_t block, const uint8_t *data);

private:
	/* 底层寄存器读写 */
	uint8_t readReg(uint8_t addr);
	void writeReg(uint8_t addr, uint8_t val);

	/* 置位/清位 */
	void setBitMask(uint8_t reg, uint8_t mask);
	void clearBitMask(uint8_t reg, uint8_t mask);

	/* 天线控制 */
	void antennaOn();
	void antennaOff();

	/* 核心通信 */
	char communicate(uint8_t command, uint8_t *inData, uint8_t inLen,
		         uint8_t *outData, uint32_t &outLenBit);

	/* 寻卡 */
	char request(uint8_t reqCode, uint8_t *tagType);
	/* 防冲撞（获取UID） */
	char anticoll(uint8_t *uid);
	/* CRC计算 */
	void calculateCRC(uint8_t *data, uint8_t len, uint8_t *crc);
	/* 选卡 */
	char select(uint8_t *uid);
	/* 认证 */
	char authState(uint8_t mode, uint8_t block, uint8_t *key, uint8_t *uid);
	/* 休眠 */
	char halt();

	int m_fd;          /* 文件描述符 */
	bool m_opened;     /* 是否已打开 */
};

#endif // RC522USER_H