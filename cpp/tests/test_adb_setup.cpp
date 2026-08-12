#include "core/adb_command_runner.h"
#include "core/apk_installer.h"
#include "core/apk_metadata.h"
#include "core/tls_diagnostics.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace phonecam;

namespace {

AdbCommandResult result(int exitCode, const QString& out, const QString& error = {})
{
    AdbCommandResult command;
    command.started = true;
    command.exitCode = exitCode;
    command.standardOutput = out;
    command.standardError = error;
    return command;
}

class FakeRunner final : public IAdbCommandRunner {
public:
    QList<AdbCommandResult> responses;
    QList<QStringList> calls;

    AdbCommandResult run(const QString&, const QStringList& arguments, int) override
    {
        calls.append(arguments);
        if (responses.isEmpty()) return result(99, {}, QStringLiteral("unexpected fake call"));
        return responses.takeFirst();
    }
};

QString installedDump(int userId = 0)
{
    return QStringLiteral(
        "Package [com.phonecam.nativeapp] (123):\n"
        "  versionCode=18 minSdk=24 targetSdk=34\n"
        "  versionName=0.2.9\n"
        "  User %1: ceDataInode=1 installed=true hidden=false suspended=false stopped=false\n")
        .arg(userId);
}

QString usersDump()
{
    return QStringLiteral(
        "Users:\n"
        "  UserInfo{0:Owner:13} running\n"
        "  UserInfo{10:system_clone:30} running\n"
        "  UserInfo{999:MultiApp:4000030} running\n");
}

QString conflictDump(bool residual = true)
{
    return QStringLiteral(
        "Package [com.phonecam.nativeapp] (123):\n"
        "  versionCode=17 minSdk=24 targetSdk=34\n"
        "  versionName=0.2.8\n"
        "  User 0: ceDataInode=0 installed=false hidden=false suspended=false stopped=true\n"
        "  User 10: ceDataInode=2 installed=%1 hidden=false suspended=false stopped=true\n"
        "  User 999: ceDataInode=0 installed=false hidden=false suspended=false stopped=true\n")
        .arg(residual ? QStringLiteral("true") : QStringLiteral("false"));
}

QString signatureConflict(const QString& package = QStringLiteral("com.phonecam.nativeapp"))
{
    return QStringLiteral("Failure [INSTALL_FAILED_UPDATE_INCOMPATIBLE: Existing package %1 signatures do not match newer version; ignoring!]")
        .arg(package);
}

struct Fixture {
    QTemporaryDir dir;
    QString apkPath;
    ApkMetadata metadata { true, QStringLiteral("com.phonecam.nativeapp"),
                           18, QStringLiteral("0.2.9"), {} };

    Fixture()
    {
        apkPath = dir.filePath(QStringLiteral("candidate.apk"));
        QFile file(apkPath);
        if (!file.open(QIODevice::WriteOnly)) qFatal("failed to create fake APK");
        file.write("test");
    }
};

} // namespace

class AdbSetupTests : public QObject {
    Q_OBJECT

private slots:
    void devicesAndStates();
    void discoveryMessages();
    void normalUpgradeNeverUninstalls();
    void conflictInCloneCancelNeverUninstalls();
    void confirmedCleanupUsesStrictOrder();
    void uninstallFailureStops();
    void residualInstallStops();
    void otherPackageConflictNeverUninstalls();
    void otherInstallFailuresNeverUninstall_data();
    void otherInstallFailuresNeverUninstall();
    void apkMetadataReadsCandidate();
    void tlsMissingMessageIsExplicit();
};

void AdbSetupTests::devicesAndStates()
{
    const QList<AdbDevice> none = ApkInstaller::parseDevices(
        QStringLiteral("List of devices attached\n\n"));
    QCOMPARE(none.size(), 0);

    const QList<AdbDevice> devices = ApkInstaller::parseDevices(QStringLiteral(
        "List of devices attached\n"
        "A unauthorized usb:1-1 transport_id:1\n"
        "B offline usb:1-2 transport_id:2\n"
        "C device product:x model:Pixel_8 device:x transport_id:3\n"
        "D device product:y model:Phone_Clone device:y transport_id:4\n"));
    QCOMPARE(devices.size(), 4);
    QCOMPARE(devices.at(0).state, QStringLiteral("unauthorized"));
    QCOMPARE(devices.at(1).state, QStringLiteral("offline"));
    QCOMPARE(devices.at(2).model, QStringLiteral("Pixel 8"));
    QCOMPARE(devices.at(3).state, QStringLiteral("device"));
}

