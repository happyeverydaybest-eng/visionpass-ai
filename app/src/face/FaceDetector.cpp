/*
 * VisionPass 人脸检测器实现
 *
 * 核心流程：
 * 1. QImage → cv::Mat（Qt图像转OpenCV格式）
 * 2. 缩小图像（加速检测，开发板上用1/2分辨率即可）
 * 3. cv::CascadeClassifier::detectMultiScale() 检测人脸
 * 4. cv::Mat → QRect 坐标转换（还原到原始图像尺寸）
 */

#include "FaceDetector.h"
#include <QDebug>

/* 默认Haar级联文件路径（开发板上） */
static const QString DEFAULT_CASCADE_PATH =
	"/opt/visionpass/model/haarcascade_frontalface_alt2.xml";

FaceDetector::FaceDetector(const QString &cascadePath, QObject *parent)
	: QObject(parent), m_minFaceSize(80), m_loaded(false)
{
	/* 如果没有指定路径，使用默认路径 */
	QString path = cascadePath.isEmpty() ? DEFAULT_CASCADE_PATH : cascadePath;

	/*
	 * 加载Haar级联XML文件
	 * cv::CascadeClassifier::load()：
	 *   参数：XML文件路径
	 *   返回值：true=加载成功，false=加载失败（文件不存在或格式错误）
	 */
	if (!m_cascade.load(path.toStdString())) {
		qWarning() << "FaceDetector: Failed to load cascade from" << path;
		qWarning() << "  请确认模型文件已部署到开发板";
		m_loaded = false;
	} else {
		qInfo() << "FaceDetector: Cascade loaded from" << path;
		m_loaded = true;
	}
}

bool FaceDetector::isLoaded() const
{
	return m_loaded;
}

void FaceDetector::setMinFaceSize(int size)
{
	/* 最小人脸尺寸不能小于10像素 */
	m_minFaceSize = (size < 10) ? 10 : size;
}

int FaceDetector::minFaceSize() const
{
	return m_minFaceSize;
}

QVector<QRect> FaceDetector::detectFromQImage(const QImage &image)
{
	if (!m_loaded) {
		qWarning() << "FaceDetector: Not loaded, skipping detection";
		return QVector<QRect>();
	}

	/*
	 * QImage → cv::Mat 转换步骤：
	 * 1. QImage::Format_RGB32 → 先转Format_RGB888（去掉Alpha通道）
	 * 2. RGB888 → BGR（OpenCV内部用BGR顺序，Qt用RGB）
	 * 3. 用cv::Mat包装数据指针（零拷贝，不分配新内存）
	 */
	QImage converted = image.convertToFormat(QImage::Format_RGB888);
	converted = converted.rgbSwapped();  /* RGB → BGR（OpenCV格式） */

	/*
	 * 构造cv::Mat：
	 *   rows = 图像高度（像素行数）
	 *   cols = 图像宽度（像素列数）
	 *   type = CV_8UC3（8位无符号，3通道=RGB）
	 *   data = converted.bits()（直接使用QImage的数据指针）
	 */
	cv::Mat frame(converted.height(), converted.width(), CV_8UC3,
		      const_cast<unsigned char *>(converted.bits()),
		      converted.bytesPerLine());

	return detectFromMat(frame);
}

QVector<QRect> FaceDetector::detectFromMat(const cv::Mat &frame)
{
	QVector<QRect> result;

	if (frame.empty()) {
		qWarning() << "FaceDetector: Empty frame";
		return result;
	}

	/*
	 * 缩小图像以加速检测
	 * scale_factor = 0.5 → 图像缩小为原来的一半
	 * 在528MHz的ARM上，缩小一半可以让检测速度提升约4倍
	 * 检测完后会将坐标还原到原始尺寸
	 */
	double scale_factor = 0.5;
	cv::Mat small_frame;
	cv::resize(frame, small_frame, cv::Size(),
		   scale_factor, scale_factor, cv::INTER_LINEAR);

	/*
	 * 转灰度图（Haar检测只需要亮度信息，不需要颜色）
	 * cv::cvtColor：颜色空间转换
	 *   COLOR_BGR2GRAY：BGR三通道 → 灰度单通道
	 * 灰度图只有1个通道，处理更快
	 */
	cv::Mat gray;
	cv::cvtColor(small_frame, gray, cv::COLOR_BGR2GRAY);

	/*
	 * detectMultiScale()参数详解：
	 *   gray：输入灰度图
	 *   faces：输出检测结果的向量（每个元素是一个矩形cv::Rect）
	 *   1.1：缩放因子（每次扫描图像放大10%，形成"图像金字塔"）
	 *        值越小检测越精确但越慢，值越大检测越快但可能漏检
	 *   3：相邻检测窗口合并阈值（至少3个检测重叠才认为是人脸）
	 *      值越大越严格（减少误检），值越小越宽松（可能误检）
	 *   cv::Size(m_minFaceSize/2, m_minFaceSize/2)：最小检测窗口
	 *      因为图像已缩小一半，所以最小尺寸也要除以2
	 */
	std::vector<cv::Rect> cv_faces;
	m_cascade.detectMultiScale(gray, cv_faces, 1.1, 3, 0,
				   cv::Size(m_minFaceSize / 2, m_minFaceSize / 2));

	/*
	 * 将cv::Rect转换为QRect，并还原到原始图像尺寸
	 * cv::Rect(x, y, width, height) → QRect(x, y, width, height)
	 * 因为检测在缩小后的图像上做的，所以坐标要乘以2还原
	 */
	for (const cv::Rect &r : cv_faces) {
		int x = static_cast<int>(r.x / scale_factor);
		int y = static_cast<int>(r.y / scale_factor);
		int w = static_cast<int>(r.width / scale_factor);
		int h = static_cast<int>(r.height / scale_factor);
		result.append(QRect(x, y, w, h));
	}

	return result;
}