#include "adb_setup/setup_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // 设置应用信息，确保 QSettings 使用统一的组织名和应用名
    app.setOrganizationName("PhoneCam");
    app.setApplicationName("PhoneCam");

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("PhoneCam USB 与 Android 安装设置"));
    parser.addHelpOption();
    const QCommandLineOption installApkOption(
        QStringLiteral("install-apk"),
        QStringLiteral("安装或修复指定的 PhoneCam Android APK。"),
        QStringLiteral("absolute-apk-path"));
    parser.addOption(installApkOption);
    parser.process(app);

    QString apkPath = parser.value(installApkOption).trimmed();
    if (!apkPath.isEmpty()) apkPath = QFileInfo(apkPath).absoluteFilePath();

    phonecam::SetupWindow window(apkPath, parser.isSet(installApkOption));
    window.show();

    return app.exec();
}
