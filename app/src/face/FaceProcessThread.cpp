/*
 * VisionPass 人脸处理线程实现
 *
 * 核心流程：
 * 1. 从队列取出QImage帧
 * 2. FaceDetector检测人脸矩形
 * 3. FaceRecognizer提取128维特征
 * 4. 与数据库比对（余弦相似度）
 * 5. 发射结果信号
 */

#include "FaceProcessThread.h"
#include "src/database/UserDatabase.h"
#include <QDebug>

FaceProcessThread::FaceProcessThread(FaceDetector *detector,
				     FaceRecognizer *recognizer,
				     UserDatabase *database,
				     QObject *parent)
	: QThread(parent),
	  m_detector(detector),
	  m_recognizer(recognizer),
	  m_database(database),
	  m_running(false),
	  m_frameSkip(3),        /* 默认每3帧处理1帧 */
	  m_frameCounter(0),
	  m_cooldownMs(3000),    /* 默认3秒冷却 */
	  m_inCooldown(false),
	  m_dbLoaded(false)
{
}

FaceProcessThread::~FaceProcessThread()
{
	stopProcessing();
	wait(5000);  /* 等待最多5秒 */
}

void FaceProcessThread::addFrame(const QImage &frame)
{
	QMutexLocker locker(&m_mutex);

	/*
	 * 限制队列长度，避免内存无限增长
	 * 如果队列超过5帧，丢弃最旧的一帧
	 */
	if (m_frameQueue.size() >= 5) {
		m_frameQueue.dequeue();
	}

	m_frameQueue.enqueue(frame);
	m_waitCondition.wakeOne();  /* 唤醒等待的线程 */
}

void FaceProcessThread::stopProcessing()
{
	QMutexLocker locker(&m_mutex);
	m_running = false;
	m_waitCondition.wakeAll();  /* 唤醒所有等待的线程 */
}

void FaceProcessThread::setCooldownMs(int ms)
{
	m_cooldownMs = qMax(1000, ms);  /* 至少1秒 */
}

void FaceProcessThread::setFrameSkip(int skip)
{
	m_frameSkip = qMax(1, skip);  /* 至少1帧处理1帧 */
}

void FaceProcessThread::run()
{
	if (!m_detector || !m_recognizer) {
		qWarning() << "FaceProcessThread: detector or recognizer not set";
		return;
	}

	m_running = true;
	m_frameCounter = 0;

	qInfo() << "FaceProcessThread: Started, frameSkip=" << m_frameSkip
		<< "cooldownMs=" << m_cooldownMs;

	/* 每次线程启动时重新加载数据库特征（避免缓存过期） */
	if (m_database) {
		m_dbFeatures = m_database->getAllFaceFeatures();
		m_dbNames = m_database->getAllUserNames();
		m_dbLoaded = true;
		qInfo() << "FaceProcessThread: Loaded" << m_dbFeatures.size()
			<< "face features from database";
	}

	while (m_running) {
		QImage frame;

		/*
		 * 从队列取出帧
		 * 如果队列为空，等待最多500ms
		 */
		{
			QMutexLocker locker(&m_mutex);
			if (m_frameQueue.isEmpty()) {
				m_waitCondition.wait(&m_mutex, 500);
				if (m_frameQueue.isEmpty())
					continue;
			}
			frame = m_frameQueue.dequeue();
		}

		/* 跳帧：只处理每N帧中的1帧 */
		m_frameCounter++;
		if (m_frameCounter % m_frameSkip != 0)
			continue;

		/* 冷却检查 */
		if (m_inCooldown && m_cooldownTimer.elapsed() < m_cooldownMs)
			continue;
		m_inCooldown = false;

		/* 执行人脸检测+识别 */
		FaceProcessResult result = processFrame(frame);

		/* 如果检测到人脸，发射信号 */
		if (result.hasFace) {
			emit recognitionResult(result);

			/* 识别成功，进入冷却期 */
			if (result.matched) {
				m_inCooldown = true;
				m_cooldownTimer.start();
			}
		}
	}

	qInfo() << "FaceProcessThread: Stopped";
}

/*
 * 人脸检测+识别核心逻辑
 * =====================
 * 步骤：
 * 1. FaceDetector::detectFromQImage() → 人脸矩形列表
 * 2. 如果检测到人脸，取最大的人脸
 * 3. FaceRecognizer::extractFeature() → 128维特征向量
 * 4. FaceRecognizer::recognize() → 比对结果
 */
FaceProcessResult FaceProcessThread::processFrame(const QImage &frame)
{
	FaceProcessResult result;
	result.hasFace = false;
	result.matched = false;
	result.similarity = 0.0f;

	/* 步骤1：人脸检测 */
	QVector<QRect> faces = m_detector->detectFromQImage(frame);

	if (faces.isEmpty())
		return result;

	/* 取最大的人脸（假设离摄像头最近） */
	QRect maxFace;
	int maxArea = 0;
	for (const QRect &face : faces) {
		int area = face.width() * face.height();
		if (area > maxArea) {
			maxArea = area;
			maxFace = face;
		}
	}

	/* 发射人脸检测信号（用于在视频上画框） */
	emit facesDetected(faces);

	/* 步骤2：特征提取 */
	FaceFeature feature = m_recognizer->extractFeature(frame, maxFace);

	if (feature.isEmpty())
		return result;

	/* 步骤3：特征比对（使用已加载的数据库特征） */
	if (!m_dbFeatures.isEmpty()) {
		RecognizeResult recResult = m_recognizer->recognize(feature, m_dbFeatures, m_dbNames);

		result.hasFace = true;
		result.faceRect = maxFace;
		result.userId = recResult.userId;
		result.userName = recResult.userName;
		result.similarity = recResult.similarity;
		result.matched = recResult.matched;
	} else {
		/* 数据库为空，只检测人脸不识别 */
		result.hasFace = true;
		result.faceRect = maxFace;
		result.matched = false;
	}

	return result;
}