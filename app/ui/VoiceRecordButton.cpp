/*
 * VisionPass 长按录音按钮实现
 *
 * 录音线程持续读取ALSA数据，计时器更新UI。
 * 长按开始录音，松手停止，时长>=1秒才发送。
 */

#include "VoiceRecordButton.h"
#include <QMouseEvent>
#include <QThread>
#include <QDebug>
#include <cstring>
#include <alsa/asoundlib.h>

static const int REC_SAMPLE_RATE = 16000;
static const int REC_CHANNELS = 1;
static const int REC_MAX_MS = 15000;
static const int REC_MIN_MS = 1000;

/* 录音线程：持续读取ALSA数据到缓冲区 */
class RecordThread : public QThread
{
public:
	RecordThread(snd_pcm_t *pcm, QVector<int16_t> *buf, std::atomic<bool> *running)
		: m_pcm(pcm), m_buffer(buf), m_running(running) {}

protected:
	void run() override {
		int16_t chunk[1024];
		while (*m_running) {
			int n = snd_pcm_readi(m_pcm, chunk, 1024);
			if (n == -EPIPE) {
				snd_pcm_prepare(m_pcm);
			} else if (n < 0) {
				qWarning() << "RecordThread: read error:" << snd_strerror(n);
				break;
			} else {
				for (int i = 0; i < n; i++)
					m_buffer->append(chunk[i]);
			}
		}
	}

private:
	snd_pcm_t *m_pcm;
	QVector<int16_t> *m_buffer;
	std::atomic<bool> *m_running;
};

VoiceRecordButton::VoiceRecordButton(QWidget *parent)
	: QPushButton(parent),
	  m_pcmHandle(nullptr),
	  m_tickTimer(nullptr),
	  m_elapsedMs(0),
	  m_recording(false)
{
	setText("按住说话");
	setFixedSize(100, 40);
	setStyleSheet(
		"QPushButton { background-color: #3498db; color: white;"
		"  border: none; border-radius: 6px; font-size: 14px; font-weight: bold; }"
		"QPushButton:pressed { background-color: #e74c3c; }"
	);

	m_tickTimer = new QTimer(this);
	m_tickTimer->setInterval(100);
	connect(m_tickTimer, &QTimer::timeout, this, &VoiceRecordButton::onRecordTick);
}

VoiceRecordButton::~VoiceRecordButton()
{
	if (m_pcmHandle) {
		snd_pcm_close(static_cast<snd_pcm_t*>(m_pcmHandle));
	}
}

void VoiceRecordButton::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;

	/* 打开ALSA设备 */
	snd_pcm_t *pcm;
	int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_CAPTURE, 0);
	if (err < 0) {
		qWarning() << "VoiceRecordButton: ALSA open failed:" << snd_strerror(err);
		return;
	}

	snd_pcm_hw_params_t *params;
	snd_pcm_hw_params_alloca(&params);
	snd_pcm_hw_params_any(pcm, params);
	snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16_LE);
	snd_pcm_hw_params_set_channels(pcm, params, REC_CHANNELS);
	unsigned int rate = REC_SAMPLE_RATE;
	snd_pcm_hw_params_set_rate_near(pcm, params, &rate, nullptr);
	err = snd_pcm_hw_params(pcm, params);
	if (err < 0) {
		qWarning() << "VoiceRecordButton: ALSA params failed:" << snd_strerror(err);
		snd_pcm_close(pcm);
		return;
	}

	/* 预热：丢弃前200ms */
	int warmupN = REC_SAMPLE_RATE * 200 / 1000;
	QVector<int16_t> warmup(warmupN);
	int w = warmupN;
	while (w > 0) {
		int n = snd_pcm_readi(pcm, warmup.data() + (warmupN - w), qMin(w, 1024));
		if (n > 0) w -= n;
		else if (n == -EPIPE) snd_pcm_prepare(pcm);
		else break;
	}

	m_pcmHandle = pcm;
	m_audioBuffer.clear();
	m_audioBuffer.reserve(REC_SAMPLE_RATE * REC_MAX_MS / 1000);
	m_elapsedMs = 0;
	m_recording = true;

	/* 设置麦克风增益：降低硬件增益减少噪声，用软件增益补偿 */
	system("amixer cset numid=9 1 2>/dev/null");  /* LINPUT1 Volume 低 */
	system("amixer cset numid=7 2 2>/dev/null");  /* LINPUT2 Volume 低 */
	system("amixer cset numid=8 1 2>/dev/null");  /* RINPUT1 Volume 低 */
	system("amixer cset numid=5 2 2>/dev/null");  /* RINPUT2 Volume 低 */
	system("amixer cset numid=1 40 2>/dev/null"); /* Capture Volume 中等 */

	/* 启动录音线程 */
	m_recordRunning = true;
	m_recordThread = new RecordThread(pcm, &m_audioBuffer, &m_recordRunning);
	m_recordThread->start();

	/* UI反馈 */
	setStyleSheet(
		"QPushButton { background-color: #e74c3c; color: white;"
		"  border: none; border-radius: 6px; font-size: 14px; font-weight: bold; }"
	);
	setText("松手发送");
	m_tickTimer->start();

	QPushButton::mousePressEvent(event);
}

