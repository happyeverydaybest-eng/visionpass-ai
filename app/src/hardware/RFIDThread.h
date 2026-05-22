/*
 * VisionPass RFID轮询线程
 *
 * 功能：每200ms轮询/dev/rc522，检测Mifare卡片
 * 检测到卡片后，与数据库比对UID，发射信号
 */

#ifndef RFIDTHREAD_H
#define RFIDTHREAD_H

#include <QThread>
#include <atomic>
#include "src/hardware/RC522User.h"

class RFIDThread : public QThread
{
	Q_OBJECT

public:
	explicit RFIDThread(QObject *parent = nullptr);
	~RFIDThread();

	/* 初始化RC522设备 */
	bool initDevice();

	/* 请求停止轮询 */
	void stopPolling();

	/* 设置轮询间隔（毫秒） */
	void setPollInterval(int ms);

signals:
	/* 检测到已授权卡片 */
	void cardDetected(const QString &uid, const QString &userName);
	/* 检测到未授权卡片 */
	void unauthorizedCard(const QString &uid);
	/* 设备错误 */
	void deviceError(const QString &error);

protected:
	void run() override;

private:
	RC522User m_rc522;
	std::atomic<bool> m_running;
	std::atomic<int> m_pollIntervalMs;  /* I4: 使用原子操作 */
};

#endif // RFIDTHREAD_H