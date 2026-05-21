/*
 * VisionPass V4L2摄像头采集线程
 *
 * 功能说明（初学者必读）：
 * =========================
 * 这个类负责从OV2640摄像头读取视频帧，并通过Qt信号发送给主界面显示。
 *
 * 为什么不用QtMultimedia？
 * =========================
 * 我们的ARM版Qt没有编译Multimedia模块（需要额外依赖），
 * 而Linux内核自带的V4L2接口可以直接访问摄像头。
 * V4L2（Video for Linux 2）是Linux的标准视频采集API，
 * 任何摄像头驱动都支持它。
 *
 * V4L2采集流程（mmap方式）：
 * =========================
 * 1. open("/dev/video0") — 打开摄像头设备
 * 2. ioctl(VIDIOC_S_FMT) — 设置视频格式（RGB565，640x480）
 * 3. ioctl(VIDIOC_REQBUFS) — 请求内核分配缓冲区
 * 4. ioctl(VIDIOC_QUERYBUF) + mmap() — 将缓冲区映射到用户空间
 * 5. ioctl(VIDIOC_QBUF) — 将缓冲区放入队列（供内核填充）
 * 6. ioctl(VIDIOC_STREAMON) — 开始视频流
 * 7. 循环：
 *      ioctl(VIDIOC_DQBUF) — 取出已填充的帧
 *      构造QImage → emit frameReady
 *      ioctl(VIDIOC_QBUF) — 将缓冲区放回队列
 * 8. ioctl(VIDIOC_STREAMOFF) — 停止视频流
 * 9. munmap() + close() — 释放资源
 *
 * 关于RGB565格式：
 * =========================
 * OV2640默认输出RGB565格式（16位色深）。
 * RGB565：R(5bit) + G(6bit) + B(5bit) = 16bit/像素
 * 640x480 RGB565 = 640×480×2 = 614400字节/帧
 * Qt的QImage::Format_RGB16正好对应RGB565。
 *
 * 帧率控制：
 * =========================
 * 528MHz ARM处理人脸检测+识别约230ms，
 * 所以设置3fps（每帧间隔330ms）足够。
 * 太快会导致CPU满载，UI卡顿。
 */

#ifndef V4L2CAPTURETHREAD_H
#define V4L2CAPTURETHREAD_H

#include <QThread>
#include <QImage>
#include <linux/videodev2.h>

/* 缓冲区数量 */
#define VIDEO_BUFFER_COUNT 3

/*
 * V4L2视频采集线程
 *
 * 使用方法：
 *   V4L2CaptureThread capture;
 *   connect(&capture, &V4L2CaptureThread::frameReady, this, &MyClass::onFrameReady);
 *   capture.start();  // 启动线程
 *   ...
 *   capture.stopCapture();  // 请求停止
 *   capture.wait();         // 等待线程退出
 */
class V4L2CaptureThread : public QThread
{
	Q_OBJECT

public:
	explicit V4L2CaptureThread(const QString &devicePath = "/dev/video0",
				   QObject *parent = nullptr);
	~V4L2CaptureThread();

	/*
	 * 打开摄像头设备并初始化V4L2
	 * 返回值：true=成功，false=失败
	 */
	bool openDevice();

	/*
	 * 关闭摄像头设备并释放资源
	 */
	void closeDevice();

	/*
	 * 请求线程停止采集（线程安全）
	 * 在run()循环中会检查此标志
	 */
	void stopCapture();

	/* 设备是否已打开 */
	bool isOpen() const;

	/* 获取当前视频宽度和高度 */
	int width() const { return m_width; }
	int height() const { return m_height; }

signals:
	/*
	 * 每采集到一帧就发射此信号
	 * 参数 frame：QImage对象（RGB565格式）
	 */
	void frameReady(const QImage &frame);

protected:
	/*
	 * 线程主函数（QThread::run()的重写）
	 * 包含V4L2采集的主循环
	 */
	void run() override;

private:
	/*
	 * 初始化V4L2缓冲区（mmap方式）
	 * 申请3个缓冲区，通过mmap映射到用户空间
	 */
	bool initBuffers();

	/*
	 * 释放V4L2缓冲区
	 */
	void freeBuffers();

	QString m_devicePath;   /* 摄像头设备路径，默认"/dev/video0" */
	int m_fd;               /* 设备文件描述符 */
	int m_width;            /* 视频宽度 */
	int m_height;           /* 视频高度 */
	bool m_running;         /* 线程运行标志 */
	bool m_deviceOpened;    /* 设备是否已打开 */

	/*
	 * V4L2缓冲区结构
	 * 每个缓冲区包含：
	 *   start — mmap返回的用户空间指针
	 *   length — 缓冲区长度（字节）
	 */
	struct Buffer {
		void *start;
		size_t length;
	};
	Buffer m_buffers[VIDEO_BUFFER_COUNT];  /* 3个缓冲区 */
};

#endif // V4L2CAPTURETHREAD_H