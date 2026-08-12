#include "core/apk_installer.h"

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>

namespace phonecam {
namespace {

constexpr int kShortCommandTimeoutMs = 15000;
constexpr int kInstallTimeoutMs = 180000;

bool commandSucceeded(const AdbCommandResult& result)
{
    return result.started && !result.timedOut && result.exitCode == 0;
}

bool installSucceeded(const AdbCommandResult& result)
{
    static const QRegularExpression success(
        QStringLiteral("(?:^|[\\r\\n])\\s*Success\\s*(?:$|[\\r\\n])"));
    return commandSucceeded(result) && success.match(result.combinedOutput()).hasMatch();
}

QString boundedDiagnostic(const QString& value)
{
    constexpr qsizetype maxCharacters = 8192;
    if (value.size() <= maxCharacters) return value;
    return value.left(maxCharacters) + QStringLiteral("...[truncated]");
}

QList<int> installedUserIds(const QList<AndroidUserState>& users)
{
    QList<int> ids;
    for (const AndroidUserState& user : users) {
        if (user.stateKnown && user.installed) ids.append(user.id);
    }
    return ids;
}

QString commandFailure(const QString& operation, const AdbCommandResult& result)
{
    if (!result.started) return QStringLiteral("%1：ADB 进程无法启动。%2")
        .arg(operation, result.standardError.trimmed());
    if (result.timedOut) return QStringLiteral("%1：ADB 命令超时。请重新插拔 USB 后重试。")
        .arg(operation);
    return QStringLiteral("%1：ADB exit code=%2。%3")
        .arg(operation).arg(result.exitCode).arg(result.combinedOutput().trimmed());
}

} // namespace

QList<AdbDevice> DeviceDiscovery::readyDevices() const
{
    QList<AdbDevice> ready;
    for (const AdbDevice& device : devices) {
        if (device.state == QStringLiteral("device")) ready.append(device);
    }
    return ready;
}

ApkInstaller::ApkInstaller(IAdbCommandRunner& runner)
    : m_runner(runner)
{
}

AdbCommandResult ApkInstaller::runForDevice(const QString& adbPath, const QString& serial,
                                            const QStringList& arguments, int timeoutMs)
{
    QStringList scopedArguments { QStringLiteral("-s"), serial };
    scopedArguments.append(arguments);
    return m_runner.run(adbPath, scopedArguments, timeoutMs);
}

QList<AdbDevice> ApkInstaller::parseDevices(const QString& output)
{
    QList<AdbDevice> devices;
    const QStringList lines = output.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                           Qt::SkipEmptyParts);
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QStringLiteral("List of devices attached"))
            || line.startsWith(QLatin1Char('*'))) {
            continue;
        }

        static const QRegularExpression row(
            QStringLiteral("^(\\S+)\\s+(device|unauthorized|offline)(?:\\s+(.*))?$"));
        const QRegularExpressionMatch match = row.match(line);
        if (!match.hasMatch()) continue;

        AdbDevice device;
        device.serial = match.captured(1);
        device.state = match.captured(2);
        device.details = match.captured(3).trimmed();
        static const QRegularExpression modelExpression(QStringLiteral("(?:^|\\s)model:(\\S+)"));
        const QRegularExpressionMatch model = modelExpression.match(device.details);
        if (model.hasMatch()) device.model = model.captured(1).replace(QLatin1Char('_'), QLatin1Char(' '));
        devices.append(device);
    }
    return devices;
}

