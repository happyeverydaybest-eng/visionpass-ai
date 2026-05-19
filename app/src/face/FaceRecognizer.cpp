/*
 * VisionPass 人脸识别器实现
 *
 * 核心流程：
 * 1. 裁剪+对齐人脸图像到112x112
 * 2. QImage → ncnn::Mat（像素归一化到[-1,1]）
 * 3. NCNN推理：输入112x112图像 → 输出128维特征向量
 * 4. 计算余弦相似度与数据库比对
 */

#include "FaceRecognizer.h"
#include <QDebug>
#include <QElapsedTimer>
#include <cmath>

/* 默认模型目录路径（开发板上） */
static const QString DEFAULT_MODEL_PATH = "/opt/visionpass/model/";

/* MobileFaceNet模型文件名 */
static const QString PARAM_FILE = "MobileFaceNet.param";
static const QString BIN_FILE = "MobileFaceNet.bin";

FaceRecognizer::FaceRecognizer(const QString &modelPath, float threshold,
			       QObject *parent)
	: QObject(parent), m_threshold(threshold), m_loaded(false)
{
	QString dir = modelPath.isEmpty() ? DEFAULT_MODEL_PATH : modelPath;
	QString paramPath = dir + PARAM_FILE;
	QString binPath = dir + BIN_FILE;

	/*
	 * 加载NCNN模型
	 * ncnn::Net::load_param()：加载网络结构文件（.param，文本格式）
	 * ncnn::Net::load_model()：加载权重数据文件（.bin，二进制格式）
	 *
	 * .param文件描述了网络的层次结构（哪些层、输入输出关系）
	 * .bin文件包含了每层的权重参数（浮点数数组）
	 *
	 * 这两个文件必须配套使用，缺一不可
	 */
	int ret_param = m_net.load_param(paramPath.toStdString().c_str());
	int ret_bin = m_net.load_model(binPath.toStdString().c_str());

	if (ret_param != 0 || ret_bin != 0) {
		qWarning() << "FaceRecognizer: Failed to load model";
		qWarning() << "  param:" << paramPath << "(ret=" << ret_param << ")";
		qWarning() << "  bin:" << binPath << "(ret=" << ret_bin << ")";
		qWarning() << "  请确认模型文件已部署到开发板";
		m_loaded = false;
	} else {
		qInfo() << "FaceRecognizer: Model loaded from" << dir;
		m_loaded = true;
	}
}

bool FaceRecognizer::isLoaded() const
{
	return m_loaded;
}

void FaceRecognizer::setThreshold(float threshold)
{
	/* 阈值范围0~1，通常人脸识别用0.5~0.7 */
	m_threshold = (threshold < 0.0f) ? 0.0f :
		      (threshold > 1.0f) ? 1.0f : threshold;
}

float FaceRecognizer::threshold() const
{
	return m_threshold;
}

FaceFeature FaceRecognizer::extractFeature(const QImage &faceImage,
					     const QRect &faceRect)
{
	if (!m_loaded) {
		qWarning() << "FaceRecognizer: Not loaded, returning empty feature";
		return FaceFeature();
	}

	/*
	 * 步骤1：裁剪人脸区域
	 * 从原始图像中裁剪出faceRect指定的矩形区域
	 */
	QImage cropped = faceImage.copy(faceRect);

	/*
	 * 步骤2：调整大小到112x112
	 * MobileFaceNet要求输入图像尺寸为112x112像素
	 * Qt::SmoothTransformation：使用高质量缩放算法（双线性插值）
	 */
	QImage aligned = cropped.scaled(INPUT_WIDTH, INPUT_HEIGHT,
					Qt::IgnoreAspectRatio,
					Qt::SmoothTransformation);

	/* 步骤3：调用NCNN推理 */
	return extractFeatureFromAligned(aligned);
}

FaceFeature FaceRecognizer::extractFeatureFromAligned(const QImage &alignedFace)
{
	FaceFeature feature;

	if (!m_loaded)
		return feature;

	QElapsedTimer timer;
	timer.start();

	/* 将QImage转换为NCNN输入格式 */
	ncnn::Mat input_mat = qimageToNcnnMat(alignedFace);

	/*
	 * NCNN推理（最关键的步骤）
	 * =========================
	 * NCNN推理流程：先创建Extractor对象，再设置输入，再提取输出
	 *
	 * ncnn::Net::create_extractor()：创建一个推理器实例
	 *   每次推理前都要创建新的Extractor（不是Net本身直接推理）
	 *
	 * extractor.input()：设置输入数据
	 *   "data"：输入层的blob名称（与.param文件中定义一致）
	 *   input_mat：输入数据（112x112x3，float，范围[-1,1]）
	 *
	 * extractor.extract()：提取输出数据
	 *   "fc1"：输出层的blob名称（MobileFaceNet最后一层）
	 *   output_mat：输出数据（1x1x128，float）
	 *
	 * 在ARM上约200ms（528MHz CPU）
	 */
	ncnn::Extractor ex = m_net.create_extractor();
	ex.input("data", input_mat);
	ncnn::Mat output_mat;
	int ret = ex.extract("fc1", output_mat);

	if (ret != 0) {
		qWarning() << "FaceRecognizer: NCNN extract failed, ret=" << ret;
		return feature;
	}

	/*
	 * 从ncnn::Mat中提取128维特征向量
	 * output_mat.w = 128（特征维度）
	 * output_mat的数据是连续的float数组
	 * 用指针直接访问每个元素
	 */
	float *data = (float *)output_mat.data;
	for (int i = 0; i < output_mat.w; i++) {
		feature.append(data[i]);
	}

	qInfo() << "FaceRecognizer: Feature extracted in" << timer.elapsed() << "ms"
		<< "dims=" << feature.size();

	return feature;
}

