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
#include "src/hardware/ButtonMonitor.h"
#include "src/hardware/ServoControl.h"
#include "src/hardware/BeeperControl.h"
#include "src/database/UserDatabase.h"
#include "src/voice/VoiceThread.h"
#include "src/network/MessageClient.h"
#include <QDebug>
#include <QFile>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>

SystemController::SystemController(QObject *parent)
	: QObject(parent),
	  m_state(IDLE),
	  m_ready(false),
	  m_autoLockDelayMs(5000),   /* 5秒后自动关锁 */
	  m_scanTimeoutMs(10000),    /* 10秒扫描超时 */
	  m_systemPasswordHash("8d969eef6ecad3c29a3a629280e686cf0c3f5d5a86aff3ca12020c923adc6c92")  /* 默认密码 "123456" */
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
	m_buttonMonitor = nullptr;
	m_userDatabase = nullptr;
	m_messageClient = nullptr;

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

	/* 停止物理按键监控 */
	if (m_buttonMonitor) {
		m_buttonMonitor->stop();
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
	m_captureThread = new V4L2CaptureThread("/dev/video1", this);  /* OV2640在video1 (CSI) */
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

	/* ===== 初始化用户数据库（必须在FaceProcessThread之前） ===== */
	m_userDatabase = new UserDatabase(this);
	if (!m_userDatabase->open()) {
		qWarning() << "UserDatabase: Failed to open";
		/* 数据库不可用不是致命错误（首次运行） */
	} else {
		qInfo() << "UserDatabase: Ready";
	}

	/* ===== 加载系统配置（系统密码） ===== */
	loadSystemConfig();

	/* ===== 初始化人脸处理线程 ===== */
	if (m_faceDetector && m_faceRecognizer) {
		/* 注册元类型，使 FaceProcessResult 可以通过 QueuedConnection 跨线程传递 */
		qRegisterMetaType<FaceProcessResult>("FaceProcessResult");

		m_faceProcessThread = new FaceProcessThread(m_faceDetector, m_faceRecognizer, m_userDatabase, this);
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
	/* 暂时禁用：RF卡可能有问题，跳过RFID初始化
	 * 恢复时取消下面的注释即可
	 */
#if 0
	m_rfidThread = new RFIDThread(m_userDatabase, this);
	connect(m_rfidThread, &RFIDThread::cardDetected,
		this, [this](const QString &uid, const QString &userName) {
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
		/* RFID不可用不是致命错误，继续运行 */
	} else {
		qInfo() << "RFIDThread: Ready, starting continuous polling";
		/* 初始化后立即开始轮询，不需要等按钮 */
		m_rfidThread->start();
	}
#endif
	qInfo() << "RFIDThread: Temporarily disabled (RF card issue)";

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
	}
	/* start()内部已打印"IRSensorMonitor: Started" */

	/* ===== 初始化物理按键监控（KEY0, GPIO1_IO18） ===== */
	m_buttonMonitor = new ButtonMonitor(18, this);
	connect(m_buttonMonitor, &ButtonMonitor::buttonPressed,
		this, [this]() {
			qInfo() << "Button: KEY0 pressed, unlocking door";
			unlockDoor();
		}, Qt::QueuedConnection);
	connect(m_buttonMonitor, &ButtonMonitor::deviceError,
		this, [this](const QString &error) {
			emit notification(error, ALARM);
		}, Qt::QueuedConnection);

	if (!m_buttonMonitor->start()) {
		qWarning() << "ButtonMonitor: Failed to start";
		/* 按键不可用不是致命错误 */
	} else {
		qInfo() << "ButtonMonitor: Ready (KEY0, GPIO1_IO18)";
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

	/* ===== 初始化消息客户端（TCP连接Ubuntu管理程序） ===== */
	m_messageClient = new MessageClient(this);
	connect(m_messageClient, &MessageClient::messageReceived,
		this, [this](const QString &text, const QString &sender) {
			/* 保存到消息历史（限制最多100条） */
			m_messageHistory.append(QString("[管理端] %1").arg(text));
			while (m_messageHistory.size() > 100)
				m_messageHistory.removeFirst();
			emit messageReceived(text, sender);
			emit notification("收到消息: " + text, IDLE);
		}, Qt::QueuedConnection);
	connect(m_messageClient, &MessageClient::voiceMessageReceived,
		this, [this](const QByteArray &pcmData, int duration, const QString &sender) {
			m_messageHistory.append(QString("[管理端] [语音 %1秒]").arg(duration));
			while (m_messageHistory.size() > 100)
				m_messageHistory.removeFirst();
			emit voiceMessageReceived(pcmData, duration, sender);
			emit notification(QString("收到语音消息 (%1秒)，播放中...").arg(duration), IDLE);

			/* 自动播放语音消息 */
			QFile tmpFile("/tmp/vp_play.raw");
			if (tmpFile.open(QIODevice::WriteOnly)) {
				tmpFile.write(pcmData);
				tmpFile.close();
				qInfo() << "Auto-playing voice message," << pcmData.size() << "bytes";
				QProcess playProc;
				playProc.start("aplay",
					QStringList() << "-f" << "S16_LE" << "-r" << "16000"
						      << "-c" << "1" << "-t" << "raw"
						      << "/tmp/vp_play.raw");
				playProc.waitForFinished(-1);
			}
		}, Qt::QueuedConnection);
	connect(m_messageClient, &MessageClient::connectionStateChanged,
		this, &SystemController::messageConnectionChanged, Qt::QueuedConnection);
	connect(m_messageClient, &MessageClient::connectionError,
		this, [this](const QString &error) {
			qWarning() << "MessageClient:" << error;
		}, Qt::QueuedConnection);

	/* 从配置文件读取管理程序IP，然后连接 */
	{
		QFile cfgFile("/opt/visionpass/config/system.json");
		if (cfgFile.open(QIODevice::ReadOnly)) {
			QJsonDocument doc = QJsonDocument::fromJson(cfgFile.readAll());
			cfgFile.close();
			if (doc.isObject()) {
				QJsonObject json = doc.object();
				QString ip = json["manager_ip"].toString();
				int port = json["manager_port"].toInt(9500);
				if (!ip.isEmpty()) {
					/* 有固定IP配置，直接连接 */
					m_messageClient->connectToServer(ip, port);
					qInfo() << "MessageClient: Connecting to" << ip << ":" << port;
				} else {
					/* 没有配置IP，启动UDP自动发现 */
					m_messageClient->startDiscovery(9501, port);
					qInfo() << "MessageClient: No manager_ip, starting auto-discovery";
				}
			}
		} else {
			/* 没有配置文件，启动UDP自动发现 */
			m_messageClient->startDiscovery(9501, 9500);
			qInfo() << "MessageClient: No config file, starting auto-discovery";
		}
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

	/* 暂时禁用：RF卡可能有问题，提示用户
	 * 恢复时删除此段，取消下面 #if 0 ... #endif 的注释
	 */
	emit notification("刷卡功能暂时禁用（RF卡问题）", ALARM);
	qInfo() << "Card reading disabled (RF card issue)";
	return;

#if 0
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
#endif
}

void SystemController::stopCardReading()
{
	if (m_state != RFID_WAITING)
		return;

	m_scanTimeoutTimer->stop();

	/* 不停止线程，保持持续轮询 */
	setState(IDLE);
	qInfo() << "Card reading stopped (thread still polling)";
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

/*
 * 加载系统配置
 *
 * 从/opt/visionpass/config/system.json读取系统密码哈希。
 * 如果配置文件不存在或格式错误，使用默认密码"123456"的哈希。
 */
bool SystemController::loadSystemConfig()
{
	QString configPath = "/opt/visionpass/config/system.json";
	QFile file(configPath);

	if (!file.exists()) {
		qInfo() << "System config not found at" << configPath
			<< ", using default password (123456)";
		return false;
	}

	if (!file.open(QIODevice::ReadOnly)) {
		qWarning() << "Cannot read system config:" << configPath;
		return false;
	}

	QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
	file.close();

	if (doc.isObject()) {
		QJsonObject json = doc.object();
		QString hash = json["password_hash"].toString();

		if (!hash.isEmpty()) {
			m_systemPasswordHash = hash;
			qInfo() << "System password loaded from config";
			return true;
		}
	}

	qWarning() << "Invalid system config format, using default password";
	return false;
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

void SystemController::submitPassword(const QString &password)
{
	qInfo() << "Password submitted, length=" << password.length();

	/* 计算输入密码的SHA-256哈希，与系统密码哈希比对 */
	QString inputHash = UserDatabase::hashPassword(password);

	if (inputHash == m_systemPasswordHash) {
		qInfo() << "Password correct, unlocking door";
		unlockDoor();
	} else {
		qWarning() << "Password incorrect";
		emit notification("密码错误", ALARM);

		/* 密码错误后返回IDLE状态 */
		setState(IDLE);
	}
}

void SystemController::startFaceRegistration(const QString &userId)
{
	qInfo() << "Face registration started for user:" << userId;

	/*
	 * 人脸注册流程：
	 * 1. 启动摄像头采集
	 * 2. 检测人脸并提取特征
	 * 3. 保存特征到数据库
	 * 4. 注册完成后停止采集
	 *
	 * 简化实现：启动人脸扫描，检测到人脸后保存特征
	 */
	if (!m_ready) {
		emit notification("系统未就绪", ALARM);
		return;
	}

	/* 启动人脸扫描（会打开摄像头） */
	startFaceRecognition();

	/* 发送通知提示用户 */
	emit notification(QString("正在为用户 %1 注册人脸，请正对摄像头").arg(userId), FACE_SCANNING);

	/*
	 * TODO: 完整实现需要在FaceProcessThread中添加注册模式
	 * 检测到人脸后：
	 *   1. 提取特征
	 *   2. 保存到数据库（m_userDatabase->saveFaceFeature(userId, feature)）
	 *   3. 发送注册成功通知
	 *   4. 停止扫描
	 */
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

void SystemController::sendMessage(const QString &text)
{
	if (m_messageClient && m_messageClient->isConnected()) {
		m_messageClient->sendMessage(text);
		/* 保存到消息历史（限制最多100条） */
		m_messageHistory.append(QString("[我] %1").arg(text));
		while (m_messageHistory.size() > 100)
			m_messageHistory.removeFirst();
		qInfo() << "Message sent:" << text;
	} else {
		qWarning() << "Cannot send message: not connected";
		emit notification("未连接到管理程序", ALARM);
	}
}

void SystemController::sendVoiceMessage(const QByteArray &pcmData, int duration)
{
	if (m_messageClient && m_messageClient->isConnected()) {
		m_messageClient->sendVoiceMessage(pcmData, duration);
		m_messageHistory.append(QString("[我] [语音 %1秒]").arg(duration));
		while (m_messageHistory.size() > 100)
			m_messageHistory.removeFirst();
		qInfo() << "Voice message sent:" << duration << "s";
	} else {
		qWarning() << "Cannot send voice: not connected";
		emit notification("未连接到管理程序", ALARM);
	}
}

QStringList SystemController::messageHistory() const
{
	return m_messageHistory;
}