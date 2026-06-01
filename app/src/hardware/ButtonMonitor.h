/**
 * @file ButtonMonitor.h
 * @brief 物理按键监控类 — 通过 sysfs GPIO 接口读取板载 KEY0 按钮状态
 *
 * 使用 QTimer 轮询 /sys/class/gpio/gpio18/value，
 * 检测下降沿（按下）后发射 buttonPressed 信号。
 * 采用与 IRSensorMonitor 相同的 QObject + QTimer 模式。
 */
#ifndef BUTTONMONITOR_H
#define BUTTONMONITOR_H

#include <QObject>
#include <QString>

class QTimer;

class ButtonMonitor : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param gpioNumber GPIO 编号（默认 18，即 GPIO1_IO18 = 32*0 + 18）
     * @param parent 父对象
     */
    explicit ButtonMonitor(int gpioNumber = 18, QObject *parent = nullptr);
    ~ButtonMonitor();

    /**
     * @brief 启动按键监控
     * @return true 成功启动，false GPIO 初始化失败
     */
    bool start();

    /**
     * @brief 停止按键监控
     */
    void stop();

    /**
     * @brief 检查是否正在监控
     */
    bool isRunning() const { return m_running; }

signals:
    /** @brief 按键按下信号（下降沿检测） */
    void buttonPressed();

    /** @brief 设备错误信号 */
    void deviceError(const QString &error);

private slots:
    /** @brief 定时轮询 GPIO 值 */
    void poll();

private:
    /** @brief 导出 GPIO 节点 */
    bool exportGpio();
    /** @brief 取消导出 GPIO 节点 */
    void unexportGpio();
    /** @brief 设置 GPIO 方向为输入 */
    bool setDirection();

    int m_gpioNumber;       ///< GPIO 编号（sysfs 编号）
    QString m_gpioPath;     ///< GPIO value 文件路径
    int m_fd;               ///< GPIO value 文件描述符
    bool m_lastState;       ///< 上一次读取的电平状态（true=高，false=低）
    bool m_running;         ///< 是否正在监控
    QTimer *m_pollTimer;    ///< 轮询定时器
};

#endif // BUTTONMONITOR_H
