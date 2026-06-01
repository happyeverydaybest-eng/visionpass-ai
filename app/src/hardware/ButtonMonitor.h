/**
 * @file ButtonMonitor.h
 * @brief 物理按键监控类 — 通过 Linux input event 子系统读取板载 KEY0 按钮
 *
 * GPIO1_IO18 已被内核 gpio_keys 驱动占用，通过 /dev/input/eventN 暴露。
 * 使用 QThread 阻塞读取 input event，检测按键按下后发射 buttonPressed 信号。
 */
#ifndef BUTTONMONITOR_H
#define BUTTONMONITOR_H

#include <QThread>
#include <QString>
#include <atomic>

class ButtonMonitor : public QThread
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param eventDevice input event 设备路径（如 /dev/input/event2）
     * @param parent 父对象
     */
    explicit ButtonMonitor(const QString &eventDevice = "/dev/input/event2",
                           QObject *parent = nullptr);
    ~ButtonMonitor();

    /**
     * @brief 停止监控线程
     */
    void stopPolling();

signals:
    /** @brief 按键按下信号 */
    void buttonPressed();

    /** @brief 设备错误信号 */
    void deviceError(const QString &error);

protected:
    /** @brief 线程主循环 — 阻塞读取 input event */
    void run() override;

private:
    /** @brief 自动查找 gpio_keys 对应的 event 设备 */
    QString findGpioKeyEventDevice() const;

    QString m_eventDevice;          ///< input event 设备路径
    std::atomic<bool> m_running;    ///< 线程运行标志
};

#endif // BUTTONMONITOR_H