void AdbSetupTests::discoveryMessages()
{
    struct Case { QString output; QString marker; int readyCount; };
    const QList<Case> cases {
        { QStringLiteral("List of devices attached\n\n"),
          QString::fromUtf8("未检测到"), 0 },
        { QStringLiteral("List of devices attached\nA unauthorized usb:1-1\n"),
          QString::fromUtf8("允许 USB 调试"), 0 },
        { QStringLiteral("List of devices attached\nA offline usb:1-1\n"),
          QStringLiteral("offline"), 0 },
        { QStringLiteral("List of devices attached\nA device model:One\nB device model:Two\n"),
          QString::fromUtf8("必须明确选择"), 2 }
    };
    for (const Case& testCase : cases) {
        FakeRunner runner;
        runner.responses = { result(0, testCase.output) };
        ApkInstaller installer(runner);
        const DeviceDiscovery discovery = installer.discoverDevices(QStringLiteral("C:/adb.exe"));
        QCOMPARE(discovery.readyDevices().size(), testCase.readyCount);
        QVERIFY2(discovery.message.contains(testCase.marker), qPrintable(discovery.message));
    }
}

void AdbSetupTests::normalUpgradeNeverUninstalls()
{
    Fixture fixture;
    FakeRunner runner;
    runner.responses = {
        result(0, QStringLiteral("Success\n")),
        result(0, QStringLiteral("0\n")),
        result(0, installedDump())
    };
    ApkInstaller installer(runner);
    const ApkInstallResult install = installer.install(
        QStringLiteral("C:/adb.exe"), QStringLiteral("SERIAL"), fixture.apkPath,
        fixture.metadata, [](const auto&, const auto&) { return false; });
    QVERIFY(install.succeeded());
    QVERIFY(!install.uninstallAttempted);
    QCOMPARE(runner.calls.size(), 3);
    QCOMPARE(runner.calls.at(0).mid(2, 2),
             QStringList({ QStringLiteral("install"), QStringLiteral("-r") }));
    for (const QStringList& call : runner.calls) QVERIFY(!call.contains(QStringLiteral("uninstall")));
}

void AdbSetupTests::conflictInCloneCancelNeverUninstalls()
{
    Fixture fixture;
    FakeRunner runner;
    runner.responses = {
        result(1, {}, signatureConflict()),
        result(0, usersDump()),
        result(0, conflictDump())
    };
    ApkInstaller installer(runner);
    QList<AndroidUserState> shownUsers;
    const ApkInstallResult install = installer.install(
        QStringLiteral("C:/adb.exe"), QStringLiteral("SERIAL"), fixture.apkPath,
        fixture.metadata, [&shownUsers](const auto& users, const auto&) {
            shownUsers = users;
            return false;
        });
    QCOMPARE(install.status, ApkInstallStatus::CleanupCancelled);
    QCOMPARE(runner.calls.size(), 3);
    const auto clone = std::find_if(shownUsers.cbegin(), shownUsers.cend(),
        [](const AndroidUserState& user) { return user.id == 10; });
    QVERIFY(clone != shownUsers.cend());
    QCOMPARE(clone->name, QStringLiteral("system_clone"));
    QVERIFY(clone->installed);
    for (const QStringList& call : runner.calls) QVERIFY(!call.contains(QStringLiteral("uninstall")));
}

void AdbSetupTests::confirmedCleanupUsesStrictOrder()
{
    Fixture fixture;
    FakeRunner runner;
    runner.responses = {
        result(1, {}, signatureConflict()),
        result(0, usersDump()),
        result(0, conflictDump()),
        result(0, QStringLiteral("Success\n")),
        result(0, usersDump()),
        result(0, QStringLiteral("Unable to find package: com.phonecam.nativeapp\n")),
        result(0, QStringLiteral("Success\n")),
        result(0, QStringLiteral("0\n")),
        result(0, installedDump())
    };
    ApkInstaller installer(runner);
    const ApkInstallResult install = installer.install(
        QStringLiteral("C:/adb.exe"), QStringLiteral("SERIAL"), fixture.apkPath,
        fixture.metadata, [](const auto&, const auto&) { return true; });
    QVERIFY(install.succeeded());
    QVERIFY(install.uninstallAttempted);
    QCOMPARE(runner.calls.size(), 9);
    QCOMPARE(runner.calls.at(0).at(2), QStringLiteral("install"));
    QCOMPARE(runner.calls.at(1).mid(2), QStringList({"shell", "pm", "list", "users"}));
    QCOMPARE(runner.calls.at(2).mid(2, 3), QStringList({"shell", "dumpsys", "package"}));
    QCOMPARE(runner.calls.at(3).mid(2),
             QStringList({"uninstall", "com.phonecam.nativeapp"}));
    QCOMPARE(runner.calls.at(4).mid(2), QStringList({"shell", "pm", "list", "users"}));
    QCOMPARE(runner.calls.at(6).at(2), QStringLiteral("install"));
    QVERIFY(!runner.calls.at(6).contains(QStringLiteral("-r")));
    QCOMPARE(runner.calls.at(8).mid(2, 3), QStringList({"shell", "dumpsys", "package"}));
    for (const QStringList& call : runner.calls) {
        QVERIFY(!call.contains(QStringLiteral("com.phonecam.phone")));
    }
}

