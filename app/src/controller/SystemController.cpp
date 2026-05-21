/*
 * VisionPass 系统控制器实现
 *
 * 骨架版本：包含状态机基本逻辑和初始化框架
 * 后续逐步添加各硬件模块的实际操作
 */

#include "SystemController.h"
#include "src/capture/V4L2CaptureThread.h"
#include <QDebug>

SystemController::SystemController(QObject *parent)
	: QObject(parent),
	  m_state(IDLE),
	  m_ready(false),
	  m_autoLockDelayMs(5000),   /* 5秒后自动关锁 */
	  m_scanTimeoutMs(10000)     /* 10秒扫描超时 */
{
	m_faceDetector = nullptr;
	m_faceRecognizer = nullptr;

	/* 创建定时器 */
	m_autoLockTimer = new QTimer(this);
	m_autoLockTimer->setSingleShot(true);
	connect(m_autoLockTimer, &QTimer::timeout, this, &SystemController::lockDoor);

	m_scanTimeoutTimer = new QTimer(this);
	m_scanTimeoutTimer->setSingleShot(true);
	connect(m_scanTimeoutTimer, &QTimer::timeout, this, [this]() {
		qInfo() << "Scan timeout, returning to IDLE";
		setState(IDLE);
	});
}

SystemController::~SystemController()
{
	/* 停止并等待摄像头采集线程（如果正在运行） */
	if (m_captureThread) {
		if (m_captureThread->isRunning()) {
			m_captureThread->stopCapture();
			m_captureThread->wait(5000);  /* 等待最多5秒 */
		}
	}

	/* 注意：m_faceDetector, m_faceRecognizer, m_captureThread 的 parent 都是 this，
	 * 所以 ~QObject 会自动 delete 它们。
	 * 但必须先停止线程（上面的代码），再让 ~QObject 删除对象。
	 */
}

bool SystemController::initialize()
{
	qInfo() << "SystemController: Initializing...";

	bool allOk = true;

	/* 初始化人脸检测器（Haar级联） */
	m_faceDetector = new FaceDetector(QString(), this);
	if (!m_faceDetector->isLoaded()) {
		qWarning() << "FaceDetector failed to load";
		allOk = false;
	}

	/* 初始化人脸识别器（NCNN MobileFaceNet） */
	m_faceRecognizer = new FaceRecognizer(QString(), 0.6f, this);
	if (!m_faceRecognizer->isLoaded()) {
		qWarning() << "FaceRecognizer failed to load";
		allOk = false;
	}

	/* 初始化V4L2摄像头采集线程 */
	m_captureThread = new V4L2CaptureThread("/dev/video0", this);
	if (!m_captureThread->openDevice()) {
		qWarning() << "V4L2: Failed to open camera device";
		delete m_captureThread;
		m_captureThread = nullptr;
		allOk = false;
	} else {
		/* 连接摄像头帧信号 → Controller转发 → MainWindow */
		connect(m_captureThread, &V4L2CaptureThread::frameReady,
			this, &SystemController::frameReady, Qt::QueuedConnection);
		qInfo() << "V4L2: Camera ready";
	}

	m_ready = allOk;
	if (allOk) {
		qInfo() << "SystemController: Initialized successfully, state=IDLE";
		setState(IDLE);
	} else {
		qWarning() << "SystemController: Partial initialization failure";
	}
	return allOk;
}

SystemState SystemController::state() const
{
	return m_state;
}

bool SystemController::isReady() const
{
	return m_ready;
}

void SystemController::setState(SystemState newState)
{
	if (m_state == newState)
		return;

	m_state = newState;
	qInfo() << "SystemController: State changed to" << newState;
	emit stateChanged(newState);
}

void SystemController::startFaceRecognition()
{
	if (m_state != IDLE)
		return;

	/* 防止重复启动（如用户快速双击按钮） */
	if (m_captureThread && m_captureThread->isRunning()) {
		qWarning() << "Face recognition already running";
		return;
	}

	setState(FACE_SCANNING);
	m_scanTimeoutTimer->start(m_scanTimeoutMs);

	/* 启动摄像头采集线程 */
	if (m_captureThread && m_captureThread->isOpen()) {
		m_captureThread->start();  /* 启动QThread */
		qInfo() << "V4L2: Capture thread started";
	} else {
		qWarning() << "V4L2: Camera not available, face recognition disabled";
	}

	qInfo() << "Face recognition started";
}

void SystemController::stopFaceRecognition()
{
	if (m_state != FACE_SCANNING)
		return;

	m_scanTimeoutTimer->stop();

	/* 停止摄像头采集线程 */
	if (m_captureThread && m_captureThread->isRunning()) {
		m_captureThread->stopCapture();
		m_captureThread->wait(3000);  /* 等待最多3秒 */
		qInfo() << "V4L2: Capture thread stopped";
	}

	setState(IDLE);
	qInfo() << "Face recognition stopped";
}

void SystemController::startCardReading()
{
	if (m_state != IDLE)
		return;

	setState(RFID_WAITING);
	m_scanTimeoutTimer->start(m_scanTimeoutMs);

	/* 后续：启动RFIDThread轮询 */
	qInfo() << "Card reading started";
}

void SystemController::stopCardReading()
{
	if (m_state != RFID_WAITING)
		return;

	m_scanTimeoutTimer->stop();
	setState(IDLE);

	/* 后续：停止RFIDThread */
	qInfo() << "Card reading stopped";
}

void SystemController::verifyPassword(const QString &password)
{
	/* 后续：从UserDatabase读取密码哈希比对 */
	qInfo() << "Password verification requested:" << password;
}

void SystemController::startVoiceRecognition()
{
	if (m_state != IDLE)
		return;

	setState(VOICE_LISTENING);
	m_scanTimeoutTimer->start(5000);  /* 语音5秒超时 */

	/* 后续：启动VoiceThread */
	qInfo() << "Voice recognition started";
}

void SystemController::stopVoiceRecognition()
{
	if (m_state != VOICE_LISTENING)
		return;

	m_scanTimeoutTimer->stop();
	setState(IDLE);

	/* 后续：停止VoiceThread */
	qInfo() << "Voice recognition stopped";
}

void SystemController::startPasswordInput()
{
	setState(PASSWORD_INPUT);
	qInfo() << "Password input started";
}

void SystemController::unlockDoor()
{
	setState(UNLOCKED);
	emit doorUnlocked();
	emit notification("门已打开", UNLOCKED);

	/* 启动自动关锁定时器 */
	m_autoLockTimer->start(m_autoLockDelayMs);

	/* 后续：ServoControl转到90度 */
	qInfo() << "Door unlocked, auto-lock in" << m_autoLockDelayMs << "ms";
}

void SystemController::lockDoor()
{
	if (m_state != UNLOCKED)
		return;

	setState(IDLE);
	emit doorLocked();
	emit notification("门已锁定", IDLE);

	/* 后续：ServoControl转到0度 */
	qInfo() << "Door locked";
}