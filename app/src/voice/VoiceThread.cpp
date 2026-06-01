/*
 * VisionPass 语音识别线程实现（DTW方案）
 *
 * 实现完整的DTW语音识别流水线：
 * 1. ALSA录音（libasound）
 * 2. MFCC特征提取
 * 3. DTW模板匹配
 */

#include "VoiceThread.h"
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QDataStream>
#include <cmath>
#include <cstring>

/* ALSA头文件 */
#include <alsa/asoundlib.h>

/* 音频参数 */
static const int SAMPLE_RATE = 16000;      /* 16kHz采样率 */
static const int CHANNELS = 1;             /* 单声道 */
static const int BITS_PER_SAMPLE = 16;     /* 16位 */

/* MFCC参数 */
static const int FRAME_SIZE = 400;         /* 25ms @ 16kHz = 400 samples */
static const int FRAME_STEP = 160;         /* 10ms @ 16kHz = 160 samples */
static const int NUM_MFCC = 13;            /* MFCC系数数量 */
static const int NUM_FILTERS = 26;         /* Mel滤波器数量 */
static const int FFT_SIZE = 512;           /* FFT点数 */

/* DTW参数 */
static const float DTW_THRESHOLD = 10.0f; /* DTW距离阈值（越小越相似） */

VoiceThread::VoiceThread(QObject *parent)
    : QThread(parent), m_running(false), m_pcmHandle(nullptr)
{
}

VoiceThread::~VoiceThread()
{
    stopListening();
    wait(5000);
}

void VoiceThread::stopListening()
{
    m_running = false;
}

bool VoiceThread::initALSA()
{
    snd_pcm_t *handle;
    int err;

    /* 打开录音设备 */
    err = snd_pcm_open(&handle, "default", SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        qWarning() << "ALSA: Cannot open device:" << snd_strerror(err);
        return false;
    }

    /* 设置硬件参数 */
    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(handle, params);

    /* 设置访问模式：交错模式 */
    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);

    /* 设置采样格式：16位小端 */
    snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);

    /* 设置声道数 */
    snd_pcm_hw_params_set_channels(handle, params, CHANNELS);

    /* 设置采样率 */
    unsigned int rate = SAMPLE_RATE;
    snd_pcm_hw_params_set_rate_near(handle, params, &rate, nullptr);

    /* 应用参数 */
    err = snd_pcm_hw_params(handle, params);
    if (err < 0) {
        qWarning() << "ALSA: Cannot set parameters:" << snd_strerror(err);
        snd_pcm_close(handle);
        return false;
    }

    /* 设置混音器：配置麦克风输入增益（使用amixer命令，避免API名称长度限制） */
    snd_mixer_t *mixer;
    if (snd_mixer_open(&mixer, 0) == 0) {
        if (snd_mixer_attach(mixer, "default") == 0) {
            /* 1. 使用amixer设置LINPUT1和LINPUT2 Volume（差分麦克风） */
            int ret1 = system("amixer cset numid=9 3 2>/dev/null");  /* Left Input Boost Mixer LINPUT1 Volume */
            int ret2 = system("amixer cset numid=7 7 2>/dev/null");  /* Left Input Boost Mixer LINPUT2 Volume */
            int ret3 = system("amixer cset numid=8 3 2>/dev/null");  /* Right Input Boost Mixer RINPUT1 Volume */
            int ret4 = system("amixer cset numid=5 7 2>/dev/null");  /* Right Input Boost Mixer RINPUT2 Volume */

            if (ret1 == 0 && ret2 == 0 && ret3 == 0 && ret4 == 0) {
                qInfo() << "ALSA: LINPUT1/2 and RINPUT1/2 Volume set to max via amixer";
            } else {
                qWarning() << "ALSA: Failed to set input volumes via amixer";
            }

            snd_mixer_selem_id_t *sid;
            snd_mixer_selem_id_alloca(&sid);

            /* 2. 设置Capture Volume到最大值 */
            snd_mixer_selem_id_set_index(sid, 0);
            snd_mixer_selem_id_set_name(sid, "Capture");
            snd_mixer_elem_t *capture_elem = snd_mixer_find_selem(mixer, sid);
            if (capture_elem) {
                long min, max;
                snd_mixer_selem_get_capture_volume_range(capture_elem, &min, &max);
                snd_mixer_selem_set_capture_volume_all(capture_elem, max);
                snd_mixer_selem_set_capture_switch_all(capture_elem, 1);
                qInfo() << "ALSA: Capture Volume set to max (" << max << ")";
            }

            /* 6. 设置ADC PCM Capture Volume到最大值 */
            snd_mixer_selem_id_set_index(sid, 0);
            snd_mixer_selem_id_set_name(sid, "ADC PCM Capture");
            snd_mixer_elem_t *adc_elem = snd_mixer_find_selem(mixer, sid);
            if (adc_elem) {
                long min, max;
                snd_mixer_selem_get_capture_volume_range(adc_elem, &min, &max);
                snd_mixer_selem_set_capture_volume_all(adc_elem, max);
                qInfo() << "ALSA: ADC PCM Capture Volume set to max (" << max << ")";
            }
        }
        snd_mixer_close(mixer);
    }

    m_pcmHandle = handle;
    qInfo() << "ALSA: Recording device opened (rate=" << rate << "Hz)";
    return true;
}