void VoiceRecordButton::mouseReleaseEvent(QMouseEvent *event)
{
	if (!m_recording)
		return;

	m_recording = false;
	m_tickTimer->stop();

	/* 停止录音线程 */
	m_recordRunning = false;
	if (m_recordThread) {
		m_recordThread->wait(2000);
		delete m_recordThread;
		m_recordThread = nullptr;
	}

	/* 关闭ALSA */
	if (m_pcmHandle) {
		snd_pcm_drop(static_cast<snd_pcm_t*>(m_pcmHandle));
		snd_pcm_close(static_cast<snd_pcm_t*>(m_pcmHandle));
		m_pcmHandle = nullptr;
	}

	/* 恢复样式 */
	setStyleSheet(
		"QPushButton { background-color: #3498db; color: white;"
		"  border: none; border-radius: 6px; font-size: 14px; font-weight: bold; }"
		"QPushButton:pressed { background-color: #e74c3c; }"
	);
	setText("按住说话");

	int durationSec = m_elapsedMs / 1000;

	if (m_elapsedMs < REC_MIN_MS) {
		qInfo() << "VoiceRecordButton: Too short (" << m_elapsedMs << "ms), cancelled";
		emit recordingCancelled();
		return;
	}

	/* 软件增益补偿（硬件增益降低后需要放大） */
	const float GAIN = 4.0f;
	for (int i = 0; i < m_audioBuffer.size(); i++) {
		float s = m_audioBuffer[i] * GAIN;
		if (s > 32767) s = 32767;
		if (s < -32768) s = -32768;
		m_audioBuffer[i] = (int16_t)s;
	}

	QByteArray audioData(reinterpret_cast<const char*>(m_audioBuffer.data()),
			     m_audioBuffer.size() * sizeof(int16_t));

	/* 调试：检查录音数据是否有内容 */
	int nonzero = 0;
	int16_t maxSample = 0;
	for (int i = 0; i < m_audioBuffer.size(); i++) {
		if (m_audioBuffer[i] != 0) nonzero++;
		if (qAbs(m_audioBuffer[i]) > maxSample)
			maxSample = qAbs(m_audioBuffer[i]);
	}
	qInfo() << "VoiceRecordButton: Recorded" << durationSec << "s,"
		<< m_audioBuffer.size() << "samples,"
		<< "nonzero=" << nonzero << ", maxSample=" << maxSample
		<< ", dataSize=" << audioData.size() << "bytes";

	emit voiceRecorded(audioData, durationSec);

	QPushButton::mouseReleaseEvent(event);
}

void VoiceRecordButton::onRecordTick()
{
	m_elapsedMs += 100;
	setText(QString("录音 %1s").arg(m_elapsedMs / 1000));

	if (m_elapsedMs >= REC_MAX_MS) {
		/* 自动停止 */
		QMouseEvent fakeRelease(QEvent::MouseButtonRelease, QPointF(),
					Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		mouseReleaseEvent(&fakeRelease);
	}
}
