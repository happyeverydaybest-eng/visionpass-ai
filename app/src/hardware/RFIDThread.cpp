/*
 * VisionPass RFID轮询线程实现
 *
 * 核心流程：
 * 1. 打开 /dev/rc522
 * 2. 初始化RC522（复位+配置ISO14443-A）
 * 3. 循环：每200ms调用detectCard()检测卡片
 * 4. 检测到卡片 → 与数据库比对 → 发射信号
 * 5. 关闭设备
 */

#include "RFIDThread.h"
#include "src/database/UserDatabase.h"
#include <QDebug>

RFIDThread::RFIDThread(UserDatabase *database, QObject *parent)
	: QThread(parent),
	  m_running(false),
	  m_pollIntervalMs(200),
	  m_database(database)
{
}

RFIDThread::~RFIDThread()
{
	stopPolling();
	wait(5000);
}

bool RFIDThread::initDevice()
{
	if (!m_rc522.openDevice()) {
		emit deviceError("无法打开/dev/rc522");
		return false;
	}

	if (!m_rc522.init()) {
		emit deviceError("RC522初始化失败");
		m_rc522.closeDevice();  /* I2: 释放fd */
		return false;
	}

	qInfo() << "RFIDThread: RC522 initialized";
	return true;
}

void RFIDThread::stopPolling()
{
	m_running = false;
}

void RFIDThread::setPollInterval(int ms)
{
	m_pollIntervalMs = qMax(100, ms);  /* 至少100ms */
}

void RFIDThread::run()
{
	if (!m_rc522.isOpen()) {
		qWarning() << "RFIDThread: RC522 not initialized";
		return;
	}

	/*
	 * 原子设置m_running：防止与stopPolling()的竞态条件
	 * 如果stopPolling()在QThread::start()后、run()开始前被调用，
	 * 这里会检测到m_running已经被设为false，直接返回
	 */
	bool expected = false;
	if (!m_running.compare_exchange_strong(expected, true)) {
		qWarning() << "RFIDThread: stopPolling was called before run() started";
		return;
	}

	qInfo() << "RFIDThread: Polling started, interval=" << m_pollIntervalMs.load() << "ms";

	QString lastUid;
	int lastUidCount = 0;  /* 连续检测到同一张卡的次数 */

	while (m_running) {
		/* 检测卡片 */
		QString uid = m_rc522.detectCard();

		if (!uid.isEmpty()) {
			/*
			 * 去抖处理：
			 * 同一张卡连续检测到2次才认为是有效的，
			 * 防止刷卡时因抖动导致的误触发
			 */
			if (uid == lastUid) {
				lastUidCount++;
			} else {
				lastUid = uid;
				lastUidCount = 1;
			}

			if (lastUidCount >= 2) {
				/* 查询数据库，检查卡片是否已授权 */
				if (m_database) {
					QString userId = m_database->findUserByCardUid(uid);
					if (!userId.isEmpty()) {
						/* 已授权卡片：获取用户姓名 */
						UserInfo user = m_database->getUserById(userId);
						QString userName = user.name.isEmpty() ? userId : user.name;
						emit cardDetected(uid, userName);
						qInfo() << "RFIDThread: Authorized card detected, UID=" << uid
						        << ", User=" << userName;
					} else {
						/* 未授权卡片 */
						emit unauthorizedCard(uid);
						qInfo() << "RFIDThread: Unauthorized card detected, UID=" << uid;
					}
				} else {
					/* 没有数据库，所有卡片视为未授权 */
					emit unauthorizedCard(uid);
					qWarning() << "RFIDThread: No database, treating card as unauthorized, UID=" << uid;
				}

				/* 等待卡片移开（避免重复读取） */
				while (m_running) {
					QString newUid = m_rc522.detectCard();
					if (newUid.isEmpty()) {
						lastUid = QString();
						lastUidCount = 0;
						break;
					}
					msleep(100);
				}
			}
		} else {
			lastUid = QString();
			lastUidCount = 0;
		}

		msleep(m_pollIntervalMs);
	}

	qInfo() << "RFIDThread: Polling stopped";
}