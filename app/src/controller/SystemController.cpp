/*
 * VisionPass 系统控制器实现
 *
 * 中央协调器：拥有所有硬件模块，管理状态机，桥接信号
 *
 * 信号流向：
 *   硬件模块 → Controller信号 → MainWindow槽 → UI更新
 *   UI按钮   → MainWindow信号 → Controller槽 → 启动对应硬件模块
 */

#include "SystemController.h"
#include "src/capture/V4L2CaptureThread.h"
#include "src/face/FaceProcessThread.h"
#include "src/hardware/RFIDThread.h"
#include "src/hardware/IRSensorMonitor.h"
#include "src/hardware/ServoControl.h"
#include "src/hardware/BeeperControl.h"
#include "src/database/UserDatabase.h"
#include "src/voice/VoiceThread.h"
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
	m_captureThread = nullptr;
	m_faceProcessThread = nullptr;
	m_rfidThread = nullptr;
	m_voiceThread = nullptr;
	m_servoControl = nullptr;
	m_beeperControl = nullptr;
	m_irSensorMonitor = nullptr;
	m_userDatabase = nullptr;

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
	/*
	 * 停止所有线程和模块（三步曲：stop → wait → 让QObject自动delete）
	 * 必须先停止线程，再让~QObject删除对象，否则线程可能访问已释放内存
	 */

	/* 停止语音线程 */
	if (m_voiceThread && m_voiceThread->isRunning()) {
		m_voiceThread->stopListening();
		m_voiceThread->wait(5000);
	}

	/* 停止RFID线程 */
	if (m_rfidThread && m_rfidThread->isRunning()) {
		m_rfidThread->stopPolling();
		m_rfidThread->wait(5000);
	}

	/* 停止人脸处理线程 */
	if (m_faceProcessThread) {
		m_faceProcessThread->stopProcessing();
		m_faceProcessThread->wait(5000);
	}

	/* 停止摄像头采集线程 */
	if (m_captureThread && m_captureThread->isRunning()) {
		m_captureThread->stopCapture();
		m_captureThread->wait(5000);
	}

	/* 停止IR传感器监控 */
	if (m_irSensorMonitor) {
		m_irSensorMonitor->stop();
	}

	/* 关闭舵机设备（停止PWM） */
	if (m_servoControl) {
		m_servoControl->closeDevice();
	}

	/* 关闭数据库 */
	if (m_userDatabase) {
		m_userDatabase->close();
	}

	/* 所有QObject子对象的parent都是this，~QObject会自动delete它们 */
}

