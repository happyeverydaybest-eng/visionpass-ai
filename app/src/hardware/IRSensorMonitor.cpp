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

IRSensorMonitor::IRSensorMonitor(QObject *parent)
	: QObject(parent),
	  m_device(nullptr),
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

	/* 释放设备文件对象（m_pollTimer由Qt父对象自动管理，无需手动删除） */
	delete m_device;
	m_device = nullptr;
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

	/* 打开设备文件 */
	m_device = new QFile("/dev/ir_sensor");
	if (!m_device->open(QIODevice::ReadOnly | QIODevice::Text)) {
		qWarning() << "IRSensorMonitor: Cannot open /dev/ir_sensor:" << m_device->errorString();
		emit deviceError("无法打开IR传感器设备");
		delete m_device;
		m_device = nullptr;
		return false;
	}

	/* 读取初始状态（避免首次poll时误触发信号） */
	m_device->seek(0);
	QByteArray data = m_device->readLine().trimmed();
	m_lastState = (data == "1");
	qInfo() << "IRSensorMonitor: Initial state =" << (m_lastState ? "person detected" : "no person");

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

	if (m_device && m_device->isOpen()) {
		m_device->close();
	}

	/* 释放设备文件对象，防止内存泄漏 */
	delete m_device;
	m_device = nullptr;

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
	if (!m_device || !m_device->isOpen())
		return;

	/* seek(0)回到文件开头，因为字符设备的read每次从当前位置读取 */
	m_device->seek(0);
	QByteArray data = m_device->readLine().trimmed();

	/* 解析状态 */
	bool currentState = (data == "1");

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