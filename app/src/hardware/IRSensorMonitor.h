/*
 * VisionPass IR传感器监控
 *
 * 功能：监控 /dev/ir_sensor 的门状态变化
 * - 检测到人靠近 → 自动触发人脸扫描
 * - 门关闭后 → 自动关锁
 *
 * 使用QTimer轮询（每500ms），不使用QThread
 * 因为poll()需要独立线程，而QTimer在主事件循环中更简单
 */

#ifndef IRSENSORMONITOR_H
#define IRSENSORMONITOR_H

#include <QObject>
#include <QTimer>

class IRSensorMonitor : public QObject
{
	Q_OBJECT

public:
	explicit IRSensorMonitor(QObject *parent = nullptr);
	~IRSensorMonitor();

	/* 启动监控（创建轮询定时器） */
	bool start();
	/* 停止监控 */
	void stop();
	/* 当前门状态：true = 检测到有人 / 门打开 */
	bool isPersonDetected() const;

signals:
	/* 检测到有人靠近门 */
	void personDetected();
	/* 人离开 / 门关闭 */
	void personLeft();
	/* 设备错误 */
	void deviceError(const QString &error);

private slots:
	/* 定时器触发的轮询函数 */
	void poll();

private:
	int m_fd;              /* /dev/ir_sensor 文件描述符 */
	QTimer *m_pollTimer;   /* 轮询定时器 */
	bool m_lastState;      /* 上一次的传感器状态 */
	bool m_monitoring;     /* 是否正在监控 */
};

#endif // IRSENSORMONITOR_H