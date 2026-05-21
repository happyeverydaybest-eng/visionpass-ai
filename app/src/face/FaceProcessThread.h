/*
 * VisionPass 人脸处理线程
 *
 * 核心职责（初学者必读）：
 * =========================
 * 这个线程接收摄像头采集的QImage帧，在后台执行：
 * 1. 人脸检测（Haar级联，~30ms）
 * 2. 特征提取（NCNN MobileFaceNet，~200ms）
 * 3. 特征比对（余弦相似度，~1ms）
 * 4. 发射识别结果给MainWindow
 *
 * 为什么是独立线程？
 * =========================
 * 人脸检测+识别约230ms，如果放在主线程会导致UI卡顿。
 * 在后台线程执行，UI可以保持流畅。
 *
 * 帧率控制策略：
 * =========================
 * - 只处理每3帧中的1帧（跳帧），节省CPU
 * - 识别成功后进入3秒冷却期，期间不处理新帧
 * - 未检测到人脸时降低检测频率
 */

#ifndef FACEPROCESSTHREAD_H
#define FACEPROCESSTHREAD_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QImage>
#include <QRect>
#include <QElapsedTimer>
#include <atomic>
#include "src/face/FaceDetector.h"
#include "src/face/FaceRecognizer.h"

/*
 * 人脸处理结果
 */
struct FaceProcessResult {
	bool hasFace;           /* 是否检测到人脸 */
	QRect faceRect;         /* 人脸矩形（在图像中的位置） */
	QString userId;         /* 匹配的用户ID */
	QString userName;       /* 匹配的用户姓名 */
	float similarity;       /* 相似度（0~1） */
	bool matched;           /* 是否匹配成功 */
};

class FaceProcessThread : public QThread
{
	Q_OBJECT

public:
	explicit FaceProcessThread(FaceDetector *detector,
				   FaceRecognizer *recognizer,
				   QObject *parent = nullptr);
	~FaceProcessThread();

	/*
	 * 添加一帧到处理队列
	 * 主线程调用，将QImage放入线程安全的队列
	 */
	void addFrame(const QImage &frame);

	/*
	 * 请求线程停止
	 */
	void stopProcessing();

	/* 设置识别冷却时间（毫秒） */
	void setCooldownMs(int ms);

	/* 设置跳帧间隔（每N帧处理1帧） */
	void setFrameSkip(int skip);

signals:
	/*
	 * 人脸检测结果（用于在视频上画框）
	 * 参数：人脸矩形列表
	 */
	void facesDetected(const QVector<QRect> &faces);

	/*
	 * 人脸识别结果
	 * 参数：识别结果
	 */
	void recognitionResult(const FaceProcessResult &result);

protected:
	void run() override;

private:
	/* 人脸检测+识别核心逻辑 */
	FaceProcessResult processFrame(const QImage &frame);

	/* 线程安全的帧队列 */
	QMutex m_mutex;
	QWaitCondition m_waitCondition;
	QQueue<QImage> m_frameQueue;

	/* 识别器 */
	FaceDetector *m_detector;
	FaceRecognizer *m_recognizer;

	/* 线程控制 */
	std::atomic<bool> m_running;

	/* 帧率控制 */
	int m_frameSkip;        /* 跳帧间隔（默认3帧处理1帧） */
	int m_frameCounter;     /* 帧计数器 */

	/* 识别冷却 */
	int m_cooldownMs;       /* 冷却时间（毫秒） */
	QElapsedTimer m_cooldownTimer;  /* 冷却计时器 */
	bool m_inCooldown;    /* 是否在冷却中 */
};

#endif // FACEPROCESSTHREAD_H