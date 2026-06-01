#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("VisionPass 用户管理工具");

    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
