/*
 * VisionPass 主窗口实现
 *
 * 1024x600 LCD布局：
 *   左侧视频区(844x590) + 右侧按钮栏(180px)
 *   浮层：状态LED(20x20)、时间(160x24)、通知(440x50)
 */

#include "MainWindow.h"
#include "PasswordDialog.h"
#include "MessageDialog.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTime>
#include <QPixmap>
#include <QPainter>
#include <QDebug>
#include <QApplication>
#include <QFile>
#include <QProcess>

/* 按钮尺寸常量 */
static const int BUTTON_WIDTH = 160;
static const int BUTTON_HEIGHT = 52;
static const int BUTTON_SPACING = 12;

MainWindow::MainWindow(SystemController *controller, QWidget *parent)
	: QMainWindow(parent), m_controller(controller)
{
	/* 固定窗口大小（嵌入式LCD，不允许调整大小） */
	setFixedSize(1024, 600);
	setWindowTitle("VisionPass AI门禁系统");

	initLayout();
	initConnections();

	/* 时间更新定时器（每秒刷新） */
	m_timeTimer = new QTimer(this);
	connect(m_timeTimer, &QTimer::timeout, this, &MainWindow::updateTimeDisplay);
	m_timeTimer->start(1000);
	updateTimeDisplay();
}

MainWindow::~MainWindow()
{
}

void MainWindow::initLayout()
{
	/* 主窗口背景：深色科技风渐变 */
	setStyleSheet(
		"QMainWindow { background-color: #2c3e50; }"
	);

	/* ===== 创建中心Widget和主布局 ===== */
	QWidget *centralWidget = new QWidget(this);
	setCentralWidget(centralWidget);

	QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
	mainLayout->setContentsMargins(5, 5, 5, 5);
	mainLayout->setSpacing(5);

	/* ===== 左侧：视频显示区 ===== */
	m_videoLabel = new QLabel(centralWidget);
	m_videoLabel->setMinimumSize(844, 590);
	m_videoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_videoLabel->setAlignment(Qt::AlignCenter);
	m_videoLabel->setStyleSheet(
		"QLabel { background-color: #1a252f; border: 2px solid #34495e; "
		"border-radius: 4px; color: #7f8c8d; font-size: 24px; }"
	);
	m_videoLabel->setText("摄像头未启动");

	/* 视频区内部浮层容器 */
	QWidget *videoContainer = new QWidget(centralWidget);
	QVBoxLayout *videoLayout = new QVBoxLayout(videoContainer);
	videoLayout->setContentsMargins(0, 0, 0, 0);
	videoLayout->addWidget(m_videoLabel);

	/* ===== 状态LED（浮在视频左下角） ===== */
	m_statusIndicator = new QLabel(m_videoLabel);
	m_statusIndicator->setFixedSize(20, 20);
	m_statusIndicator->move(12, m_videoLabel->height() - 32);
	m_statusIndicator->setStyleSheet(
		"QLabel { background-color: #e74c3c; border-radius: 10px; "
		"border: 2px solid #c0392b; }"
	);

	/* ===== 时间显示（浮在LED右边） ===== */
	m_timeLabel = new QLabel(m_videoLabel);
	m_timeLabel->setFixedSize(160, 24);
	m_timeLabel->move(40, m_videoLabel->height() - 30);
	m_timeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	m_timeLabel->setStyleSheet(
		"QLabel { color: #ecf0f1; font-size: 14px; "
		"background-color: transparent; }"
	);

	/* ===== 通知浮层 ===== */
	m_notifyOverlay = new NotificationOverlay(m_videoLabel);

	/* ===== 右侧：按钮栏 ===== */
	QWidget *rightPanel = new QWidget(centralWidget);
	rightPanel->setFixedWidth(180);
	rightPanel->setStyleSheet("QWidget { background-color: transparent; }");

	QVBoxLayout *buttonLayout = new QVBoxLayout(rightPanel);
	buttonLayout->setContentsMargins(10, 0, 10, 0);
	buttonLayout->setSpacing(BUTTON_SPACING);
	buttonLayout->addStretch();  /* 上方弹性空间，按钮居中 */

	/* 开锁按钮 + 消息按钮 */
	m_faceButton = new QPushButton("人脸开锁", rightPanel);
	m_cardButton = new QPushButton("刷卡开锁", rightPanel);
	m_passwordButton = new QPushButton("密码开锁", rightPanel);
	m_voiceButton = new QPushButton("语音开锁", rightPanel);
	m_messageButton = new QPushButton("发送消息", rightPanel);
	m_exitButton = new QPushButton("退出系统", rightPanel);

	/* 按钮固定尺寸 */
	QList<QPushButton*> buttons = {
		m_faceButton, m_cardButton, m_passwordButton,
		m_voiceButton, m_messageButton
	};

	/* 每个按钮的配色和样式 */
	QStringList colors = {
		"#3498db",   /* 人脸：蓝色 */
		"#f39c12",   /* 刷卡：橙色 */
		"#27ae60",   /* 密码：绿色 */
		"#9b59b6",   /* 语音：紫色 */
		"#1abc9c"    /* 消息：青色 */
	};

	for (int i = 0; i < buttons.size(); i++) {
		QPushButton *btn = buttons[i];
		btn->setFixedSize(BUTTON_WIDTH, BUTTON_HEIGHT);
		QString color = colors[i];

		/* 深色科技风格按钮：渐变背景 + 圆角 + 白色文字 */
		btn->setStyleSheet(QString(
			"QPushButton {"
			"  background-color: %1;"
			"  color: white;"
			"  border: none;"
			"  border-radius: 8px;"
			"  font-size: 16px;"
			"  font-weight: bold;"
			"  padding: 8px;"
			"}"
			"QPushButton:hover { background-color: %1; }"
			"QPushButton:pressed { background-color: #2c3e50; }"
			"QPushButton:disabled { background-color: #7f8c8d; color: #bdc3c7; }"
		).arg(color));

		buttonLayout->addWidget(btn);
	}

	/* 退出按钮：红色，与开锁按钮区分 */
	m_exitButton->setFixedSize(BUTTON_WIDTH, BUTTON_HEIGHT);
	m_exitButton->setStyleSheet(
		"QPushButton {"
		"  background-color: #c0392b;"
		"  color: white;"
		"  border: none;"
		"  border-radius: 8px;"
		"  font-size: 16px;"
		"  font-weight: bold;"
		"  padding: 8px;"
		"}"
		"QPushButton:hover { background-color: #e74c3c; }"
		"QPushButton:pressed { background-color: #96281b; }"
	);

	buttonLayout->addStretch();  /* 开锁按钮与退出按钮之间的弹性空间 */
	buttonLayout->addWidget(m_exitButton);
	buttonLayout->addSpacing(5); /* 底部留白 */

	/* 组装主布局 */
	mainLayout->addWidget(videoContainer, 1);   /* stretch=1,占据剩余空间 */
	mainLayout->addWidget(rightPanel);

	/* 预存视频区目标尺寸（避免每次查询布局） */
	m_videoTargetSize = m_videoLabel->size();
}

