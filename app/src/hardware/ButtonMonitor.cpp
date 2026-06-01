/**
 * @file ButtonMonitor.cpp
 * @brief 物理按键监控实现 — 通过 Linux input event 子系统读取 KEY0 按钮
 *
 * GPIO1_IO18 已被内核 gpio_keys 驱动占用（/sys/kernel/debug/gpio 显示 gpio-18 USER-KEY1），
 * 不能通过 sysfs 导出。改为使用 /dev/input/eventN 读取 input event。
 *
 * 工作原理：
 * 1. 打开 gpio_keys 对应的 /dev/input/eventN（通常是 event2）
 * 2. 阻塞 read() 等待 input_event 结构体
 * 3. 收到 EV_KEY 事件且 value=1（按下）时发射 buttonPressed 信号
 * 4. 无轮询，纯中断驱动，CPU 占用极低
 */
#include "ButtonMonitor.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <string.h>
#include <errno.h>

ButtonMonitor::ButtonMonitor(const QString &eventDevice, QObject *parent)
    : QThread(parent)
    , m_eventDevice(eventDevice)
    , m_running(false)
{
}

ButtonMonitor::~ButtonMonitor()
{
    stopPolling();
}

void ButtonMonitor::stopPolling()
{
    m_running = false;
    /* 等待线程退出（最多 2 秒） */
    wait(2000);
}

QString ButtonMonitor::findGpioKeyEventDevice() const
{
    /*
     * 遍历 /sys/class/input/ 查找 gpio_keys 设备对应的 eventN
     * 匹配条件：设备名称包含 "gpio_keys" 或 "gpio-keys"
     */
    QDir inputDir("/sys/class/input");
    for (const QString &entry : inputDir.entryList(QStringList() << "input*", QDir::Dirs)) {
        QString namePath = QString("/sys/class/input/%1/device/name").arg(entry);
        QFile nameFile(namePath);
        if (nameFile.open(QIODevice::ReadOnly)) {
            QString name = QString::fromLocal8Bit(nameFile.readAll().trimmed());
            if (name.contains("gpio_keys") || name.contains("gpio-keys")) {
                /* 找到 gpio_keys 设备，读取对应的 eventN */
                QString eventPath = QString("/sys/class/input/%1/event").arg(entry);
                QDir eventDir(eventPath);
                QStringList events = eventDir.entryList(QStringList() << "event*", QDir::Dirs);
                if (!events.isEmpty()) {
                    QString device = "/dev/input/" + events.first();
                    qDebug() << "ButtonMonitor: 找到 gpio_keys 设备:" << device
                             << "(名称:" << name << ")";
                    return device;
                }
            }
        }
    }
    return QString();
}

void ButtonMonitor::run()
{
    /* 如果未指定设备，自动查找 */
    if (m_eventDevice.isEmpty() || m_eventDevice == "auto") {
        QString found = findGpioKeyEventDevice();
        if (found.isEmpty()) {
            emit deviceError("未找到 gpio_keys 按键设备，请检查设备树配置");
            return;
        }
        m_eventDevice = found;
    }

    /* 打开 input event 设备 */
    int fd = ::open(m_eventDevice.toLocal8Bit().constData(), O_RDONLY);
    if (fd < 0) {
        emit deviceError(QString("打开按键设备 %1 失败: %2")
                         .arg(m_eventDevice, strerror(errno)));
        return;
    }

    m_running = true;
    qDebug() << "ButtonMonitor: 开始监控" << m_eventDevice;

    /* 获取设备名称用于调试 */
    char devName[256] = "Unknown";
    if (ioctl(fd, EVIOCGNAME(sizeof(devName)), devName) >= 0) {
        qDebug() << "ButtonMonitor: 设备名称 =" << devName;
    }

    /* 主循环：阻塞读取 input event */
    struct input_event ev;
    while (m_running) {
        /*
         * read() 会阻塞直到有事件到达，非常节省 CPU
         * input_event 结构体：16 字节时间戳 + 2 字节 type + 2 字节 code + 4 字节 value
         */
        ssize_t n = ::read(fd, &ev, sizeof(ev));
        if (n < 0) {
            if (errno == EINTR) {
                continue;  /* 被信号中断，继续 */
            }
            if (m_running) {
                emit deviceError(QString("读取按键设备失败: %1").arg(strerror(errno)));
            }
            break;
        }

        if (n != sizeof(ev)) {
            continue;  /* 读取不完整，跳过 */
        }

        /* 只关心 EV_KEY 事件（按键事件） */
        if (ev.type == EV_KEY) {
            /*
             * value 含义：
             *   0 = 按键释放
             *   1 = 按键按下
             *   2 = 按键重复（长按）
             */
            if (ev.value == 1) {
                qDebug() << "ButtonMonitor: 按键按下, code =" << ev.code;
                emit buttonPressed();
            }
        }
    }

    ::close(fd);
    qDebug() << "ButtonMonitor: 监控已停止";
}
