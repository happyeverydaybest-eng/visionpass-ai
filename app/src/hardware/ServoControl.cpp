/*
 * VisionPass 舵机控制实现
 *
 * 通过ioctl系统调用控制SG90舵机：
 * - ioctl(fd, SERVO_SET_ANGLE, angle) 设置角度
 * - ioctl(fd, SERVO_STOP) 停止PWM
 */

#include "ServoControl.h"
#include <QDebug>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

ServoControl::ServoControl(QObject *parent)
	: QObject(parent), m_fd(-1)
{
}

ServoControl::~ServoControl()
{
	closeDevice();
}

bool ServoControl::openDevice()
{
	if (m_fd >= 0)
		return true;

	m_fd = ::open("/dev/servo", O_RDWR);
	if (m_fd < 0) {
		qWarning() << "ServoControl: Cannot open /dev/servo:" << strerror(errno);
		emit deviceError("无法打开舵机设备");
		return false;
	}

	qInfo() << "ServoControl: Device opened";
	return true;
}

void ServoControl::closeDevice()
{
	if (m_fd >= 0) {
		/* 关闭前先停止PWM，释放GPIO */
		::ioctl(m_fd, SERVO_STOP);
		::close(m_fd);
		m_fd = -1;
		qInfo() << "ServoControl: Device closed";
	}
}

bool ServoControl::isOpen() const
{
	return m_fd >= 0;
}

/*
 * 设置舵机角度
 * 角度范围：0~180度
 * SG90舵机：0度=关闭位置，90度=中间位置，180度=全开位置
 * 返回值：true=成功，false=失败
 */
bool ServoControl::setAngle(int angle)
{
	if (m_fd < 0) {
		qWarning() << "ServoControl: Device not opened";
		return false;
	}

	/* 限制角度范围 */
	if (angle < 0) angle = 0;
	if (angle > 180) angle = 180;

	/*
	 * ioctl第三个参数是角度值（直接传int，不是指针）
	 * 驱动内部将角度转换为PWM脉宽
	 */
	if (::ioctl(m_fd, SERVO_SET_ANGLE, angle) < 0) {
		qWarning() << "ServoControl: ioctl SERVO_SET_ANGLE failed:" << strerror(errno);
		emit deviceError("舵机控制失败");
		return false;
	}

	qInfo() << "ServoControl: Angle set to" << angle;
	return true;
}

bool ServoControl::unlock()
{
	if (setAngle(90)) {  /* 90度 = 开锁位置 */
		emit unlocked();
		return true;
	}
	return false;
}

bool ServoControl::lock()
{
	if (setAngle(0)) {   /* 0度 = 关锁位置 */
		emit locked();
		return true;
	}
	return false;
}