float FaceRecognizer::cosineSimilarity(const FaceFeature &feat1,
					const FaceFeature &feat2)
{
	/* 两个向量维度不同，无法计算 */
	if (feat1.size() != feat2.size() || feat1.isEmpty())
		return 0.0f;

	/*
	 * 余弦相似度公式：
	 *   similarity = dot(A, B) / (|A| * |B|)
	 *
	 * dot(A, B) = sum(A[i] * B[i])     （向量内积）
	 * |A| = sqrt(sum(A[i] * A[i]))      （向量长度/范数）
	 *
	 * 例如：
	 *   A = [1, 2, 3], B = [4, 5, 6]
	 *   dot = 1*4 + 2*5 + 3*6 = 32
	 *   |A| = sqrt(1+4+9) = sqrt(14) ≈ 3.74
	 *   |B| = sqrt(16+25+36) = sqrt(77) ≈ 8.77
	 *   similarity = 32 / (3.74 * 8.77) ≈ 0.97
	 */
	float dot_product = 0.0f;   /* 内积 */
	float norm1 = 0.0f;         /* 向量1的平方和 */
	float norm2 = 0.0f;         /* 向量2的平方和 */

	for (int i = 0; i < feat1.size(); i++) {
		dot_product += feat1[i] * feat2[i];
		norm1 += feat1[i] * feat1[i];
		norm2 += feat2[i] * feat2[i];
	}

	/* 防止除零（如果向量全为0，范数为0） */
	float denominator = sqrtf(norm1) * sqrtf(norm2);
	if (denominator < 1e-6f)
		return 0.0f;

	return dot_product / denominator;
}

RecognizeResult FaceRecognizer::recognize(const FaceFeature &currentFeature,
					  const QMap<QString, FaceFeature> &databaseFeatures,
					  const QMap<QString, QString> &databaseNames)
{
	RecognizeResult bestResult;
	bestResult.matched = false;
	bestResult.similarity = 0.0f;
	bestResult.userId = "";
	bestResult.userName = "陌生人";

	if (currentFeature.isEmpty() || databaseFeatures.isEmpty())
		return bestResult;

	/*
	 * 遍历数据库中所有已注册用户，找到相似度最高的
	 * 这是最简单的1:N识别方式
	 * 对于小规模数据库（<100人），遍历速度足够快
	 *
	 * 对于大规模数据库（>1000人），应该用更高效的索引方式
	 * 但在门禁场景中，注册用户通常不超过50人
	 */
	float maxSimilarity = -1.0f;
	QString bestUserId;

	for (const QString &userId : databaseFeatures.keys()) {
		float sim = cosineSimilarity(currentFeature, databaseFeatures[userId]);
		if (sim > maxSimilarity) {
			maxSimilarity = sim;
			bestUserId = userId;
		}
	}

	bestResult.similarity = maxSimilarity;
	bestResult.userId = bestUserId;
	bestResult.userName = databaseNames.value(bestUserId, "未知");

	/* 判断是否超过阈值 */
	bestResult.matched = (maxSimilarity >= m_threshold);

	qInfo() << "FaceRecognizer: Best match =" << bestResult.userName
		<< "similarity =" << maxSimilarity
		<< "threshold =" << m_threshold
		<< "matched =" << bestResult.matched;

	return bestResult;
}

ncnn::Mat FaceRecognizer::qimageToNcnnMat(const QImage &image)
{
	/*
	 * QImage → ncnn::Mat 转换详解
	 * ============================
	 *
	 * NCNN输入要求：
	 *   数据类型：float（32位浮点）
	 *   通道数：3（RGB）
	 *   值范围：[-1.0, 1.0]
	 *   尺寸：112 x 112
	 *
	 * 转换步骤：
	 * 1. QImage转RGB888格式（确保3通道，无Alpha）
	 * 2. 像素归一化：(pixel/255.0 - 0.5) / 0.5 = pixel/127.5 - 1.0
	 *    这个公式把[0,255]映射到[-1,1]
	 * 3. ncnn::Mat按通道存储（CHW格式），即先R通道所有像素，再G通道，再B通道
	 *    注意：NCNN默认按RGB顺序（不是OpenCV的BGR）
	 */
	QImage rgb = image.convertToFormat(QImage::Format_RGB888);

	int w = rgb.width();
	int h = rgb.height();

	/* 创建ncnn::Mat：3通道，宽w，高h */
	ncnn::Mat mat(w, h, 3);

	/*
	 * 逐像素转换
	 * NCNN的Mat数据布局：先第0通道(R)所有像素，再第1通道(G)，再第2通道(B)
	 * mat.channel(c) 返回第c通道的float指针
	 * mat.channel(c).row(y) 返回第y行的float指针
	 */
	for (int y = 0; y < h; y++) {
		const uchar *src_line = rgb.constScanLine(y);
		float *dst_r = mat.channel(0).row(y);
		float *dst_g = mat.channel(1).row(y);
		float *dst_b = mat.channel(2).row(y);

		for (int x = 0; x < w; x++) {
			/* RGB888格式：每像素3字节，顺序R-G-B */
			uchar r = src_line[x * 3 + 0];
			uchar g = src_line[x * 3 + 1];
			uchar b = src_line[x * 3 + 2];

			/* 归一化到[-1, 1] */
			dst_r[x] = (r / 127.5f) - 1.0f;
			dst_g[x] = (g / 127.5f) - 1.0f;
			dst_b[x] = (b / 127.5f) - 1.0f;
		}
	}

	return mat;
}