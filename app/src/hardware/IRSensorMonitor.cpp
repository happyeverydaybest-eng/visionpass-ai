/*
 * VisionPass IR传感器监控实现
 *
 * 工作原理：
 * - 使用QTimer每500ms读取一次 /dev/ir_sensor
 * - 设备返回 "0\n" 表示无人（门关闭），"1\n" 表示有人（门打开）
 * - 比较当前状态与上次状态，只在变化时发射信号
 */

#include "IRSensorMonitor.h"
#include <QDebug>
#include <fcntl.h>
#include <unistd.h>

IRSensorMonitor::IRSensorMonitor(QObject *parent)
	: QObject(parent),
	  m_fd(-1),
	  m_pollTimer(nullptr),
	  m_lastState(false),
	  m_monitoring(false)
{
	/* 创建轮询定时器（生命周期由Qt父对象管理） */
	m_pollTimer = new QTimer(this);
	connect(m_pollTimer, &QTimer::timeout, this, &IRSensorMonitor::poll);
}

IRSensorMonitor::~IRSensorMonitor()
{
	stop();
}

/*
 * 启动监控
 * 1. 打开 /dev/ir_sensor
 * 2. 读取初始状态
 * 3. 启动500ms轮询定时器
 */
bool IRSensorMonitor::start()
{
	if (m_monitoring)
		return true;

	/* 打开设备文件（字符设备，不需要seek） */
	m_fd = ::open("/dev/ir_sensor", O_RDONLY);
	if (m_fd < 0) {
		qWarning() << "IRSensorMonitor: Cannot open /dev/ir_sensor:" << strerror(errno);
		emit deviceError("无法打开IR传感器设备");
		return false;
	}

	/* 读取初始状态（避免首次poll时误触发信号） */
	char buf[8];
	ssize_t n = ::read(m_fd, buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = '\0';
		m_lastState = (buf[0] == '1');
		qInfo() << "IRSensorMonitor: Initial state =" << (m_lastState ? "person detected" : "no person");
	} else {
		qWarning() << "IRSensorMonitor: Failed to read initial state";
		m_lastState = false;
	}

	/* 启动轮询定时器 */
	m_pollTimer->start(500);  /* 每500ms轮询一次 */

	m_monitoring = true;
	qInfo() << "IRSensorMonitor: Started";
	return true;
}

void IRSensorMonitor::stop()
{
	if (m_pollTimer) {
		m_pollTimer->stop();
	}

	if (m_fd >= 0) {
		::close(m_fd);
		m_fd = -1;
	}

	m_monitoring = false;
}

bool IRSensorMonitor::isPersonDetected() const
{
	return m_lastState;
}

/*
 * 轮询函数（由QTimer触发）
 *
 * 读取 /dev/ir_sensor 的当前值：
 * - "0" 表示无人（门关闭）
 * - "1" 表示有人（门打开）
 *
 * 只在状态变化时发射信号，避免重复通知
 */
void IRSensorMonitor::poll()
{
	if (m_fd < 0)
		return;

	/* 读取字符设备（每次从头读取，字符设备会自动重置位置） */
	char buf[8];
	ssize_t n = ::read(m_fd, buf, sizeof(buf) - 1);
	if (n <= 0) {
		qWarning() << "IRSensorMonitor: Failed to read from device";
		return;
	}
	buf[n] = '\0';

	/* 解析状态（字符设备返回 "0\n" 或 "1\n"） */
	bool currentState = (buf[0] == '1');

	/* 只在状态变化时发射信号 */
	if (currentState != m_lastState) {
		m_lastState = currentState;

		if (currentState) {
			qInfo() << "IRSensorMonitor: Person detected";
			emit personDetected();
		} else {
			qInfo() << "IRSensorMonitor: Person left";
			emit personLeft();
		}
	}
}