void AdbSetupTests::uninstallFailureStops()
{
    Fixture fixture;
    FakeRunner runner;
    runner.responses = {
        result(1, {}, signatureConflict()),
        result(0, usersDump()), result(0, conflictDump()),
        result(1, {}, QStringLiteral("Failure [DELETE_FAILED_INTERNAL_ERROR]"))
    };
    ApkInstaller installer(runner);
    const ApkInstallResult install = installer.install(
        QStringLiteral("C:/adb.exe"), QStringLiteral("SERIAL"), fixture.apkPath,
        fixture.metadata, [](const auto&, const auto&) { return true; });
    QCOMPARE(install.status, ApkInstallStatus::UninstallFailed);
    QCOMPARE(runner.calls.size(), 4);
}

void AdbSetupTests::residualInstallStops()
{
    Fixture fixture;
    FakeRunner runner;
    runner.responses = {
        result(1, {}, signatureConflict()),
        result(0, usersDump()), result(0, conflictDump()),
        result(0, QStringLiteral("Success\n")),
        result(0, usersDump()), result(0, conflictDump())
    };
    ApkInstaller installer(runner);
    const ApkInstallResult install = installer.install(
        QStringLiteral("C:/adb.exe"), QStringLiteral("SERIAL"), fixture.apkPath,
        fixture.metadata, [](const auto&, const auto&) { return true; });
    QCOMPARE(install.status, ApkInstallStatus::ResidualInstall);
    QCOMPARE(runner.calls.size(), 6);
}

void AdbSetupTests::otherPackageConflictNeverUninstalls()
{
    Fixture fixture;
    FakeRunner runner;
    runner.responses = {
        result(1, {}, signatureConflict(QStringLiteral("com.example.other")))
    };
    ApkInstaller installer(runner);
    const ApkInstallResult install = installer.install(
        QStringLiteral("C:/adb.exe"), QStringLiteral("SERIAL"), fixture.apkPath,
        fixture.metadata, [](const auto&, const auto&) { return true; });
    QCOMPARE(install.status, ApkInstallStatus::InstallFailed);
    QCOMPARE(runner.calls.size(), 1);
    QVERIFY(!install.uninstallAttempted);
}

void AdbSetupTests::otherInstallFailuresNeverUninstall_data()
{
    QTest::addColumn<QString>("failure");
    QTest::addColumn<QString>("messageMarker");
    QTest::newRow("downgrade")
        << QStringLiteral("Failure [INSTALL_FAILED_VERSION_DOWNGRADE]")
        << QString::fromUtf8("版本低于");
    QTest::newRow("user-restricted")
        << QStringLiteral("Failure [INSTALL_FAILED_USER_RESTRICTED]")
        << QString::fromUtf8("限制");
    QTest::newRow("storage")
        << QStringLiteral("Failure [INSTALL_FAILED_INSUFFICIENT_STORAGE]")
        << QString::fromUtf8("存储空间不足");
    QTest::newRow("parse")
        << QStringLiteral("Failure [INSTALL_PARSE_FAILED_BAD_MANIFEST]")
        << QString::fromUtf8("无法被 Android 解析");
}

void AdbSetupTests::otherInstallFailuresNeverUninstall()
{
    QFETCH(QString, failure);
    QFETCH(QString, messageMarker);
    Fixture fixture;
    FakeRunner runner;
    runner.responses = { result(1, {}, failure) };
    ApkInstaller installer(runner);
    const ApkInstallResult install = installer.install(
        QStringLiteral("C:/adb.exe"), QStringLiteral("SERIAL"), fixture.apkPath,
        fixture.metadata, [](const auto&, const auto&) { return true; });
    QCOMPARE(install.status, ApkInstallStatus::InstallFailed);
    QVERIFY(install.message.contains(messageMarker));
    QVERIFY(!install.uninstallAttempted);
    QCOMPARE(runner.calls.size(), 1);
    QVERIFY(!runner.calls.first().contains(QStringLiteral("uninstall")));
}

void AdbSetupTests::apkMetadataReadsCandidate()
{
    const QString path = QString::fromUtf8(PHONECAM_TEST_APK_PATH);
    if (!QFile::exists(path)) QSKIP("repository candidate APK is not present");
    const ApkMetadata metadata = readApkMetadata(QFileInfo(path).absoluteFilePath());
    QVERIFY2(metadata.valid, qPrintable(metadata.error));
    QCOMPARE(metadata.packageName, QStringLiteral("com.phonecam.nativeapp"));
    QCOMPARE(metadata.versionCode, qint64(18));
    QCOMPARE(metadata.versionName, QStringLiteral("0.2.9"));
}

void AdbSetupTests::tlsMissingMessageIsExplicit()
{
    QVERIFY(!isTlsRuntimeAvailable(false, {}, {}));
    const QString message = tlsRuntimeMissingMessage();
    QVERIFY(message.contains(QStringLiteral("HTTPS/TLS")));
    QVERIFY(message.contains(QString::fromUtf8("重新安装 PhoneCam")));
    QVERIFY(message.contains(QString::fromUtf8("导入 Platform-Tools ZIP")));
    QVERIFY(!message.contains(QString::fromUtf8("未知网络错误")));
}

QTEST_MAIN(AdbSetupTests)
#include "test_adb_setup.moc"
