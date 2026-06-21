#include "adb_setup/setup_window.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // 设置应用信息，确保 QSettings 使用统一的组织名和应用名
    app.setOrganizationName("PhoneCam");
    app.setApplicationName("PhoneCam");

    phonecam::SetupWindow window;
    window.show();

    return app.exec();
}
