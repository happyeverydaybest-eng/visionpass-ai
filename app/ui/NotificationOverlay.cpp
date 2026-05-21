/*
 * VisionPass 浮动通知标签实现
 */

#include "NotificationOverlay.h"
#include <QVBoxLayout>
#include <QGraphicsOpacityEffect>

NotificationOverlay::NotificationOverlay(QWidget *parent)
	: QWidget(parent)
{
	/* 通知浮层：半透明背景 + 白色文字 */
	m_messageLabel = new QLabel(this);
	m_messageLabel->setAlignment(Qt::AlignCenter);
	m_messageLabel->setWordWrap(true);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(m_messageLabel);
	layout->setContentsMargins(20, 10, 20, 10);

	/* 固定尺寸 */
	setFixedSize(440, 50);

	/* 默认隐藏 */
	hide();

	/* 自动隐藏定时器 */
	m_autoHideTimer = new QTimer(this);
	m_autoHideTimer->setSingleShot(true);
	connect(m_autoHideTimer, &QTimer::timeout, this, &NotificationOverlay::hideMessage);
}

void NotificationOverlay::showMessage(const QString &message,
				       NotificationType type, int durationMs)
{
	/* 根据类型设置样式 */
	QString bgColor, textColor;
	switch (type) {
	case Success:
		bgColor = "#27ae60";   /* 绿色 */
		textColor = "white";
		break;
	case Error:
		bgColor = "#e74c3c";   /* 红色 */
		textColor = "white";
		break;
	case Warning:
		bgColor = "#f39c12";   /* 橙色 */
		textColor = "white";
		break;
	default:
		bgColor = "#2c3e50";
		textColor = "white";
		break;
	}

	m_messageLabel->setText(message);
	setStyleSheet(QString(
		"background-color: %1;"
		"border-radius: 8px;"
		"color: %2;"
		"font-size: 18px;"
		"font-weight: bold;"
	).arg(bgColor).arg(textColor));

	/* 居中定位在父Widget中 */
	if (parentWidget()) {
		int px = (parentWidget()->width() - width()) / 2;
		int py = (parentWidget()->height() - height()) / 2;
		move(px, py);
	}

	show();
	raise();  /* 确保浮在最上层 */

	/* 启动自动隐藏 */
	m_autoHideTimer->start(durationMs);
}

void NotificationOverlay::hideMessage()
{
	hide();
}