void MainWindow::initConnections()
{
	/* ===== 按钮信号 → MainWindow槽 ===== */
	connect(m_faceButton, &QPushButton::clicked,
		this, &MainWindow::onFaceUnlockClicked);
	connect(m_cardButton, &QPushButton::clicked,
		this, &MainWindow::onCardUnlockClicked);
	connect(m_passwordButton, &QPushButton::clicked,
		this, &MainWindow::onPasswordUnlockClicked);
	connect(m_voiceButton, &QPushButton::clicked,
		this, &MainWindow::onVoiceUnlockClicked);
	connect(m_exitButton, &QPushButton::clicked,
		this, &MainWindow::onExitClicked);
	connect(m_messageButton, &QPushButton::clicked,
		this, &MainWindow::onMessageClicked);

	/* ===== SystemController信号 → MainWindow槽 ===== */
	connect(m_controller, &SystemController::stateChanged,
		this, &MainWindow::onStateChanged);
	connect(m_controller, &SystemController::frameReady,
		this, &MainWindow::onFrameReady);
	connect(m_controller, &SystemController::faceRecognized,
		this, &MainWindow::onFaceRecognized);
	connect(m_controller, &SystemController::cardDetected,
		this, &MainWindow::onCardDetected);
	connect(m_controller, &SystemController::unauthorizedCard,
		this, &MainWindow::onUnauthorizedCard);
	connect(m_controller, &SystemController::doorUnlocked,
		this, &MainWindow::onDoorUnlocked);
	connect(m_controller, &SystemController::doorLocked,
		this, &MainWindow::onDoorLocked);
	connect(m_controller, &SystemController::notification,
		this, &MainWindow::onNotification);
	connect(m_controller, &SystemController::messageReceived,
		this, &MainWindow::onMessageReceived);
}