void VoiceThread::closeALSA()
{
    if (m_pcmHandle) {
        snd_pcm_close(static_cast<snd_pcm_t*>(m_pcmHandle));
        m_pcmHandle = nullptr;
    }
}

bool VoiceThread::recordAudio(QVector<int16_t> &buffer, int durationMs)
{
    if (!m_pcmHandle) return false;

    snd_pcm_t *handle = static_cast<snd_pcm_t*>(m_pcmHandle);

    /*
     * 预热阶段：读取并丢弃前200ms的音频
     * WM8960 codec启动需要时间，前几百毫秒数据不稳定
     * 预热后等待50ms让codec稳定
     */
    const int WARMUP_MS = 200;
    int warmupSamples = SAMPLE_RATE * WARMUP_MS / 1000;
    QVector<int16_t> warmupBuffer(warmupSamples);
    int warmupFrames = warmupSamples;

    while (warmupFrames > 0 && m_running) {
        int err = snd_pcm_readi(handle, warmupBuffer.data() + (warmupSamples - warmupFrames),
                               qMin(warmupFrames, 1024));
        if (err == -EPIPE) {
            snd_pcm_prepare(handle);
        } else if (err < 0) {
            qWarning() << "ALSA: Warmup read error:" << snd_strerror(err);
            break;
        } else {
            warmupFrames -= err;
        }
    }

    /* 预热后等待50ms，让codec稳定 */
    usleep(50000);

    /* 正式录音 */
    int numSamples = SAMPLE_RATE * durationMs / 1000;
    buffer.resize(numSamples);
    int frames = numSamples;
    int err;

    while (frames > 0 && m_running) {
        err = snd_pcm_readi(handle, buffer.data() + (numSamples - frames),
                           qMin(frames, 1024));
        if (err == -EPIPE) {
            /* 溢出，恢复 */
            snd_pcm_prepare(handle);
        } else if (err < 0) {
            qWarning() << "ALSA: Read error:" << snd_strerror(err);
            return false;
        } else {
            frames -= err;
        }
    }

    /*
     * 软件增益放大：将音量提升10倍
     * 如果硬件混音器设置不生效，用软件方式增强信号
     * 注意：放大后需要钳位到int16_t范围[-32768, 32767]
     */
    const float GAIN = 10.0f;
    for (int i = 0; i < buffer.size(); i++) {
        float sample = static_cast<float>(buffer[i]) * GAIN;
        if (sample > 32767.0f) sample = 32767.0f;
        if (sample < -32768.0f) sample = -32768.0f;
        buffer[i] = static_cast<int16_t>(sample);
    }

    /*
     * 重置PCM流状态：
     * snd_pcm_drop() - 立即停止，丢弃缓冲区数据
     * snd_pcm_prepare() - 重新准备，为下次录音做好准备
     * 这样可以确保每次录音都是干净的初始状态
     */
    snd_pcm_drop(handle);
    snd_pcm_prepare(handle);

    return frames == 0;
}

