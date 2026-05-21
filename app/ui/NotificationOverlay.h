/*
 * VisionPass 浮动通知标签
 *
 * 显示在视频区中央的通知消息，自动隐藏。
 * 三种样式：成功(绿色)、错误(红色)、警告(橙色)。
 */

#ifndef NOTIFICATIONOVERLAY_H
#define NOTIFICATIONOVERLAY_H

#include <QWidget>
#include <QLabel>
#include <QTimer>

class NotificationOverlay : public QWidget
{
	Q_OBJECT

public:
	enum NotificationType {
		Success,  /* 绿色背景 */
		Error,    /* 红色背景 */
		Warning   /* 橙色背景 */
	};

	explicit NotificationOverlay(QWidget *parent = nullptr);

	/* 显示通知消息，durationMs后自动隐藏 */
	void showMessage(const QString &message, NotificationType type = Success,
		     int durationMs = 3000);

	/* 立即隐藏通知 */
	void hideMessage();

private:
	QLabel *m_messageLabel;
	QTimer *m_autoHideTimer;
};

#endif // NOTIFICATIONOVERLAY_H