/* ===== 按钮点击处理 ===== */

void MainWindow::onFaceUnlockClicked()
{
	m_controller->startFaceRecognition();
}

void MainWindow::onCardUnlockClicked()
{
	m_controller->startCardReading();
}

void MainWindow::onPasswordUnlockClicked()
{
	/* 创建并显示密码输入对话框 */
	PasswordDialog *dialog = new PasswordDialog(this);

	/* 连接信号：用户确认密码后，传递给 SystemController 验证 */
	connect(dialog, &PasswordDialog::passwordConfirmed,
		m_controller, &SystemController::submitPassword);

	/* 以模态方式显示对话框（exec会阻塞直到对话框关闭） */
	dialog->exec();

	/* 对话框关闭后自动删除 */
	dialog->deleteLater();
}

void MainWindow::onVoiceUnlockClicked()
{
	m_controller->startVoiceRecognition();
}

void MainWindow::onExitClicked()
{
	/*
	 * 退出系统：
	 * 先确认对话框防止误触，然后关闭应用程序。
	 * QApplication::quit() 会退出事件循环，main() 中的 app.exec() 返回。
	 */
	qInfo() << "Exit button clicked, shutting down...";
	QApplication::quit();
}

void MainWindow::onMessageClicked()
{
	/* 弹出消息输入对话框 */
	MessageDialog *dialog = new MessageDialog(this);

	/* 加载历史消息 */
	QStringList history = m_controller->messageHistory();
	for (const QString &msg : history) {
		dialog->appendHistoryMessage(msg);
	}

	/* 连接信号：用户发送文字消息 */
	connect(dialog, &MessageDialog::messageToSend,
		m_controller, &SystemController::sendMessage);

	/* 连接信号：用户发送语音消息 */
	connect(dialog, &MessageDialog::voiceToSend,
		m_controller, [dialog, this](const QByteArray &pcmData, int duration) {
			dialog->appendVoiceWithData(pcmData, duration, true);
			m_controller->sendVoiceMessage(pcmData, duration);
		});

	/* 连接信号：收到管理端文字消息 */
	connect(m_controller, &SystemController::messageReceived,
		dialog, &MessageDialog::appendReceivedMessage);

	/* 连接信号：收到管理端语音消息 → 在对话框中显示（自动播放在SystemController中） */
	connect(m_controller, &SystemController::voiceMessageReceived,
		dialog, [dialog](const QByteArray &pcmData, int duration, const QString &sender) {
			Q_UNUSED(sender);
			dialog->appendVoiceWithData(pcmData, duration, false);
		});

	dialog->exec();
	dialog->deleteLater();
}

void MainWindow::onMessageToSend(const QString &text)
{
	Q_UNUSED(text);
	/* 此槽未直接使用，消息通过dialog→controller→client的信号链传递 */
}

void MainWindow::onMessageReceived(const QString &text, const QString &sender)
{
	/* 收到管理端消息时显示通知 */
	m_notifyOverlay->showMessage(
		QString("来自[%1]: %2").arg(sender, text),
		NotificationOverlay::Success, 5000);
}

/* ===== 状态变化处理 ===== */

void MainWindow::onStateChanged(SystemState newState)
{
	/* 根据系统状态更新状态指示灯颜色 */
	switch (newState) {
	case IDLE:
		setStatusIndicator("#e74c3c");    /* 红色：空闲 */
		break;
	case FACE_SCANNING:
	case FACE_MATCHED:
		setStatusIndicator("#f1c40f");    /* 黄色：人脸扫描/匹配中 */
		break;
	case RFID_WAITING:
	case RFID_MATCHED:
		setStatusIndicator("#f1c40f");    /* 黄色：读卡中 */
		break;
	case VOICE_LISTENING:
	case VOICE_MATCHED:
		setStatusIndicator("#f1c40f");    /* 黄色：语音监听中 */
		break;
	case PASSWORD_INPUT:
		setStatusIndicator("#f1c40f");    /* 黄色：密码输入中 */
		break;
	case UNLOCKED:
		setStatusIndicator("#27ae60");    /* 绿色：开锁成功 */
		break;
	case FACE_UNKNOWN:
	case RFID_UNKNOWN:
		setStatusIndicator("#e74c3c");    /* 红色：验证失败 */
		break;
	case ALARM:
		setStatusIndicator("#9b59b6");    /* 紫色：告警 */
		break;
	default:
		setStatusIndicator("#95a5a6");    /* 灰色：未知状态 */
		break;
	}
}

