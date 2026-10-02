#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    if (QGuiApplication::platformName() == "linuxfb") {
        w.showFullScreen();
    } else {
        /* desktop preview: same size as the ST7789 panel */
        w.setFixedSize(172, 320);
        w.show();
    }
    return a.exec();
}
