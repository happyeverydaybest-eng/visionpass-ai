/*
 * VisionPass 消息管理程序入口（Ubuntu x86端）
 *
 * 功能：接收门禁端消息，发送消息给门禁端
 * 端口：9500
 */

#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);

	MainWindow window;
	window.show();

	return app.exec();
}
