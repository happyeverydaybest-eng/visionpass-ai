/*
 * VisionPass 系统控制器（中央协调器）
 *
 * 核心职责：
 * =========
 * SystemController是整个门禁系统的"大脑"，负责：
 * 1. 拥有所有硬件模块实例（摄像头、RFID、舵机、蜂鸣器等）
 * 2. 管理系统状态机（IDLE → FACE_SCANNING → UNLOCKED → IDLE循环）
 * 3. 桥接信号：硬件模块的信号通过Controller转发给MainWindow
 * 4. 控制开锁/关锁逻辑：哪种验证方式成功 → 舵机开锁 → 定时关锁
 *
 * 为什么需要Controller而不是MainWindow直接连硬件？
 * =========
 * - MainWindow只管UI，不应该知道/dev/rc522或/dev/servo的存在
 * - Controller统一管理状态，避免多个验证方式同时触发冲突
 * - 状态转换逻辑集中在一处，便于调试和维护
 *
 * 信号流向：
 *   硬件模块 → Controller信号 → MainWindow槽 → UI更新
 *   UI按钮   → MainWindow信号 → Controller槽 → 启动对应硬件模块
 */

#ifndef SYSTEMCONTROLLER_H
#define SYSTEMCONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <QVector>
#include <QRect>
#include "SystemState.h"
#include "src/face/FaceDetector.h"
#include "src/face/FaceRecognizer.h"

/* 前向声明（避免头文件互相引用） */
class V4L2CaptureThread;
class FaceProcessThread;
class RFIDThread;

/* 人脸处理结果（定义在FaceProcessThread.h中） */
struct FaceProcessResult;

class SystemController : public QObject
{
	Q_OBJECT

public:
	explicit SystemController(QObject *parent = nullptr);
	~SystemController();

	/* 初始化所有模块（加载模型、打开设备） */
	bool initialize();

	/* 当前系统状态 */
	SystemState state() const;

	/* 模块是否就绪 */
	bool isReady() const;

public slots:
	/* ===== 验证方式启动（来自MainWindow按钮） ===== */

	/* 启动人脸扫描（打开摄像头+开始检测循环） */
	void startFaceRecognition();
	/* 停止人脸扫描 */
	void stopFaceRecognition();

	/* 启动RFID刷卡等待 */
	void startCardReading();
	/* 停止RFID刷卡等待 */
	void stopCardReading();

	/* 密码验证 */
	void verifyPassword(const QString &password);

	/* 启动语音监听 */
	void startVoiceRecognition();
	/* 停止语音监听 */
	void stopVoiceRecognition();

	/* 启动密码输入（MainWindow弹出密码对话框） */
	void startPasswordInput();

	/* ===== 开锁/关锁 ===== */
	void unlockDoor();
	void lockDoor();

signals:
	/* ===== 状态变化信号（发送给MainWindow） ===== */

	/* 系统状态变化 */
	void stateChanged(SystemState newState);

	/* 视频帧就绪（来自摄像头线程） */
	void frameReady(const QImage &frame);

	/* 人脸检测结果（用于在视频上画框） */
	void facesDetected(const QVector<QRect> &faces);

	/* 人脸识别结果 */
	void faceRecognized(const RecognizeResult &result);

	/* RFID卡片检测结果 */
	void cardDetected(const QString &userId, const QString &userName);
	void unauthorizedCard(const QString &cardId);

	/* IR传感器状态变化 */
	void irSensorChanged(bool personDetected);

	/* 开锁/关锁事件 */
	void doorUnlocked();
	void doorLocked();

	/* 通知消息（错误/成功/警告） */
	void notification(const QString &message, SystemState contextState);

private:
	/* 状态机核心：切换状态并发射信号 */
	void setState(SystemState newState);

	/* 各模块指针 */
	FaceDetector *m_faceDetector;
	FaceRecognizer *m_faceRecognizer;
	V4L2CaptureThread *m_captureThread;  /* V4L2摄像头采集线程 */
	FaceProcessThread *m_faceProcessThread; /* 人脸检测+识别线程 */
	RFIDThread *m_rfidThread;             /* RFID刷卡线程 */

	/* V4L2帧到人脸处理线程的连接句柄（用于安全断开） */
	QMetaObject::Connection m_frameToFaceConnection;

	/* 系统状态 */
	SystemState m_state;
	bool m_ready;

	/* 自动关锁定时器（开锁后5秒自动关锁） */
	QTimer *m_autoLockTimer;
	int m_autoLockDelayMs;  /* 自动关锁延迟（毫秒） */

	/* 扫描超时定时器（人脸/刷卡10秒超时） */
	QTimer *m_scanTimeoutTimer;
	int m_scanTimeoutMs;    /* 扫描超时时间（毫秒） */
};

#endif // SYSTEMCONTROLLER_H