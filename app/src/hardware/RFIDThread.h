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

/* 前向声明 */
class UserDatabase;

class RFIDThread : public QThread
{
	Q_OBJECT

public:
	/*
	 * 构造函数
	 * 参数 database：用户数据库指针，用于查询卡片UID对应的用户信息
	 *             如果为nullptr，则所有卡片都被视为未授权
	 */
	explicit RFIDThread(UserDatabase *database = nullptr, QObject *parent = nullptr);
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
	UserDatabase *m_database;
	std::atomic<bool> m_running;
	std::atomic<int> m_pollIntervalMs;  /* I4: 使用原子操作 */
};

#endif // RFIDTHREAD_H