/*
 * VisionPass AI门禁系统 — 主程序入口
 *
 * 初始化流程：
 * 1. 创建QApplication（linuxfb平台，适配1024x600 LCD）
 * 2. 创建SystemController（加载所有硬件模块和AI模型）
 * 3. 创建MainWindow（UI界面）
 * 4. 连接Controller和MainWindow的信号/槽
 * 5. 显示主窗口，进入Qt事件循环
 *
 * 运行方式（开发板上）：
 *   ./visionpass -platform linuxfb
 *   或设置环境变量：export QT_QPA_PLATFORM=linuxfb
 *
 * 退出方式：
 *   - 点击界面"退出系统"按钮
 *   - 发送 SIGTERM/SIGINT 信号（如 kill 命令）
 */

#include <QApplication>
#include <QTimer>
#include <QDebug>
#include <signal.h>
#include "ui/MainWindow.h"
#include "src/controller/SystemController.h"

/*
 * 信号安全退出机制：
 *
 * 信号处理函数中不能调用Qt函数（非async-signal-safe），
 * 所以只设置一个volatile sig_atomic_t标志，
 * 然后用QTimer每200ms检查一次标志，触发quit。
 */
static volatile sig_atomic_t g_exitFlag = 0;

static void signalHandler(int signum)
{
	Q_UNUSED(signum);
	g_exitFlag = 1;
}

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);

	/* 注册信号处理（只设置标志，不调用Qt函数） */
	signal(SIGTERM, signalHandler);
	signal(SIGINT, signalHandler);

	/* 定时器：每200ms检查信号标志，安全退出 */
	QTimer exitCheckTimer;
	QObject::connect(&exitCheckTimer, &QTimer::timeout, [&]() {
		if (g_exitFlag) {
			qInfo() << "Received signal, exiting...";
			app.quit();
		}
	});
	exitCheckTimer.start(200);

	/* 创建系统控制器（后端逻辑+硬件管理） */
	SystemController controller;

	/* 初始化所有模块（加载模型、打开设备文件） */
	if (!controller.initialize()) {
		qWarning() << "System initialization failed!";
		/* 即使部分模块初始化失败也继续运行（如摄像头未连接） */
	}

	/* 创建主窗口（UI界面），传入Controller指针 */
	MainWindow mainWindow(&controller);
	mainWindow.show();

	/* 进入Qt事件循环 */
	int ret = app.exec();

	qInfo() << "VisionPass exited with code" << ret;
	return ret;
}
