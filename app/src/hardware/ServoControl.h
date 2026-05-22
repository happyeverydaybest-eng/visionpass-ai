/*
 * VisionPass 舵机控制（SG90）
 *
 * 通过ioctl调用/dev/servo驱动控制舵机角度
 * - unlock(): 转到90度（开锁）
 * - lock(): 转到0度（关锁）
 *
 * ioctl命令与 drivers/servo/servo.c 一致：
 *   SERVO_SET_ANGLE = _IOW('S', 0, int)  // 设置角度0~180
 *   SERVO_STOP      = _IO('S', 1)        // 停止PWM
 */

#ifndef SERVOCONTROL_H
#define SERVOCONTROL_H

#include <QObject>

/* ioctl命令定义（必须与内核驱动servo.c保持一致） */
#include <sys/ioctl.h>
#define SERVO_SET_ANGLE  _IOW('S', 0, int)
#define SERVO_STOP       _IO('S', 1)

class ServoControl : public QObject
{
	Q_OBJECT

public:
	explicit ServoControl(QObject *parent = nullptr);
	~ServoControl();

	/* 打开/dev/servo设备 */
	bool openDevice();
	/* 关闭设备（先停止PWM再关闭fd） */
	void closeDevice();
	/* 设备是否已打开 */
	bool isOpen() const;

	/* 开锁：舵机转到90度 */
	bool unlock();
	/* 关锁：舵机转到0度 */
	bool lock();
	/* 设置任意角度（0~180） */
	bool setAngle(int angle);

signals:
	/* 开锁完成 */
	void unlocked();
	/* 关锁完成 */
	void locked();
	/* 设备错误 */
	void deviceError(const QString &error);

private:
	int m_fd;  /* /dev/servo文件描述符 */
};

#endif // SERVOCONTROL_H