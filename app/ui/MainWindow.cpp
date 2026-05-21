/*
 * VisionPass 主窗口实现
 *
 * 1024x600 LCD布局：
 *   左侧视频区(844x590) + 右侧按钮栏(180px)
 *   浮层：状态LED(20x20)、时间(160x24)、通知(440x50)
 */

#include "MainWindow.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTime>
#include <QPixmap>
#include <QPainter>

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

	/* 5个开锁按钮 */
	m_faceButton = new QPushButton("人脸开锁", rightPanel);
	m_cardButton = new QPushButton("刷卡开锁", rightPanel);
	m_passwordButton = new QPushButton("密码开锁", rightPanel);
	m_voiceButton = new QPushButton("语音开锁", rightPanel);
	m_adminButton = new QPushButton("管理", rightPanel);

	/* 按钮固定尺寸 */
	QList<QPushButton*> buttons = {
		m_faceButton, m_cardButton, m_passwordButton,
		m_voiceButton, m_adminButton
	};

	/* 每个按钮的配色和样式 */
	QStringList colors = {
		"#3498db",   /* 人脸：蓝色 */
		"#f39c12",   /* 刷卡：橙色 */
		"#27ae60",   /* 密码：绿色 */
		"#9b59b6",   /* 语音：紫色 */
		"#1abc9c"    /* 管理：青色 */
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

	buttonLayout->addStretch();  /* 下方弹性空间 */

	/* 组装主布局 */
	mainLayout->addWidget(videoContainer, 1);   /* stretch=1,占据剩余空间 */
	mainLayout->addWidget(rightPanel);
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
	connect(m_adminButton, &QPushButton::clicked,
		this, &MainWindow::onAdminClicked);

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
	/* 后续：弹出PasswordDialog */
	m_controller->startPasswordInput();
}

void MainWindow::onVoiceUnlockClicked()
{
	m_controller->startVoiceRecognition();
}

void MainWindow::onAdminClicked()
{
	/* 后续：弹出UserManagementDialog */
}

/* ===== SystemController信号处理 ===== */

void MainWindow::onStateChanged(SystemState newState)
{
	/* 根据状态更新按钮启用/禁用 */
	updateButtonsForState(newState);

	/* 根据状态更新LED颜色 */
	switch (newState) {
	case IDLE:
		setStatusIndicator("#e74c3c");   /* 红色：锁定 */
		break;
	case FACE_SCANNING:
		setStatusIndicator("#f1c40f");    /* 黄色：扫描中 */
		break;
	case FACE_MATCHED:
	case RFID_MATCHED:
	case VOICE_MATCHED:
	case UNLOCKED:
		setStatusIndicator("#27ae60");    /* 绿色：成功/开锁 */
		break;
	case FACE_UNKNOWN:
	case RFID_UNKNOWN:
		setStatusIndicator("#e74c3c");    /* 红色：拒绝 */
		break;
	case RFID_WAITING:
	case VOICE_LISTENING:
		setStatusIndicator("#f39c12");     /* 橙色：等待 */
		break;
	case PASSWORD_INPUT:
		setStatusIndicator("#f39c12");     /* 橙色：输入中 */
		break;
	case ALARM:
		setStatusIndicator("#9b59b6");     /* 紫色：告警 */
		break;
	}
}

void MainWindow::onFrameReady(const QImage &frame)
{
	m_currentFrame = frame;

	/* 在视频区显示帧 */
	QPixmap pixmap = QPixmap::fromImage(frame);
	/* 缩放以适应视频区尺寸，保持宽高比 */
	m_videoLabel->setPixmap(pixmap.scaled(
		m_videoLabel->size(),
		Qt::KeepAspectRatio,
		Qt::FastTransformation  /* 快速缩放，嵌入式设备用FastTransformation */
	));
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
	m_adminButton->setEnabled(true);

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
		m_adminButton->setEnabled(false);
		break;
	case ALARM:
		/* 告警状态下所有按钮禁用 */
		m_faceButton->setEnabled(false);
		m_cardButton->setEnabled(false);
		m_passwordButton->setEnabled(false);
		m_voiceButton->setEnabled(false);
		m_adminButton->setEnabled(false);
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