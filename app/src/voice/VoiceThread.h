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
 * 工作流程：
 * =========================
 * 1. ALSA录音（16kHz, 16bit, mono）
 * 2. 能量检测（判断是否有语音活动）
 * 3. MFCC特征提取（13系数，25ms帧，10ms步进）
 * 4. DTW模板匹配（与预录模板比较）
 * 5. 识别成功发射信号
 */

#ifndef VOICETHREAD_H
#define VOICETHREAD_H

#include <QThread>
#include <QString>
#include <QVector>
#include <atomic>

/*
 * MFCC特征帧结构
 * 每帧包含13个MFCC系数
 */
struct MFCFrame {
    QVector<float> coefficients;  /* 13个MFCC系数 */
};

/*
 * DTW语音识别线程
 */
class VoiceThread : public QThread
{
    Q_OBJECT

public:
    explicit VoiceThread(QObject *parent = nullptr);
    ~VoiceThread();

    /* 停止监听 */
    void stopListening();

signals:
    /* 识别到语音命令 */
    void voiceCommandDetected(const QString &command);

    /* 设备错误 */
    void deviceError(const QString &error);

    /* 录音状态变化 */
    void recordingStarted();
    void recordingStopped();

protected:
    void run() override;

private:
    std::atomic<bool> m_running;

    /* ALSA录音相关 */
    bool initALSA();
    void closeALSA();
    bool recordAudio(QVector<int16_t> &buffer, int durationMs);
    void *m_pcmHandle;  /* snd_pcm_t* */

    /* MFCC特征提取 */
    QVector<MFCFrame> extractMFCC(const QVector<int16_t> &audio);
    void preEmphasis(QVector<int16_t> &audio);
    QVector<float> hammingWindow(int size);

    /* DTW模板匹配 */
    float dtwDistance(const QVector<MFCFrame> &seq1, const QVector<MFCFrame> &seq2);
    bool loadTemplate(const QString &command, QVector<MFCFrame> &frames);
    bool saveTemplate(const QString &command, const QVector<MFCFrame> &frames);

    /* 能量检测（判断是否有语音） */
    bool hasVoiceActivity(const QVector<int16_t> &audio, float threshold = 200.0f);

    /* 模板文件路径 */
    QString getTemplatePath(const QString &command);
};

#endif // VOICETHREAD_H