bool SystemController::initialize()
{
	qInfo() << "SystemController: Initializing...";

	bool allOk = true;

	/* ===== 初始化人脸检测器（Haar级联） ===== */
	m_faceDetector = new FaceDetector(QString(), this);
	if (!m_faceDetector->isLoaded()) {
		qWarning() << "FaceDetector failed to load";
		allOk = false;
	}

	/* ===== 初始化人脸识别器（NCNN MobileFaceNet） ===== */
	m_faceRecognizer = new FaceRecognizer(QString(), 0.6f, this);
	if (!m_faceRecognizer->isLoaded()) {
		qWarning() << "FaceRecognizer failed to load";
		allOk = false;
	}

	/* ===== 初始化V4L2摄像头采集线程 ===== */
	m_captureThread = new V4L2CaptureThread("/dev/video0", this);
	if (!m_captureThread->openDevice()) {
		qWarning() << "V4L2: Failed to open camera device";
		m_captureThread->setParent(nullptr);
		delete m_captureThread;
		m_captureThread = nullptr;
		allOk = false;
	} else {
		connect(m_captureThread, &V4L2CaptureThread::frameReady,
			this, &SystemController::frameReady, Qt::QueuedConnection);
		qInfo() << "V4L2: Camera ready";
	}

	/* ===== 初始化人脸处理线程 ===== */
	if (m_faceDetector && m_faceRecognizer) {
		m_faceProcessThread = new FaceProcessThread(m_faceDetector, m_faceRecognizer, this);
		connect(m_faceProcessThread, &FaceProcessThread::facesDetected,
			this, &SystemController::facesDetected, Qt::QueuedConnection);
		connect(m_faceProcessThread, &FaceProcessThread::recognitionResult,
			this, [this](const FaceProcessResult &result) {
				RecognizeResult rec;
				rec.userId = result.userId;
				rec.userName = result.userName;
				rec.similarity = result.similarity;
				rec.matched = result.matched;
				emit faceRecognized(rec);

				/* 人脸匹配成功 → 自动开锁 */
				if (result.matched) {
					unlockDoor();
				}
			}, Qt::QueuedConnection);
		qInfo() << "FaceProcessThread: Ready";
	}

	/* ===== 初始化RFID线程 ===== */
	m_rfidThread = new RFIDThread(this);
	/* 先连接信号，再初始化设备（确保错误信号能被接收） */
	connect(m_rfidThread, &RFIDThread::cardDetected,
		this, [this](const QString &uid, const QString &userName) {
			/* RFID匹配成功 → 自动开锁 */
			emit cardDetected(uid, userName);
			unlockDoor();
		}, Qt::QueuedConnection);
	connect(m_rfidThread, &RFIDThread::unauthorizedCard,
		this, [this](const QString &cardId) {
			emit unauthorizedCard(cardId);
			emit notification("未授权卡片: " + cardId, ALARM);
		}, Qt::QueuedConnection);
	connect(m_rfidThread, &RFIDThread::deviceError,
		this, [this](const QString &error) {
			emit notification(error, ALARM);
		}, Qt::QueuedConnection);

	if (!m_rfidThread->initDevice()) {
		qWarning() << "RFIDThread: Failed to initialize RC522";
		m_rfidThread->setParent(nullptr);
		delete m_rfidThread;
		m_rfidThread = nullptr;
		allOk = false;
	} else {
		qInfo() << "RFIDThread: Ready";
	}

	/* ===== 初始化舵机控制 ===== */
	m_servoControl = new ServoControl(this);
	if (!m_servoControl->openDevice()) {
		qWarning() << "ServoControl: Failed to open /dev/servo";
		/* 舵机不可用不是致命错误，继续运行 */
	} else {
		qInfo() << "ServoControl: Ready";
	}

	/* ===== 初始化蜂鸣器 ===== */
	m_beeperControl = new BeeperControl(this);
	qInfo() << "BeeperControl: Ready";

	/* ===== 初始化IR传感器监控 ===== */
	m_irSensorMonitor = new IRSensorMonitor(this);
	connect(m_irSensorMonitor, &IRSensorMonitor::personDetected,
		this, [this]() {
			/* 检测到人靠近 → 自动启动人脸扫描 */
			if (m_state == IDLE) {
				qInfo() << "IR: Person detected, auto-starting face recognition";
				startFaceRecognition();
			}
		}, Qt::QueuedConnection);
	connect(m_irSensorMonitor, &IRSensorMonitor::personLeft,
		this, [this]() {
			/* 人离开 → 如果正在扫描则停止 */
			if (m_state == FACE_SCANNING) {
				qInfo() << "IR: Person left, stopping face recognition";
				stopFaceRecognition();
			}
		}, Qt::QueuedConnection);
	connect(m_irSensorMonitor, &IRSensorMonitor::deviceError,
		this, [this](const QString &error) {
			emit notification(error, ALARM);
		}, Qt::QueuedConnection);

	if (!m_irSensorMonitor->start()) {
		qWarning() << "IRSensorMonitor: Failed to start";
		/* IR传感器不可用不是致命错误 */
	} else {
		qInfo() << "IRSensorMonitor: Started";
	}

	/* ===== 初始化用户数据库 ===== */
	m_userDatabase = new UserDatabase(this);
	if (!m_userDatabase->open()) {
		qWarning() << "UserDatabase: Failed to open";
		/* 数据库不可用不是致命错误（首次运行） */
	} else {
		qInfo() << "UserDatabase: Ready";
	}

	/* ===== 初始化语音识别线程 ===== */
	m_voiceThread = new VoiceThread(this);
	connect(m_voiceThread, &VoiceThread::voiceCommandDetected,
		this, [this](const QString &command) {
			if (command == "开门") {
				emit notification("语音指令识别成功", UNLOCKED);
				unlockDoor();
			}
		}, Qt::QueuedConnection);
	connect(m_voiceThread, &VoiceThread::deviceError,
		this, [this](const QString &error) {
			emit notification(error, ALARM);
		}, Qt::QueuedConnection);
	qInfo() << "VoiceThread: Ready (DTW placeholder)";

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

	if (m_faceProcessThread && m_faceProcessThread->isRunning()) {
		qWarning() << "Face processing already running";
		return;
	}

	setState(FACE_SCANNING);
	m_scanTimeoutTimer->start(m_scanTimeoutMs);

	/* 启动人脸处理线程 */
	if (m_faceProcessThread) {
		m_faceProcessThread->start();
		qInfo() << "FaceProcessThread: Started";
	}

	/* 启动摄像头采集线程，帧直接给人脸处理线程 */
	if (m_captureThread && m_captureThread->isOpen()) {
		m_frameToFaceConnection = connect(m_captureThread, &V4L2CaptureThread::frameReady,
						m_faceProcessThread, &FaceProcessThread::addFrame,
						Qt::QueuedConnection);
		m_captureThread->start();
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

	/* 断开V4L2帧与人脸处理线程的连接 */
	if (m_frameToFaceConnection) {
		disconnect(m_frameToFaceConnection);
		m_frameToFaceConnection = QMetaObject::Connection();
	}

	/* 停止人脸处理线程 */
	if (m_faceProcessThread && m_faceProcessThread->isRunning()) {
		m_faceProcessThread->stopProcessing();
		m_faceProcessThread->wait(5000);
		qInfo() << "FaceProcessThread: Stopped";
	}

	/* 停止摄像头采集线程 */
	if (m_captureThread && m_captureThread->isRunning()) {
		m_captureThread->stopCapture();
		m_captureThread->wait(3000);
		qInfo() << "V4L2: Capture thread stopped";
	}

	setState(IDLE);
	qInfo() << "Face recognition stopped";
}

void SystemController::startCardReading()
{
	if (m_state != IDLE)
		return;

	if (m_rfidThread && m_rfidThread->isRunning()) {
		qWarning() << "RFID reading already running";
		return;
	}

	setState(RFID_WAITING);
	m_scanTimeoutTimer->start(m_scanTimeoutMs);

	if (m_rfidThread && !m_rfidThread->isRunning()) {
		m_rfidThread->start();
		qInfo() << "RFIDThread: Started";
	} else if (!m_rfidThread) {
		qWarning() << "RFIDThread: Not available";
	}

	qInfo() << "Card reading started";
}

void SystemController::stopCardReading()
{
	if (m_state != RFID_WAITING)
		return;

	m_scanTimeoutTimer->stop();

	if (m_rfidThread && m_rfidThread->isRunning()) {
		m_rfidThread->stopPolling();
		m_rfidThread->wait(5000);
		qInfo() << "RFIDThread: Stopped";
	}

	setState(IDLE);
	qInfo() << "Card reading stopped";
}

/*
 * 停止所有正在进行的扫描（人脸/RFID/语音）
 * 用于unlockDoor()等需要强制清理所有子系统的场景
 * 与stopFaceRecognition/stopCardReading/stopVoiceRecognition不同，
 * 此方法不检查当前状态，而是根据线程运行状态强制停止
 */
void SystemController::stopAllActiveScanning()
{
	m_scanTimeoutTimer->stop();

	/* 停止人脸扫描（如果正在运行） */
	if (m_captureThread && m_captureThread->isRunning()) {
		/* 断开V4L2帧与人脸处理线程的连接 */
		if (m_frameToFaceConnection) {
			disconnect(m_frameToFaceConnection);
			m_frameToFaceConnection = QMetaObject::Connection();
		}

		if (m_faceProcessThread && m_faceProcessThread->isRunning()) {
			m_faceProcessThread->stopProcessing();
			m_faceProcessThread->wait(5000);
			qInfo() << "FaceProcessThread: Stopped (force)";
		}

		m_captureThread->stopCapture();
		m_captureThread->wait(3000);
		qInfo() << "V4L2: Capture thread stopped (force)";
	}

	/* 停止RFID扫描（如果正在运行） */
	if (m_rfidThread && m_rfidThread->isRunning()) {
		m_rfidThread->stopPolling();
		m_rfidThread->wait(5000);
		qInfo() << "RFIDThread: Stopped (force)";
	}

	/* 停止语音识别（如果正在运行） */
	if (m_voiceThread && m_voiceThread->isRunning()) {
		m_voiceThread->stopListening();
		m_voiceThread->wait(5000);
		qInfo() << "VoiceThread: Stopped (force)";
	}
}

void SystemController::verifyPassword(const QString &password)
{
	if (!m_userDatabase || !m_userDatabase->isOpen()) {
		qWarning() << "UserDatabase not available";
		emit notification("密码验证失败：数据库不可用", ALARM);
		setState(IDLE);
		return;
	}

	/* 计算输入密码的SHA-256哈希 */
	QString hash = UserDatabase::hashPassword(password);

	/*
	 * 使用优化的SQL查询直接匹配密码哈希
	 * 只查询id, name, password_hash字段，不加载人脸特征BLOB（节省~500ms）
	 */
	UserInfo user = m_userDatabase->findUserByPasswordHash(hash);

	if (!user.id.isEmpty()) {
		emit notification(QString("欢迎, %1!").arg(user.name), UNLOCKED);
		unlockDoor();
	} else {
		emit notification("密码错误", ALARM);
		if (m_beeperControl) m_beeperControl->beepError();
	}

	setState(IDLE);
}

void SystemController::startVoiceRecognition()
{
	if (m_state != IDLE)
		return;

	setState(VOICE_LISTENING);
	m_scanTimeoutTimer->start(5000);  /* 语音5秒超时 */

	if (m_voiceThread && !m_voiceThread->isRunning()) {
		m_voiceThread->start();
		qInfo() << "VoiceThread: Started";
	}

	qInfo() << "Voice recognition started";
}

void SystemController::stopVoiceRecognition()
{
	if (m_state != VOICE_LISTENING)
		return;

	m_scanTimeoutTimer->stop();

	if (m_voiceThread && m_voiceThread->isRunning()) {
		m_voiceThread->stopListening();
		m_voiceThread->wait(5000);
		qInfo() << "VoiceThread: Stopped";
	}

	setState(IDLE);
	qInfo() << "Voice recognition stopped";
}

void SystemController::startPasswordInput()
{
	setState(PASSWORD_INPUT);
	qInfo() << "Password input started";
}

void SystemController::unlockDoor()
{
	/* 防止重复开锁 */
	if (m_state == UNLOCKED)
		return;

	/* 停止所有正在进行的扫描（人脸/RFID/语音），避免线程继续运行 */
	stopAllActiveScanning();

	setState(UNLOCKED);
	emit doorUnlocked();
	emit notification("门已打开", UNLOCKED);

	/* 舵机开锁（90度） */
	if (m_servoControl && m_servoControl->isOpen()) {
		if (!m_servoControl->unlock()) {
			qWarning() << "SystemController: Servo unlock failed";
			emit notification("舵机开锁失败", ALARM);
		}
	}

	/* 蜂鸣器成功提示 */
	if (m_beeperControl) {
		m_beeperControl->beepSuccess();
	}

	/* 启动自动关锁定时器 */
	m_autoLockTimer->start(m_autoLockDelayMs);

	qInfo() << "Door unlocked, auto-lock in" << m_autoLockDelayMs << "ms";
}

void SystemController::lockDoor()
{
	if (m_state != UNLOCKED)
		return;

	setState(IDLE);
	emit doorLocked();
	emit notification("门已锁定", IDLE);

	/* 舵机关锁（0度） */
	if (m_servoControl && m_servoControl->isOpen()) {
		m_servoControl->lock();
	}

	qInfo() << "Door locked";
}