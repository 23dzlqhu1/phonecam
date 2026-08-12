#pragma once

#include "core/adb_command_runner.h"
#include "core/apk_metadata.h"

#include <QList>
#include <QString>

#include <functional>

namespace phonecam {

inline constexpr const char* kPhoneCamNativePackage = "com.phonecam.nativeapp";

struct AdbDevice {
    QString serial;
    QString state;
    QString details;
    QString model;
};

struct DeviceDiscovery {
    bool commandSucceeded = false;
    QList<AdbDevice> devices;
    QString message;
    AdbCommandResult command;

    QList<AdbDevice> readyDevices() const;
};

struct AndroidUserState {
    int id = -1;
    QString name;
    bool stateKnown = false;
    bool installed = false;
    bool hidden = false;
    bool suspended = false;
    bool stopped = false;
};

enum class ApkInstallStatus {
    Success,
    InvalidApk,
    WrongPackage,
    DeviceStateError,
    InstallFailed,
    CleanupCancelled,
    InspectionFailed,
    UninstallFailed,
    ResidualInstall,
    VerificationFailed
};

struct ApkInstallResult {
    ApkInstallStatus status = ApkInstallStatus::InstallFailed;
    QString message;
    QString diagnostic;
    QList<AndroidUserState> users;
    bool uninstallAttempted = false;

    bool succeeded() const { return status == ApkInstallStatus::Success; }
};

using CleanupConfirmation = std::function<bool(const QList<AndroidUserState>& users,
                                                const QString& diagnostic)>;

class ApkInstaller {
public:
    explicit ApkInstaller(IAdbCommandRunner& runner);

    DeviceDiscovery discoverDevices(const QString& adbPath);

    ApkInstallResult install(const QString& adbPath,
                             const QString& serial,
                             const QString& apkPath,
                             const ApkMetadata& metadata,
                             const CleanupConfirmation& confirmCleanup);

    static QList<AdbDevice> parseDevices(const QString& output);
    static QList<AndroidUserState> parseUserStates(const QString& usersOutput,
                                                   const QString& dumpsysOutput);
    static bool isExactPhoneCamSignatureConflict(const QString& output);
    static QString classifyInstallFailure(const QString& output);

private:
    struct Inspection {
        bool succeeded = false;
        bool packagePresent = false;
        bool userStatesComplete = false;
        QList<AndroidUserState> users;
        QString diagnostic;
        QString error;
    };

    Inspection inspectPackage(const QString& adbPath, const QString& serial);
    ApkInstallResult verifyInstalled(const QString& adbPath,
                                     const QString& serial,
                                     const ApkMetadata& metadata);
    AdbCommandResult runForDevice(const QString& adbPath, const QString& serial,
                                  const QStringList& arguments, int timeoutMs);

    IAdbCommandRunner& m_runner;
};

} // namespace phonecam
