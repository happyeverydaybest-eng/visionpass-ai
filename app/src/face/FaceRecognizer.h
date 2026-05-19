/*
 * VisionPass 人脸识别器（NCNN MobileFaceNet）
 *
 * 功能说明（初学者必读）：
 * =========================
 * 人脸识别的完整流程分为三步：
 *
 * 第1步：人脸检测（FaceDetector完成）
 *   输入：摄像头图像
 *   输出：人脸矩形区域 [x, y, width, height]
 *
 * 第2步：特征提取（本类完成）
 *   输入：人脸区域的图像
 *   处理：通过NCNN MobileFaceNet神经网络，把人脸图像变成128维特征向量
 *   输出：128个浮点数组成的向量（例如 [0.12, -0.34, 0.56, ...]）
 *   耗时：约200ms（在528MHz ARM上）
 *
 * 第3步：特征比对
 *   计算当前人脸特征向量与数据库中已注册特征向量的余弦相似度
 *   相似度 > 阈值(0.6) → 识别成功，返回用户名
 *   相似度 < 阈值 → 陌生人，识别失败
 *
 * 什么是"特征向量"？
 * =========================
 * MobileFaceNet把一张112x112的人脸图片"压缩"成128个数字。
 * 这128个数字代表了这张脸的关键特征（眼睛间距、鼻子形状等）。
 * 同一个人的两张照片，特征向量非常接近（余弦相似度>0.9）
 * 不同人的特征向量差异很大（余弦相似度<0.3）
 *
 * 什么是"余弦相似度"？
 * =========================
 * 余弦相似度 = cos(angle) = dot(A,B) / (|A| * |B|)
 * 取值范围：-1.0 ~ 1.0
 *   1.0 → 两个向量完全相同（同一个人）
 *   0.0 → 两个向量无关（不同人）
 *  -1.0 → 两个向量相反（不可能出现在人脸特征中）
 *
 * 在人脸识别中，阈值通常设为0.6：
 *   相似度 > 0.6 → 认为是同一个人
 *   相似度 < 0.6 → 认为是不同的人
 */

#ifndef FACERECOGNIZER_H
#define FACERECOGNIZER_H

#include <QObject>
#include <QImage>
#include <QRect>
#include <QVector>
#include <QString>
#include <opencv2/opencv.hpp>
#include "net.h"  /* NCNN头文件 */

/*
 * 人脸特征向量类型
 * MobileFaceNet输出128维浮点向量
 */
typedef QVector<float> FaceFeature;

/*
 * 识别结果结构体
 */
struct RecognizeResult {
	QString userId;      /* 用户ID（数据库中的唯一标识） */
	QString userName;    /* 用户姓名 */
	float similarity;    /* 余弦相似度（0~1之间） */
	bool matched;        /* 是否匹配成功（similarity >= threshold） */
};

class FaceRecognizer : public QObject
{
	Q_OBJECT

public:
	/*
	 * 构造函数
	 * 参数 modelPath：NCNN模型文件目录路径
	 *   目录中应包含 MobileFaceNet.param（网络结构）和 MobileFaceNet.bin（权重数据）
	 *   默认路径：/opt/visionpass/model/
	 * 参数 threshold：识别阈值（余弦相似度，默认0.6）
	 */
	explicit FaceRecognizer(const QString &modelPath = "",
				float threshold = 0.6f,
				QObject *parent = nullptr);

	/* 模型是否加载成功 */
	bool isLoaded() const;

	/* 设置识别阈值 */
	void setThreshold(float threshold);
	float threshold() const;

	/*
	 * 从人脸图像中提取特征向量
	 * 参数 faceImage：包含单个人脸的图像
	 * 参数 faceRect：人脸矩形区域（在faceImage中的位置）
	 * 返回值：128维特征向量
	 *
	 * 这是识别流程的核心步骤：
	 * 1. 从faceImage中裁剪出faceRect区域
	 * 2. 调整大小到112x112（MobileFaceNet的输入尺寸）
	 * 3. 通过NCNN推理，得到128维特征向量
	 */
	FaceFeature extractFeature(const QImage &faceImage, const QRect &faceRect);

	/*
	 * 同上，但直接接受裁剪好的人脸图像（已调整到112x112）
	 * 参数 alignedFace：已对齐的112x112人脸图像
	 */
	FaceFeature extractFeatureFromAligned(const QImage &alignedFace);

	/*
	 * 计算两个特征向量的余弦相似度
	 * 参数 feat1：第一个特征向量
	 * 参数 feat2：第二个特征向量
	 * 返回值：相似度（0~1之间）
	 *
	 * 公式：similarity = dot(feat1, feat2) / (norm(feat1) * norm(feat2))
	 */
	static float cosineSimilarity(const FaceFeature &feat1, const FaceFeature &feat2);

	/*
	 * 与数据库中的特征进行比对
	 * 参数 currentFeature：当前检测到的人脸特征
	 * 参数 databaseFeatures：数据库中已注册的所有特征（userId → 特征向量）
	 * 参数 databaseNames：数据库中已注册的所有姓名（userId → 姓名）
	 * 返回值：最佳匹配结果
	 *
	 * 遍历数据库中所有已注册特征，找到相似度最高的那个
	 */
	RecognizeResult recognize(const FaceFeature &currentFeature,
				  const QMap<QString, FaceFeature> &databaseFeatures,
				  const QMap<QString, QString> &databaseNames);

signals:
	/* 识别完成信号 */
	void recognitionComplete(const RecognizeResult &result);

private:
	ncnn::Net m_net;          /* NCNN网络对象 */
	float m_threshold;        /* 识别阈值 */
	bool m_loaded;            /* 模型是否加载成功 */

	/* NCNN推理输入尺寸 */
	static const int INPUT_WIDTH = 112;
	static const int INPUT_HEIGHT = 112;

	/*
	 * QImage → ncnn::Mat 转换
	 * NCNN输入要求：float类型，3通道(RGB)，值范围[-1, 1]
	 * 步骤：
	 * 1. QImage → cv::Mat（RGB888格式）
	 * 2. cv::Mat → ncnn::Mat（float, 3通道）
	 * 3. 像素值归一化：(pixel / 255.0 - 0.5) / 0.5 = pixel/127.5 - 1.0
	 */
	ncnn::Mat qimageToNcnnMat(const QImage &image);
};

#endif // FACERECOGNIZER_H