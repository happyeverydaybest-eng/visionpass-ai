/*
 * VisionPass 人脸检测器（OpenCV Haar级联）
 *
 * 功能说明（初学者必读）：
 * =========================
 * 人脸检测和人脸识别是两个不同的步骤：
 * - 人脸检测：在一张照片中找到"人脸在哪里"（返回矩形坐标）
 * - 人脸识别：判断"这个人脸是谁"（提取特征并比对数据库）
 *
 * 本类只负责第一步——用OpenCV的Haar级联分类器检测人脸位置。
 * Haar级联是一种传统的机器学习方法（不是深度学习），原理：
 * 1. 训练时：用大量人脸/非人脸图片，学习出一组"特征模板"
 * 2. 检测时：用这些模板在图片上滑动扫描，找到匹配人脸的区域
 * 3. 优点：速度快（在528MHz的ARM上约30ms），不需要GPU
 * 4. 缺点：对侧脸、遮挡、光照变化比较敏感
 *
 * 为什么用Haar级联而不是深度学习检测？
 * - 开发板只有528MHz CPU + 512MB RAM，跑深度学习检测模型太慢
 * - Haar级联在ARM上约30ms，可以满足实时需求（每秒3帧即可）
 * - NCNN MobileFaceNet只用来做第二步（特征提取），约200ms
 * - 所以整个流程：检测30ms + 特征提取200ms ≈ 230ms/帧
 */

#ifndef FACEDETECTOR_H
#define FACEDETECTOR_H

#include <QObject>
#include <QImage>
#include <QRect>
#include <QVector>
#include <opencv2/opencv.hpp>

class FaceDetector : public QObject
{
	Q_OBJECT

public:
	/*
	 * 构造函数
	 * 参数 cascadePath：Haar级联XML文件路径
	 *   默认使用 /opt/visionpass/model/haarcascade_frontalface_alt2.xml
	 *   这个文件是OpenCV自带的人脸检测模型，约528KB
	 */
	explicit FaceDetector(const QString &cascadePath = "",
			      QObject *parent = nullptr);

	/*
	 * 从QImage中检测人脸
	 * 参数 image：Qt格式的图像（来自摄像头）
	 * 返回值：检测到的每个人脸的矩形区域列表
	 *
	 * 使用方法：
	 *   QVector<QRect> faces = detector.detectFromQImage(cameraImage);
	 *   for (const QRect &face : faces) {
	 *       qDebug() << "Found face at" << face;
	 *   }
	 */
	QVector<QRect> detectFromQImage(const QImage &image);

	/*
	 * 从cv::Mat中检测人脸（内部使用，也可外部直接调用）
	 * 参数 frame：OpenCV格式的图像矩阵
	 * 返回值：检测到的每个人脸的矩形区域列表
	 */
	QVector<QRect> detectFromMat(const cv::Mat &frame);

	/*
	 * 设置最小人脸尺寸（像素）
	 * 参数 size：最小人脸矩形的宽/高
	 *   开发板摄像头分辨率较低时，设为60~80
	 *   分辨率较高时，设为100~150
	 *   太小会导致误检（把小物体误认为人脸）
	 *   太大会漏检远处的人脸
	 */
	void setMinFaceSize(int size);

	/* 获取当前最小人脸尺寸 */
	int minFaceSize() const;

	/* 检测器是否已成功加载（构造后应检查此值） */
	bool isLoaded() const;

private:
	cv::CascadeClassifier m_cascade;  /* OpenCV Haar级联分类器对象 */
	int m_minFaceSize;                 /* 最小人脸尺寸（像素） */
	bool m_loaded;                     /* 是否加载成功 */
};

#endif // FACEDETECTOR_H