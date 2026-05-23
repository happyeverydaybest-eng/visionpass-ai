/*
 * VisionPass V4L2摄像头采集线程实现
 *
 * 核心逻辑：V4L2 mmap采集循环
 * 参考原版capture_thread.cpp中的V4L2实现，但适配Qt信号/槽机制
 */

#include "V4L2CaptureThread.h"
#include <QDebug>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>

V4L2CaptureThread::V4L2CaptureThread(const QString &devicePath, QObject *parent)
	: QThread(parent),
	  m_devicePath(devicePath),
	  m_fd(-1),
	  m_width(640),
	  m_height(480),
	  m_running(false),
	  m_deviceOpened(false)
{
	/* 初始化缓冲区指针 */
	for (int i = 0; i < VIDEO_BUFFER_COUNT; i++) {
		m_buffers[i].start = nullptr;
		m_buffers[i].length = 0;
	}
}

V4L2CaptureThread::~V4L2CaptureThread()
{
	stopCapture();
	wait();  /* 等待线程退出 */
	closeDevice();
}

bool V4L2CaptureThread::openDevice()
{
	if (m_deviceOpened)
		return true;

	/*
	 * 步骤1：打开摄像头设备
	 * O_RDWR — 可读可写（V4L2需要读写权限）
	 * O_NONBLOCK — 非阻塞模式（查询状态时不会阻塞）
	 */
	m_fd = open(m_devicePath.toStdString().c_str(), O_RDWR | O_NONBLOCK, 0);
	if (m_fd < 0) {
		qWarning() << "V4L2: Cannot open" << m_devicePath << ":" << strerror(errno);
		return false;
	}

	/*
	 * 步骤2：查询并设置视频格式
	 * VIDIOC_S_FMT — 设置视频格式（Set Format）
	 */
	struct v4l2_format fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;        /* 视频捕获模式 */
	fmt.fmt.pix.width = m_width;                     /* 宽度640 */
	fmt.fmt.pix.height = m_height;                   /* 高度480 */
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;   /* UYVY格式（OV2640默认） */
	fmt.fmt.pix.field = V4L2_FIELD_NONE;            /* 逐行扫描 */

	if (ioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0) {
		qWarning() << "V4L2: VIDIOC_S_FMT failed:" << strerror(errno);
		close(m_fd);
		m_fd = -1;
		return false;
	}

	/*
	 * 驱动可能不支持请求的精确分辨率，
	 * 所以用VIDIOC_G_FMT读取实际设置的分辨率
	 */
	m_width = fmt.fmt.pix.width;
	m_height = fmt.fmt.pix.height;
	qInfo() << "V4L2: Format set to" << m_width << "x" << m_height
		<< "pixelformat=" << QString::fromLatin1((char*)&fmt.fmt.pix.pixelformat, 4);

	/* 步骤3：初始化mmap缓冲区 */
	if (!initBuffers()) {
		freeBuffers();  /* 释放已分配的部分缓冲区 */
		close(m_fd);
		m_fd = -1;
		return false;
	}

	m_deviceOpened = true;
	qInfo() << "V4L2: Device opened successfully";
	return true;
}

void V4L2CaptureThread::closeDevice()
{
	if (m_fd < 0)
		return;

	/* 停止视频流 */
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ioctl(m_fd, VIDIOC_STREAMOFF, &type);

	/* 释放mmap缓冲区 */
	freeBuffers();

	/* 关闭设备 */
	close(m_fd);
	m_fd = -1;
	m_deviceOpened = false;

	qInfo() << "V4L2: Device closed";
}

bool V4L2CaptureThread::initBuffers()
{
	/*
	 * 步骤3.1：请求内核分配缓冲区
	 * VIDIOC_REQBUFS — Request Buffers
	 * 请求3个缓冲区（VIDEO_BUFFER_COUNT = 3）
	 */
	struct v4l2_requestbuffers req;
	memset(&req, 0, sizeof(req));
	req.count = VIDEO_BUFFER_COUNT;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (ioctl(m_fd, VIDIOC_REQBUFS, &req) < 0) {
		qWarning() << "V4L2: VIDIOC_REQBUFS failed:" << strerror(errno);
		return false;
	}

	/* 检查内核实际分配的缓冲区数量 */
	if (req.count < VIDEO_BUFFER_COUNT) {
		qWarning() << "V4L2: Insufficient buffer memory";
		return false;
	}

	/*
	 * 步骤3.2：查询并mmap每个缓冲区
	 * 对每个缓冲区：
	 *   VIDIOC_QUERYBUF — 查询缓冲区信息
	 *   mmap() — 将内核缓冲区映射到用户空间
	 *   VIDIOC_QBUF — 将缓冲区放入采集队列
	 */
	for (int i = 0; i < VIDEO_BUFFER_COUNT; i++) {
		struct v4l2_buffer buf;
		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		/* 查询缓冲区 */
		if (ioctl(m_fd, VIDIOC_QUERYBUF, &buf) < 0) {
			qWarning() << "V4L2: VIDIOC_QUERYBUF failed:" << strerror(errno);
			return false;
		}

		/* mmap映射缓冲区到用户空间 */
		m_buffers[i].length = buf.length;
		m_buffers[i].start = mmap(nullptr, buf.length,
					  PROT_READ | PROT_WRITE,
					  MAP_SHARED, m_fd, buf.m.offset);

		if (m_buffers[i].start == MAP_FAILED) {
			qWarning() << "V4L2: mmap failed:" << strerror(errno);
			return false;
		}

		/* 将缓冲区放入采集队列 */
		if (ioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
			qWarning() << "V4L2: VIDIOC_QBUF failed:" << strerror(errno);
			return false;
		}
	}

	qInfo() << "V4L2:" << VIDEO_BUFFER_COUNT << "buffers initialized";
	return true;
}