bool VoiceThread::hasVoiceActivity(const QVector<int16_t> &audio, float threshold)
{
    /*
     * 改进的语音活动检测：基于帧的检测
     * 将音频分成多个帧，只要有任何一帧的RMS超过阈值，就认为有语音
     * 这样可以避免静音段拉低整体平均值的问题
     */

    int frameSize = 1600;  // 100ms at 16kHz
    int numFrames = audio.size() / frameSize;

    int voicedFrames = 0;
    float maxFrameRMS = 0.0f;

    for (int frame = 0; frame < numFrames; frame++) {
        int startIdx = frame * frameSize;

        // 计算当前帧的RMS（注意：必须cast到double再乘，避免int16_t溢出！）
        double sum = 0.0;
        for (int i = 0; i < frameSize; i++) {
            double sample = static_cast<double>(audio[startIdx + i]);
            sum += sample * sample;
        }
        float frameRMS = std::sqrt(sum / frameSize);

        if (frameRMS > maxFrameRMS) {
            maxFrameRMS = frameRMS;
        }

        if (frameRMS > threshold) {
            voicedFrames++;
        }
    }

    // 统计信息
    int16_t maxVal = 0;
    int16_t minVal = 0;
    for (int i = 0; i < audio.size(); i++) {
        if (audio[i] > maxVal) maxVal = audio[i];
        if (audio[i] < minVal) minVal = audio[i];
    }

    qInfo() << "VoiceThread: Audio stats - Max frame RMS:" << maxFrameRMS
            << ", Voiced frames:" << voicedFrames << "/" << numFrames
            << ", Max:" << maxVal << ", Min:" << minVal
            << ", Threshold:" << threshold;

    // 如果有超过3%的帧有声音，就认为有语音活动（至少1帧）
    return voicedFrames >= 1;
}

void VoiceThread::preEmphasis(QVector<int16_t> &audio)
{
    /* 预加重滤波器：y[n] = x[n] - 0.97 * x[n-1] */
    for (int i = audio.size() - 1; i > 0; i--) {
        audio[i] = audio[i] - 0.97f * audio[i-1];
    }
}

QVector<float> VoiceThread::hammingWindow(int size)
{
    QVector<float> window(size);
    for (int i = 0; i < size; i++) {
        window[i] = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (size - 1));
    }
    return window;
}

/* 简单FFT实现（Cooley-Tukey算法） */
static void fft(QVector<float> &real, QVector<float> &imag, int n)
{
    if (n <= 1) return;

    /* 位反转重排 */
    int j = 0;
    for (int i = 0; i < n - 1; i++) {
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
        int k = n >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }

    /* 蝶形运算 */
    for (int len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * M_PI / len;
        float wreal = std::cos(angle);
        float wimag = std::sin(angle);

        for (int i = 0; i < n; i += len) {
            float curReal = 1.0f, curImag = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                float treal = curReal * real[i + k + len/2] - curImag * imag[i + k + len/2];
                float timag = curReal * imag[i + k + len/2] + curImag * real[i + k + len/2];

                real[i + k + len/2] = real[i + k] - treal;
                imag[i + k + len/2] = imag[i + k] - timag;
                real[i + k] += treal;
                imag[i + k] += timag;

                float newReal = curReal * wreal - curImag * wimag;
                curImag = curReal * wimag + curImag * wreal;
                curReal = newReal;
            }
        }
    }
}