DeviceDiscovery ApkInstaller::discoverDevices(const QString& adbPath)
{
    DeviceDiscovery discovery;
    discovery.command = m_runner.run(adbPath,
        { QStringLiteral("devices"), QStringLiteral("-l") }, kShortCommandTimeoutMs);
    discovery.commandSucceeded = commandSucceeded(discovery.command);
    if (!discovery.commandSucceeded) {
        discovery.message = commandFailure(QStringLiteral("读取 USB 设备列表失败"), discovery.command);
        return discovery;
    }

    discovery.devices = parseDevices(discovery.command.standardOutput);
    const QList<AdbDevice> ready = discovery.readyDevices();
    if (ready.isEmpty()) {
        const bool unauthorized = std::any_of(discovery.devices.cbegin(), discovery.devices.cend(),
            [](const AdbDevice& d) { return d.state == QStringLiteral("unauthorized"); });
        const bool offline = std::any_of(discovery.devices.cbegin(), discovery.devices.cend(),
            [](const AdbDevice& d) { return d.state == QStringLiteral("offline"); });
        if (unauthorized) {
            discovery.message = QStringLiteral("手机尚未授权：请解锁手机，并在手机上允许 USB 调试。") ;
        } else if (offline) {
            discovery.message = QStringLiteral("手机 USB 调试状态为 offline：请重新插拔数据线，或关闭后重新开启 USB 调试。") ;
        } else {
            discovery.message = QStringLiteral("未检测到可安装的手机：请连接 USB 数据线并开启 USB 调试。") ;
        }
    } else if (ready.size() > 1) {
        discovery.message = QStringLiteral("检测到多台手机，必须明确选择安装目标。") ;
    }
    return discovery;
}

