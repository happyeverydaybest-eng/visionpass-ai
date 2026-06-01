/*
 * VisionPass 长按录音按钮
 *
 * 功能：类似微信语音，长按开始录音，松手停止并发射信号
 * 录音格式：16kHz, 16bit, 单声道 PCM
 * 最大时长：15秒
 */

#ifndef VOICERECORDBUTTON_H
#define VOICERECORDBUTTON_H

#include <QPushButton>
#include <QTimer>
#include <QVector>
#include <atomic>
#include <cstdint>

class RecordThread;

class VoiceRecordButton : public QPushButton
{
	Q_OBJECT

public:
	explicit VoiceRecordButton(QWidget *parent = nullptr);
	~VoiceRecordButton();

signals:
	/* 录音完成，携带PCM数据和时长(秒) */
	void voiceRecorded(const QByteArray &audioData, int durationSec);
	/* 录音取消（时间太短） */
	void recordingCancelled();

protected:
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
	void onRecordTick();

private:
	bool startRecording();
	void stopRecording();

	void *m_pcmHandle;           /* snd_pcm_t* */
	QVector<int16_t> m_audioBuffer; /* 录音缓冲区 */
	QTimer *m_tickTimer;         /* 录音计时器 */
	int m_elapsedMs;             /* 已录音时长(ms) */
	bool m_recording;            /* 是否正在录音 */
	RecordThread *m_recordThread; /* 录音线程 */
	std::atomic<bool> m_recordRunning; /* 录音线程运行标志 */
};

#endif // VOICERECORDBUTTON_H