QVector<MFCFrame> VoiceThread::extractMFCC(const QVector<int16_t> &audio)
{
    QVector<MFCFrame> frames;
    QVector<float> window = hammingWindow(FRAME_SIZE);

    /* 转换为float */
    QVector<float> signal(audio.size());
    for (int i = 0; i < audio.size(); i++) {
        signal[i] = static_cast<float>(audio[i]);
    }

    /* 分帧处理 */
    for (int start = 0; start + FRAME_SIZE <= signal.size(); start += FRAME_STEP) {
        /* 加窗 */
        QVector<float> frame(FFT_SIZE, 0.0f);
        for (int i = 0; i < FRAME_SIZE; i++) {
            frame[i] = signal[start + i] * window[i];
        }

        /* FFT */
        QVector<float> real = frame;
        QVector<float> imag(FFT_SIZE, 0.0f);
        fft(real, imag, FFT_SIZE);

        /* 功率谱 */
        QVector<float> power(FFT_SIZE / 2 + 1);
        for (int i = 0; i < power.size(); i++) {
            power[i] = (real[i] * real[i] + imag[i] * imag[i]) / FFT_SIZE;
        }

        /* Mel滤波器组（简化版） */
        QVector<float> melEnergies(NUM_FILTERS);
        float melLow = 0.0f;
        float melHigh = 2595.0f * std::log10(1.0f + SAMPLE_RATE / 2.0f / 700.0f);

        for (int m = 0; m < NUM_FILTERS; m++) {
            float melCenter = melLow + (melHigh - melLow) * (m + 1) / (NUM_FILTERS + 1);
            float hzCenter = 700.0f * (std::pow(10.0f, melCenter / 2595.0f) - 1.0f);
            int binCenter = hzCenter * FFT_SIZE / SAMPLE_RATE;

            /* 简单三角滤波器 */
            float energy = 0.0f;
            int binLow = qMax(0, binCenter - 2);
            int binHigh = qMin(power.size() - 1, binCenter + 2);
            for (int k = binLow; k <= binHigh; k++) {
                energy += power[k];
            }
            melEnergies[m] = std::log(energy + 1e-10f);
        }

        /* DCT得到MFCC */
        MFCFrame mfcc;
        mfcc.coefficients.resize(NUM_MFCC);
        for (int i = 0; i < NUM_MFCC; i++) {
            float sum = 0.0f;
            for (int j = 0; j < NUM_FILTERS; j++) {
                sum += melEnergies[j] * std::cos(M_PI * i * (j + 0.5f) / NUM_FILTERS);
            }
            mfcc.coefficients[i] = sum;
        }
        frames.append(mfcc);
    }

    return frames;
}

float VoiceThread::dtwDistance(const QVector<MFCFrame> &seq1, const QVector<MFCFrame> &seq2)
{
    int n = seq1.size();
    int m = seq2.size();
    if (n == 0 || m == 0) return 1e9f;

    /* 动态规划矩阵 */
    QVector<QVector<float>> dtw(n + 1, QVector<float>(m + 1, 1e9f));
    dtw[0][0] = 0.0f;

    /* 填充DTW矩阵 */
    for (int i = 1; i <= n; i++) {
        for (int j = 1; j <= m; j++) {
            /* 计算欧氏距离 */
            float dist = 0.0f;
            for (int k = 0; k < NUM_MFCC; k++) {
                float diff = seq1[i-1].coefficients[k] - seq2[j-1].coefficients[k];
                dist += diff * diff;
            }
            dist = std::sqrt(dist);

            /* 状态转移 */
            float minPrev = qMin(dtw[i-1][j], qMin(dtw[i][j-1], dtw[i-1][j-1]));
            dtw[i][j] = dist + minPrev;
        }
    }

    return dtw[n][m] / (n + m);  /* 归一化距离 */
}

QString VoiceThread::getTemplatePath(const QString &command)
{
    QDir dataDir("/opt/visionpass/data");
    if (!dataDir.exists()) {
        dataDir.mkpath(".");
    }
    return dataDir.filePath("voice_" + command + ".dtw");
}