void V4L2CaptureThread::freeBuffers()
{
	for (int i = 0; i < VIDEO_BUFFER_COUNT; i++) {
		if (m_buffers[i].start != nullptr && m_buffers[i].start != MAP_FAILED) {
			munmap(m_buffers[i].start, m_buffers[i].length);
			m_buffers[i].start = nullptr;
			m_buffers[i].length = 0;
		}
	}
}

void V4L2CaptureThread::stopCapture()
{
	m_running = false;
}

bool V4L2CaptureThread::isOpen() const
{
	return m_deviceOpened;
}

/*
 * 线程主函数 — V4L2采集循环
 * =========================
 * 这是核心采集逻辑，在单独线程中运行。
 */
void V4L2CaptureThread::run()
{
	if (!m_deviceOpened) {
		qWarning() << "V4L2: Device not opened, cannot start capture";
		return;
	}

	/*
	 * 步骤4：启动视频流
	 * VIDIOC_STREAMON — 开始视频采集
	 */
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(m_fd, VIDIOC_STREAMON, &type) < 0) {
		qWarning() << "V4L2: VIDIOC_STREAMON failed:" << strerror(errno);
		return;
	}

	m_running = true;
	qInfo() << "V4L2: Capture started";

	/*
	 * 步骤5：采集主循环
	 * =================
	 * 每次循环：
	 *   1. DQBUF — 取出已填充的帧
	 *   2. 构造QImage
	 *   3. emit frameReady
	 *   4. QBUF — 将缓冲区放回队列
	 *   5. 控制帧率（msleep）
	 */
	while (m_running) {
		struct v4l2_buffer buf;
		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;

		/*
		 * DQBUF（Dequeue Buffer）：取出内核已填充的帧
		 * 如果暂时没有新帧，ioctl会阻塞等待（直到STREAMOFF或超时）
		 * 这里用select检查可读性，避免永久阻塞
		 */
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(m_fd, &fds);
		struct timeval tv;
		tv.tv_sec = 1;     /* 1秒超时 */
		tv.tv_usec = 0;

		int r = select(m_fd + 1, &fds, nullptr, nullptr, &tv);
		if (r < 0) {
			qWarning() << "V4L2: select failed:" << strerror(errno);
			break;
		}
		if (r == 0) {
			/* 超时，继续循环 */
			continue;
		}

		/* 取出已填充的缓冲区 */
		if (ioctl(m_fd, VIDIOC_DQBUF, &buf) < 0) {
			if (errno == EAGAIN) {
				/* 非阻塞模式下无帧就绪，正常情况，继续循环 */
				continue;
			}
			qWarning() << "V4L2: VIDIOC_DQBUF failed:" << strerror(errno);
			continue;
		}

		/*
		 * 构造QImage
		 * =============
		 * 摄像头数据格式：UYVY（每2个像素4字节）
		 * UYVY: U0 Y0 V0 Y1 U2 Y2 V2 Y3 ...
		 * 每4字节 = 2个像素（Y0Y1 共享 U0V0）
		 *
		 * 需要转换为 RGB888 格式
		 */
		unsigned char *frameData =
			static_cast<unsigned char *>(m_buffers[buf.index].start);

		QImage safeFrame(m_width, m_height, QImage::Format_RGB888);

		/* UYVY 转 RGB888 */
		for (int row = 0; row < m_height; row++) {
			unsigned char *uyvy_row = frameData + row * m_width * 2;
			unsigned char *rgb_row = safeFrame.scanLine(row);

			for (int col = 0; col < m_width; col += 2) {
				/* UYVY格式：U0 Y0 V0 Y1 */
				int u = uyvy_row[col * 2];
				int y0 = uyvy_row[col * 2 + 1];
				int v = uyvy_row[col * 2 + 2];
				int y1 = uyvy_row[col * 2 + 3];

				/* YUV 转 RGB */
				int c0 = y0 - 16;
				int c1 = y1 - 16;
				int d = u - 128;
				int e = v - 128;

				int r0 = (298 * c0 + 409 * e + 128) >> 8;
				int g0 = (298 * c0 - 100 * d - 208 * e + 128) >> 8;
				int b0 = (298 * c0 + 516 * d + 128) >> 8;

				int r1 = (298 * c1 + 409 * e + 128) >> 8;
				int g1 = (298 * c1 - 100 * d - 208 * e + 128) >> 8;
				int b1 = (298 * c1 + 516 * d + 128) >> 8;

				/* 限制范围 0-255 */
				rgb_row[col * 3] = qBound(0, r0, 255);
				rgb_row[col * 3 + 1] = qBound(0, g0, 255);
				rgb_row[col * 3 + 2] = qBound(0, b0, 255);

				rgb_row[col * 3 + 3] = qBound(0, r1, 255);
				rgb_row[col * 3 + 4] = qBound(0, g1, 255);
				rgb_row[col * 3 + 5] = qBound(0, b1, 255);
			}
		}

		/* 发射信号（Qt会自动跨线程队列） */
		emit frameReady(safeFrame);

		/*
		 * 将缓冲区放回队列，供内核填充下一帧
		 */
		if (ioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
			qWarning() << "V4L2: VIDIOC_QBUF failed:" << strerror(errno);
		}

		/*
		 * 帧率控制：约3fps（每帧间隔330ms）
		 * 528MHz ARM处理人脸检测+识别约230ms，
		 * 所以3fps足够，太快会导致CPU满载
		 */
		msleep(330);
	}

	/*
	 * 步骤6：停止视频流
	 */
	ioctl(m_fd, VIDIOC_STREAMOFF, &type);
	m_running = false;

	qInfo() << "V4L2: Capture stopped";
}