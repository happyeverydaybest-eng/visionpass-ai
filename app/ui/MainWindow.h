/*
 * VisionPass 主窗口（1024x600 LCD）
 *
 * 界面布局：
 * =========
 * 左侧：视频显示区（844x590）
 * 右侧：5个开锁按钮栏（180px宽）
 * 浮层：状态LED、时间、通知消息
 *
 * 设计原则：
 * - MainWindow只管UI，不直接操作硬件
 * - 所有硬件操作通过SystemController的信号/槽
 * - 深色科技风配色（#2c3e50背景）
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include "src/controller/SystemController.h"
#include "src/controller/SystemState.h"
#include "ui/NotificationOverlay.h"

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(SystemController *controller, QWidget *parent = nullptr);
	~MainWindow();

private slots:
	/* ===== 按钮点击处理 ===== */
	void onFaceUnlockClicked();
	void onCardUnlockClicked();
	void onPasswordUnlockClicked();
	void onVoiceUnlockClicked();
	void onAdminClicked();

	/* ===== SystemController信号处理 ===== */
	void onStateChanged(SystemState newState);
	void onFrameReady(const QImage &frame);
	void onFaceRecognized(const RecognizeResult &result);
	void onCardDetected(const QString &userId, const QString &userName);
	void onUnauthorizedCard(const QString &cardId);
	void onDoorUnlocked();
	void onDoorLocked();
	void onNotification(const QString &message, SystemState contextState);

	/* ===== 定时更新 ===== */
	void updateTimeDisplay();

private:
	/* 初始化UI布局 */
	void initLayout();
	/* 初始化信号连接 */
	void initConnections();
	/* 根据状态更新按钮启用/禁用 */
	void updateButtonsForState(SystemState state);
	/* 设置状态LED颜色 */
	void setStatusIndicator(const QString &color);

	/* SystemController指针（不拥有） */
	SystemController *m_controller;

	/* ===== UI元素 ===== */

	/* 视频显示区 */
	QLabel *m_videoLabel;

	/* 右侧按钮 */
	QPushButton *m_faceButton;
	QPushButton *m_cardButton;
	QPushButton *m_passwordButton;
	QPushButton *m_voiceButton;
	QPushButton *m_adminButton;

	/* 状态LED */
	QLabel *m_statusIndicator;

	/* 时间显示 */
	QLabel *m_timeLabel;
	QTimer *m_timeTimer;

	/* 通知浮层 */
	NotificationOverlay *m_notifyOverlay;

	/* 当前显示的视频帧（用于人脸框绘制） */
	QImage m_currentFrame;
};

#endif // MAINWINDOW_H