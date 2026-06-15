#include <QApplication>
#include <QDebug>
#include "gui/main_window.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("PhoneCam");
    app.setApplicationVersion("2.0.0");
    app.setOrganizationName("PhoneCam");

    qDebug() << "PhoneCam C++ v2.0.0 starting...";

    phonecam::MainWindow window;
    window.show();

    return app.exec();
}
