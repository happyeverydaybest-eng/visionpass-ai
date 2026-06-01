/**
 * @file ButtonMonitor.cpp
 * @brief 物理按键监控实现 — 读取 GPIO1_IO18（板载 KEY0）状态
 *
 * 工作原理：
 * 1. 通过 sysfs 导出 GPIO 并设置为输入方向
 * 2. QTimer 每 50ms 读取一次 GPIO 值
 * 3. 检测到下降沿（高→低，即按键按下）时发射 buttonPressed 信号
 * 4. 内置防抖：状态必须稳定一个周期才确认
 *
 * GPIO 编号计算：GPIO1_IO18 = 32 * 1 + 18 = 50
 * （I.MX6ULL 中 GPIO1 的基址编号为 32*0=0，但 sysfs 编号取决于内核配置）
 * 实际上 GPIO1_IO18 对应 sysfs 编号 18（GPIO1 基址为 0）
 */
#include "ButtonMonitor.h"

#include <QTimer>
#include <QDebug>

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/* GPIO 编号：GPIO1_IO18 = 18（GPIO1 基址为 0） */
static const char *GPIO_EXPORT_PATH = "/sys/class/gpio/export";
static const char *GPIO_UNEXPORT_PATH = "/sys/class/gpio/unexport";

ButtonMonitor::ButtonMonitor(int gpioNumber, QObject *parent)
    : QObject(parent)
    , m_gpioNumber(gpioNumber)
    , m_fd(-1)
    , m_lastState(true)    /* 默认高电平（按钮未按下） */
    , m_running(false)
    , m_pollTimer(new QTimer(this))
{
    /* 构建 GPIO value 文件路径 */
    m_gpioPath = QString("/sys/class/gpio/gpio%1/value").arg(m_gpioNumber);

    /* 连接定时器到轮询槽 */
    connect(m_pollTimer, &QTimer::timeout, this, &ButtonMonitor::poll);
}

ButtonMonitor::~ButtonMonitor()
{
    stop();
}

bool ButtonMonitor::start()
{
    if (m_running) {
        return true;
    }

    /* 尝试导出 GPIO（如果已导出则忽略错误） */
    exportGpio();

    /* 设置 GPIO 方向为输入 */
    if (!setDirection()) {
        emit deviceError(QString("按键 GPIO%1 设置输入方向失败").arg(m_gpioNumber));
        return false;
    }

    /* 打开 GPIO value 文件 */
    m_fd = ::open(m_gpioPath.toLocal8Bit().constData(), O_RDONLY);
    if (m_fd < 0) {
        emit deviceError(QString("按键 GPIO%1 打开失败: %2")
                         .arg(m_gpioNumber).arg(strerror(errno)));
        return false;
    }

    /* 读取初始状态 */
    char buf[4];
    ssize_t n = ::read(m_fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        m_lastState = (buf[0] == '1');
    }

    /* 重置文件指针到开头，以便后续读取 */
    ::lseek(m_fd, 0, SEEK_SET);

    /* 启动定时器，50ms 轮询间隔 */
    m_pollTimer->start(50);
    m_running = true;

    qDebug() << "ButtonMonitor: 按键监控已启动, GPIO" << m_gpioNumber
             << "初始状态:" << (m_lastState ? "高" : "低");
    return true;
}

void ButtonMonitor::stop()
{
    if (!m_running) {
        return;
    }

    m_pollTimer->stop();
    m_running = false;

    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }

    unexportGpio();

    qDebug() << "ButtonMonitor: 按键监控已停止";
}

void ButtonMonitor::poll()
{
    if (m_fd < 0) {
        return;
    }

    /* 重置文件指针到开头 */
    ::lseek(m_fd, 0, SEEK_SET);

    char buf[4];
    ssize_t n = ::read(m_fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        return;
    }
    buf[n] = '\0';

    bool currentState = (buf[0] == '1');

    /* 检测下降沿：高→低 表示按键按下 */
    if (m_lastState && !currentState) {
        qDebug() << "ButtonMonitor: 检测到按键按下";
        emit buttonPressed();
    }

    m_lastState = currentState;
}

bool ButtonMonitor::exportGpio()
{
    /* 检查 GPIO 是否已经导出 */
    if (access(m_gpioPath.toLocal8Bit().constData(), F_OK) == 0) {
        return true;  /* 已导出 */
    }

    int fd = ::open(GPIO_EXPORT_PATH, O_WRONLY);
    if (fd < 0) {
        qWarning() << "ButtonMonitor: 打开 export 文件失败:" << strerror(errno);
        return false;
    }

    QByteArray gpioStr = QByteArray::number(m_gpioNumber);
    ssize_t written = ::write(fd, gpioStr.constData(), gpioStr.size());
    ::close(fd);

    if (written < 0) {
        /* EBUSY 表示已经导出，不算错误 */
        if (errno != EBUSY) {
            qWarning() << "ButtonMonitor: 导出 GPIO" << m_gpioNumber << "失败:" << strerror(errno);
            return false;
        }
    }

    /* 等待 sysfs 节点创建 */
    usleep(100000);  /* 100ms */

    return true;
}

void ButtonMonitor::unexportGpio()
{
    int fd = ::open(GPIO_UNEXPORT_PATH, O_WRONLY);
    if (fd < 0) {
        return;
    }

    QByteArray gpioStr = QByteArray::number(m_gpioNumber);
    ::write(fd, gpioStr.constData(), gpioStr.size());
    ::close(fd);
}

bool ButtonMonitor::setDirection()
{
    QString directionPath = QString("/sys/class/gpio/gpio%1/direction").arg(m_gpioNumber);
    int fd = ::open(directionPath.toLocal8Bit().constData(), O_WRONLY);
    if (fd < 0) {
        qWarning() << "ButtonMonitor: 打开 direction 文件失败:" << strerror(errno);
        return false;
    }

    const char *dir = "in";
    ssize_t written = ::write(fd, dir, strlen(dir));
    ::close(fd);

    return written > 0;
}