bool VoiceThread::loadTemplate(const QString &command, QVector<MFCFrame> &frames)
{
    QFile file(getTemplatePath(command));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QDataStream in(&file);
    int numFrames;
    in >> numFrames;

    frames.clear();
    for (int i = 0; i < numFrames; i++) {
        MFCFrame frame;
        int numCoeffs;
        in >> numCoeffs;
        frame.coefficients.resize(numCoeffs);
        for (int j = 0; j < numCoeffs; j++) {
            in >> frame.coefficients[j];
        }
        frames.append(frame);
    }

    return !frames.isEmpty();
}

bool VoiceThread::saveTemplate(const QString &command, const QVector<MFCFrame> &frames)
{
    QFile file(getTemplatePath(command));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QDataStream out(&file);
    out << frames.size();
    for (const auto &frame : frames) {
        out << frame.coefficients.size();
        for (float coeff : frame.coefficients) {
            out << coeff;
        }
    }

    return true;
}

void VoiceThread::run()
{
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true)) {
        return;
    }

    qInfo() << "VoiceThread: Started";
    emit recordingStarted();

    /* 初始化ALSA */
    if (!initALSA()) {
        emit deviceError("无法打开录音设备");
        m_running = false;
        return;
    }

    /* 加载"开门"模板 */
    QVector<MFCFrame> templateFrames;
    bool hasTemplate = loadTemplate("开门", templateFrames);
    if (!hasTemplate) {
        qWarning() << "VoiceThread: No template for '开门', recording new template...";
        qInfo() << "VoiceThread: 请在3秒内对着麦克风说'开门'...";

        /* 录制新模板（3秒） */
        QVector<int16_t> audio;
        bool recordOk = recordAudio(audio, 3000);
        qInfo() << "VoiceThread: Recording completed, samples:" << audio.size()
                << ", success:" << recordOk;

        if (recordOk) {
            bool hasVoice = hasVoiceActivity(audio);
            qInfo() << "VoiceThread: Voice activity detected:" << hasVoice;

            if (hasVoice) {
                templateFrames = extractMFCC(audio);
                qInfo() << "VoiceThread: MFCC frames extracted:" << templateFrames.size();

                if (!templateFrames.isEmpty()) {
                    bool saveOk = saveTemplate("开门", templateFrames);
                    qInfo() << "VoiceThread: Template save result:" << saveOk;

                    if (saveOk) {
                        hasTemplate = true;
                        qInfo() << "VoiceThread: Template recorded and saved successfully!";
                    } else {
                        qWarning() << "VoiceThread: Failed to save template";
                    }
                } else {
                    qWarning() << "VoiceThread: MFCC extraction returned empty frames";
                }
            } else {
                qWarning() << "VoiceThread: No voice activity detected during recording";
            }
        } else {
            qWarning() << "VoiceThread: Audio recording failed";
        }
    }

    /* 主循环：持续录音和识别 */
    while (m_running) {
        QVector<int16_t> audio;

        /* 录制1.5秒 */
        if (!recordAudio(audio, 1500)) {
            msleep(100);
            continue;
        }

        /* 能量检测 */
        if (!hasVoiceActivity(audio)) {
            continue;
        }

        qInfo() << "VoiceThread: Voice activity detected";

        /* 提取MFCC */
        QVector<MFCFrame> frames = extractMFCC(audio);
        if (frames.isEmpty()) {
            continue;
        }

        /* DTW匹配 */
        if (hasTemplate) {
            float distance = dtwDistance(frames, templateFrames);
            qInfo() << "VoiceThread: DTW distance =" << distance
                    << "(threshold:" << DTW_THRESHOLD << ")";

            if (distance < DTW_THRESHOLD) {
                qInfo() << "VoiceThread: Command '开门' recognized!";
                emit voiceCommandDetected("开门");

                /* 识别成功后等待3秒，避免重复触发 */
                for (int i = 0; i < 30 && m_running; i++) {
                    msleep(100);
                }
            }
        }
    }

    closeALSA();
    m_running = false;
    emit recordingStopped();
    qInfo() << "VoiceThread: Stopped";
}