void MainWindow::onFrameReady(const QImage &frame)
{
	m_currentFrame = frame;

	/*
	 * 性能优化（嵌入式ARM 528MHz）：
	 * 1. 先用 QImage::scaled() 缩放（比 QPixmap::scaled() 快，避免双重转换）
	 * 2. 再转为 QPixmap 显示
	 * 3. 使用预存的 m_videoTargetSize 避免触发布局查询
	 */
	QImage scaled = frame.scaled(m_videoTargetSize, Qt::KeepAspectRatio,
				     Qt::FastTransformation);
	m_videoLabel->setPixmap(QPixmap::fromImage(scaled));
}

void MainWindow::onFaceRecognized(const RecognizeResult &result)
{
	if (result.matched) {
		m_notifyOverlay->showMessage(
			QString("欢迎, %1!").arg(result.userName),
			NotificationOverlay::Success, 3000);
	} else {
		m_notifyOverlay->showMessage(
			"陌生人!",
			NotificationOverlay::Error, 3000);
	}
}

void MainWindow::onCardDetected(const QString &userId, const QString &userName)
{
	m_notifyOverlay->showMessage(
		QString("欢迎, %1!").arg(userName),
		NotificationOverlay::Success, 3000);
}

void MainWindow::onUnauthorizedCard(const QString &cardId)
{
	m_notifyOverlay->showMessage(
		"未授权卡片",
		NotificationOverlay::Error, 3000);
}

void MainWindow::onDoorUnlocked()
{
	m_notifyOverlay->showMessage(
		"门已打开",
		NotificationOverlay::Success, 3000);
}

void MainWindow::onDoorLocked()
{
	m_notifyOverlay->showMessage(
		"门已锁定",
		NotificationOverlay::Warning, 3000);
}

void MainWindow::onNotification(const QString &message, SystemState contextState)
{
	NotificationOverlay::NotificationType type;
	switch (contextState) {
	case UNLOCKED:
	case FACE_MATCHED:
	case RFID_MATCHED:
	case VOICE_MATCHED:
		type = NotificationOverlay::Success;
		break;
	case FACE_UNKNOWN:
	case RFID_UNKNOWN:
	case ALARM:
		type = NotificationOverlay::Error;
		break;
	default:
		type = NotificationOverlay::Warning;
		break;
	}
	m_notifyOverlay->showMessage(message, type, 3000);
}

void MainWindow::updateTimeDisplay()
{
	m_timeLabel->setText(QTime::currentTime().toString("HH:mm"));
}

/* ===== 私有辅助方法 ===== */

void MainWindow::updateButtonsForState(SystemState state)
{
	/* 默认全部可用 */
	m_faceButton->setEnabled(true);
	m_cardButton->setEnabled(true);
	m_passwordButton->setEnabled(true);
	m_voiceButton->setEnabled(true);

	switch (state) {
	case FACE_SCANNING:
		m_faceButton->setEnabled(false);    /* 扫描中，人脸按钮禁用 */
		break;
	case RFID_WAITING:
		m_cardButton->setEnabled(false);    /* 等待刷卡，刷卡按钮禁用 */
		break;
	case VOICE_LISTENING:
		m_voiceButton->setEnabled(false);   /* 监听中，语音按钮禁用 */
		break;
	case UNLOCKED:
	case FACE_MATCHED:
	case RFID_MATCHED:
	case VOICE_MATCHED:
		/* 开锁状态下所有按钮禁用 */
		m_faceButton->setEnabled(false);
		m_cardButton->setEnabled(false);
		m_passwordButton->setEnabled(false);
		m_voiceButton->setEnabled(false);
		break;
	case ALARM:
		/* 告警状态下所有按钮禁用 */
		m_faceButton->setEnabled(false);
		m_cardButton->setEnabled(false);
		m_passwordButton->setEnabled(false);
		m_voiceButton->setEnabled(false);
		break;
	default:
		break;
	}
}

void MainWindow::setStatusIndicator(const QString &color)
{
	m_statusIndicator->setStyleSheet(QString(
		"QLabel { background-color: %1; border-radius: 10px; "
		"border: 2px solid %1; }"
	).arg(color));
}