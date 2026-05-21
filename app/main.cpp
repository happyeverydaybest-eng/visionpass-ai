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
 */

#include <QApplication>
#include <QDebug>
#include "ui/MainWindow.h"
#include "src/controller/SystemController.h"

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);

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
	return app.exec();
}