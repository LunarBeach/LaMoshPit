#include <QApplication>
#include "gui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow window;
    window.setWindowTitle("Lee Anne's Mosh Pit");
    window.resize(1400, 900);
    window.show();
    return app.exec();
}