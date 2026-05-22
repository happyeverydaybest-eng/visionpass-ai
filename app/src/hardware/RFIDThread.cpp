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
#include <QDebug>

RFIDThread::RFIDThread(QObject *parent)
	: QThread(parent),
	  m_running(false),
	  m_pollIntervalMs(200)
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

	m_running = true;
	qInfo() << "RFIDThread: Polling started, interval=" << m_pollIntervalMs << "ms";

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
				/* TODO: 从数据库查询UID对应的用户信息 */
				/* 暂时直接发射未授权信号（等数据库实现后再改） */
				emit unauthorizedCard(uid);
				qInfo() << "RFIDThread: Card detected, UID=" << uid;

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