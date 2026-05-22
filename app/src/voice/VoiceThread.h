/*
 * VisionPass 语音识别线程（DTW方案）
 *
 * 功能说明（初学者必读）：
 * =========================
 * 通过开发板上的WM8960音频芯片录音，使用DTW（动态时间规整）
 * 模板匹配方法识别"开门"等语音指令。
 *
 * 为什么用DTW而不是Sherpa-ONNX？
 * =========================
 * - Sherpa-ONNX需要C++17，但我们的GCC 4.9.4只支持到C++11
 * - DTW是纯C++实现，无外部依赖，适合嵌入式环境
 * - DTW原理：将录音提取MFCC特征，与预存的模板进行动态时间规整比对
 *
 * 当前状态：占位实现
 * =========================
 * 这是一个功能框架，后续需要实现：
 * 1. ALSA录音（通过/dev/snd或libasound）
 * 2. MFCC特征提取（梅尔频率倒谱系数）
 * 3. DTW模板匹配算法
 * 4. 模板采集和存储
 *
 * 目前的实现会立即返回"未检测到语音"，
 * 但保留了完整的接口供后续实现使用。
 */

#ifndef VOICETHREAD_H
#define VOICETHREAD_H

#include <QThread>
#include <atomic>

class VoiceThread : public QThread
{
	Q_OBJECT

public:
	explicit VoiceThread(QObject *parent = nullptr);
	~VoiceThread();

	/* 请求停止监听 */
	void stopListening();

signals:
	/* 检测到语音指令"开门" */
	void voiceCommandDetected(const QString &command);
	/* 设备错误 */
	void deviceError(const QString &error);

protected:
	void run() override;

private:
	std::atomic<bool> m_running;
};

#endif // VOICETHREAD_H