QList<AndroidUserState> ApkInstaller::parseUserStates(const QString& usersOutput,
                                                      const QString& dumpsysOutput)
{
    QList<AndroidUserState> users;
    QHash<int, int> indexById;
    static const QRegularExpression userInfo(
        QStringLiteral("UserInfo\\{(\\d+):([^:}]*)[^}]*\\}"));
    QRegularExpressionMatchIterator userMatches = userInfo.globalMatch(usersOutput);
    while (userMatches.hasNext()) {
        const QRegularExpressionMatch match = userMatches.next();
        AndroidUserState user;
        user.id = match.captured(1).toInt();
        user.name = match.captured(2).trimmed();
        if (user.name.isEmpty()) user.name = QStringLiteral("User %1").arg(user.id);
        indexById.insert(user.id, users.size());
        users.append(user);
    }

    static const QRegularExpression stateLine(
        QStringLiteral("^\\s*User\\s+(\\d+):\\s*(.*)$"),
        QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator stateMatches = stateLine.globalMatch(dumpsysOutput);
    while (stateMatches.hasNext()) {
        const QRegularExpressionMatch match = stateMatches.next();
        const int id = match.captured(1).toInt();
        const QString flags = match.captured(2);
        if (!indexById.contains(id)) {
            AndroidUserState user;
            user.id = id;
            user.name = QStringLiteral("User %1").arg(id);
            indexById.insert(id, users.size());
            users.append(user);
        }
        AndroidUserState& user = users[indexById.value(id)];
        auto flag = [&flags](const QString& name, bool* found) {
            const QRegularExpression expression(
                QStringLiteral("(?:^|\\s)%1=(true|false)(?:\\s|$)")
                    .arg(QRegularExpression::escape(name)));
            const QRegularExpressionMatch flagMatch = expression.match(flags);
            *found = flagMatch.hasMatch();
            return flagMatch.hasMatch() && flagMatch.captured(1) == QStringLiteral("true");
        };
        bool installedFound = false;
        bool ignored = false;
        user.installed = flag(QStringLiteral("installed"), &installedFound);
        user.hidden = flag(QStringLiteral("hidden"), &ignored);
        user.suspended = flag(QStringLiteral("suspended"), &ignored);
        user.stopped = flag(QStringLiteral("stopped"), &ignored);
        user.stateKnown = installedFound;
    }
    return users;
}

bool ApkInstaller::isExactPhoneCamSignatureConflict(const QString& output)
{
    if (!output.contains(QStringLiteral("INSTALL_FAILED_UPDATE_INCOMPATIBLE"))) return false;
    static const QRegularExpression package(
        QStringLiteral("(?:^|[^A-Za-z0-9_.])com\\.phonecam\\.nativeapp(?:$|[^A-Za-z0-9_.])"));
    return package.match(output).hasMatch();
}

QString ApkInstaller::classifyInstallFailure(const QString& output)
{
    if (output.contains(QStringLiteral("INSTALL_FAILED_VERSION_DOWNGRADE"))) {
        return QStringLiteral("安装失败：候选 APK 版本低于手机中的版本。不会自动卸载。") ;
    }
    if (output.contains(QStringLiteral("INSTALL_FAILED_USER_RESTRICTED"))) {
        return QStringLiteral("安装失败：手机系统或管理员限制了本次安装。不会自动卸载。") ;
    }
    if (output.contains(QStringLiteral("INSTALL_FAILED_INSUFFICIENT_STORAGE"))) {
        return QStringLiteral("安装失败：手机存储空间不足。不会自动卸载。") ;
    }
    if (output.contains(QStringLiteral("unauthorized"), Qt::CaseInsensitive)) {
        return QStringLiteral("安装失败：请在手机上允许 USB 调试。不会自动卸载。") ;
    }
    if (output.contains(QStringLiteral("offline"), Qt::CaseInsensitive)) {
        return QStringLiteral("安装失败：手机已 offline，请重新插拔 USB。不会自动卸载。") ;
    }
    if (output.contains(QStringLiteral("INSTALL_PARSE_FAILED"))) {
        return QStringLiteral("安装失败：APK 无法被 Android 解析。不会自动卸载。") ;
    }
    return QStringLiteral("APK 安装失败。该错误不符合 PhoneCam 签名冲突条件，不会自动卸载任何应用。") ;
}

ApkInstaller::Inspection ApkInstaller::inspectPackage(const QString& adbPath,
                                                       const QString& serial)
{
    Inspection inspection;
    const AdbCommandResult users = runForDevice(adbPath, serial,
        { QStringLiteral("shell"), QStringLiteral("pm"), QStringLiteral("list"),
          QStringLiteral("users") }, kShortCommandTimeoutMs);
    if (!commandSucceeded(users)) {
        inspection.error = commandFailure(QStringLiteral("读取 Android 用户空间失败"), users);
        return inspection;
    }

    const AdbCommandResult package = runForDevice(adbPath, serial,
        { QStringLiteral("shell"), QStringLiteral("dumpsys"), QStringLiteral("package"),
          QString::fromLatin1(kPhoneCamNativePackage) }, kShortCommandTimeoutMs);
    if (!commandSucceeded(package)) {
        inspection.error = commandFailure(QStringLiteral("读取 PhoneCam 安装状态失败"), package);
        return inspection;
    }

    inspection.succeeded = true;
    inspection.users = parseUserStates(users.standardOutput, package.standardOutput);
    inspection.packagePresent = package.standardOutput.contains(
        QStringLiteral("Package [com.phonecam.nativeapp]"));
    inspection.userStatesComplete = !inspection.packagePresent;
    if (inspection.packagePresent) {
        inspection.userStatesComplete = !inspection.users.isEmpty();
        for (const AndroidUserState& user : inspection.users) {
            if (!user.stateKnown) {
                inspection.userStatesComplete = false;
                break;
            }
        }
    }
    inspection.diagnostic = boundedDiagnostic(
        QStringLiteral("pm list users:\n%1\n\ndumpsys package %2:\n%3")
            .arg(users.standardOutput.trimmed(), QString::fromLatin1(kPhoneCamNativePackage),
                 package.standardOutput.trimmed()));
    return inspection;
}

ApkInstallResult ApkInstaller::verifyInstalled(const QString& adbPath,
                                                const QString& serial,
                                                const ApkMetadata& metadata)
{
    const AdbCommandResult currentUser = runForDevice(adbPath, serial,
        { QStringLiteral("shell"), QStringLiteral("am"), QStringLiteral("get-current-user") },
        kShortCommandTimeoutMs);
    bool userOk = false;
    const int targetUserId = currentUser.standardOutput.trimmed().toInt(&userOk);
    if (!commandSucceeded(currentUser) || !userOk || targetUserId < 0) {
        return { ApkInstallStatus::VerificationFailed,
                 commandFailure(QStringLiteral("安装后无法确认目标 Android 用户空间"), currentUser),
                 boundedDiagnostic(currentUser.combinedOutput()), {}, false };
    }

    const AdbCommandResult package = runForDevice(adbPath, serial,
        { QStringLiteral("shell"), QStringLiteral("dumpsys"), QStringLiteral("package"),
          QString::fromLatin1(kPhoneCamNativePackage) }, kShortCommandTimeoutMs);
    if (!commandSucceeded(package)) {
        return { ApkInstallStatus::VerificationFailed,
                 commandFailure(QStringLiteral("安装后验证失败"), package),
                 boundedDiagnostic(package.combinedOutput()), {}, false };
    }

    const QString output = package.standardOutput;
    static const QRegularExpression versionCodeExpression(
        QStringLiteral("(?:^|\\s)versionCode=(\\d+)(?:\\s|$)"));
    static const QRegularExpression versionNameExpression(
        QStringLiteral("(?:^|\\s)versionName=([^\\s]+)(?:\\s|$)"));
    const QRegularExpressionMatch versionCode = versionCodeExpression.match(output);
    const QRegularExpressionMatch versionName = versionNameExpression.match(output);
    const QList<AndroidUserState> users = parseUserStates({}, output);

    bool targetInstalled = false;
    for (const AndroidUserState& user : users) {
        if (user.id == targetUserId && user.stateKnown && user.installed) {
            targetInstalled = true;
            break;
        }
    }

    if (!output.contains(QString::fromLatin1(kPhoneCamNativePackage))
        || !versionCode.hasMatch()
        || versionCode.captured(1).toLongLong() != metadata.versionCode
        || !versionName.hasMatch()
        || versionName.captured(1) != metadata.versionName
        || !targetInstalled) {
        return { ApkInstallStatus::VerificationFailed,
                 QStringLiteral("安装后验证失败：系统中的包名、版本或目标 User %1 安装状态与候选 APK 不一致。")
                     .arg(targetUserId),
                 boundedDiagnostic(output), users, false };
    }

    return { ApkInstallStatus::Success,
             QStringLiteral("PhoneCam Android 端安装并验证成功（versionCode=%1，versionName=%2，User %3）。")
                 .arg(metadata.versionCode).arg(metadata.versionName).arg(targetUserId),
             boundedDiagnostic(output), users, false };
}

ApkInstallResult ApkInstaller::install(const QString& adbPath,
                                       const QString& serial,
                                       const QString& apkPath,
                                       const ApkMetadata& metadata,
                                       const CleanupConfirmation& confirmCleanup)
{
    if (!metadata.valid) {
        return { ApkInstallStatus::InvalidApk,
                 QStringLiteral("无法验证候选 APK：%1").arg(metadata.error), {}, {}, false };
    }
    if (metadata.packageName != QString::fromLatin1(kPhoneCamNativePackage)) {
        return { ApkInstallStatus::WrongPackage,
                 QStringLiteral("拒绝安装：APK 包名是 %1，不是 %2。不会卸载任何应用。")
                     .arg(metadata.packageName, QString::fromLatin1(kPhoneCamNativePackage)),
                 {}, {}, false };
    }
    const QFileInfo apkInfo(apkPath);
    if (!apkInfo.isAbsolute() || !apkInfo.exists() || !apkInfo.isFile()) {
        return { ApkInstallStatus::InvalidApk,
                 QStringLiteral("APK 路径不是存在的绝对文件：%1").arg(apkPath), {}, {}, false };
    }

    const AdbCommandResult firstInstall = runForDevice(adbPath, serial,
        { QStringLiteral("install"), QStringLiteral("-r"), apkInfo.absoluteFilePath() },
        kInstallTimeoutMs);
    if (installSucceeded(firstInstall)) {
        return verifyInstalled(adbPath, serial, metadata);
    }

    const QString firstOutput = firstInstall.combinedOutput();
    if (!isExactPhoneCamSignatureConflict(firstOutput)) {
        return { ApkInstallStatus::InstallFailed,
                 classifyInstallFailure(firstOutput), boundedDiagnostic(firstOutput), {}, false };
    }

    const Inspection beforeCleanup = inspectPackage(adbPath, serial);
    if (!beforeCleanup.succeeded) {
        return { ApkInstallStatus::InspectionFailed, beforeCleanup.error,
                 beforeCleanup.diagnostic, beforeCleanup.users, false };
    }
    if (!beforeCleanup.packagePresent || !beforeCleanup.userStatesComplete
        || installedUserIds(beforeCleanup.users).isEmpty()) {
        return { ApkInstallStatus::InspectionFailed,
                 QStringLiteral("检测到签名冲突，但无法可靠解析旧版在各 Android 用户空间的安装状态。为保护数据，已停止且不会卸载。"),
                 beforeCleanup.diagnostic, beforeCleanup.users, false };
    }
    if (!confirmCleanup || !confirmCleanup(beforeCleanup.users, beforeCleanup.diagnostic)) {
        return { ApkInstallStatus::CleanupCancelled,
                 QStringLiteral("已取消：未执行卸载。请在手机系统设置的应用管理中，逐一进入机主、系统分身或工作资料，手动卸载 com.phonecam.nativeapp 后再重试。com.phonecam.phone 不受影响。"),
                 beforeCleanup.diagnostic, beforeCleanup.users, false };
    }

    const AdbCommandResult uninstall = runForDevice(adbPath, serial,
        { QStringLiteral("uninstall"), QString::fromLatin1(kPhoneCamNativePackage) },
        kInstallTimeoutMs);
    if (!installSucceeded(uninstall)) {
        return { ApkInstallStatus::UninstallFailed,
                 commandFailure(QStringLiteral("旧版 PhoneCam 卸载失败，已停止重新安装"), uninstall),
                 boundedDiagnostic(uninstall.combinedOutput()), beforeCleanup.users, true };
    }

    const Inspection afterCleanup = inspectPackage(adbPath, serial);
    if (!afterCleanup.succeeded) {
        return { ApkInstallStatus::InspectionFailed,
                 QStringLiteral("卸载后无法验证所有用户空间，已停止重新安装。%1")
                     .arg(afterCleanup.error),
                 afterCleanup.diagnostic, afterCleanup.users, true };
    }
    if (afterCleanup.packagePresent && !afterCleanup.userStatesComplete) {
        return { ApkInstallStatus::InspectionFailed,
                 QStringLiteral("卸载后旧包仍可被系统解析，但无法可靠确认所有用户空间状态。已停止重新安装。"),
                 afterCleanup.diagnostic, afterCleanup.users, true };
    }
    const QList<int> remainingIds = installedUserIds(afterCleanup.users);
    if (!remainingIds.isEmpty()) {
        QStringList idText;
        for (int id : remainingIds) idText.append(QString::number(id));
        return { ApkInstallStatus::ResidualInstall,
                 QStringLiteral("卸载后仍检测到 installed=true 的用户空间：User %1。已停止重新安装；请在手机系统设置中手动删除对应系统分身或工作资料中的 com.phonecam.nativeapp。")
                     .arg(idText.join(QStringLiteral(", "))),
                 afterCleanup.diagnostic, afterCleanup.users, true };
    }

    const AdbCommandResult secondInstall = runForDevice(adbPath, serial,
        { QStringLiteral("install"), apkInfo.absoluteFilePath() }, kInstallTimeoutMs);
    if (!installSucceeded(secondInstall)) {
        return { ApkInstallStatus::InstallFailed,
                 QStringLiteral("旧版已清理，但候选 APK 安装失败。%1")
                     .arg(classifyInstallFailure(secondInstall.combinedOutput())),
                 boundedDiagnostic(secondInstall.combinedOutput()), {}, true };
    }

    ApkInstallResult verified = verifyInstalled(adbPath, serial, metadata);
    verified.uninstallAttempted = true;
    return verified;
}

} // namespace phonecam
