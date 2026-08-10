// ═══════════════════════════════════════════════════════════════
// adb_locator.cpp — 共享 ADB 定位 / 验证实现
//
// 搜索顺序（与旧 ConnectionManager::findAdb() 的来源保持一致并覆盖全部来源）：
//   1. PHONECAM_ADB_PATH（仅存在且是文件才纳入候选）
//   2. QSettings adb/path 缓存（IniFormat + UserScope + org/app = PhoneCam）
//   3. 系统 PATH（QStandardPaths::findExecutable("adb")）
//   4. ANDROID_SDK_ROOT 下的 platform-tools/adb.exe（宽容：值本身若为 platform-tools 也接受）
//   5. ANDROID_HOME 下的 platform-tools/adb.exe（同上）
//   6. %LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe
//   7. C:/Android/Sdk、D:/Android/Sdk、<home>/Android/Sdk 下的 platform-tools/adb.exe
//   8. local.properties 的 sdk.dir（dev build 专用，保留原逻辑）
// ═══════════════════════════════════════════════════════════════

#include "core/adb_locator.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

namespace phonecam {

QString AdbLocator::s_resolvedPath;

// ── local.properties 解析（与 connection_manager.cpp 中逻辑一致）────────
// 读取 sdk.dir= 行，并还原 Gradle 的 Windows 路径转义（\: -> :，\\ -> \）
static QString readSdkDirFromLocalProperties(const QString& filePath) {
    QFile props(filePath);
    if (!props.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream in(&props);
    QString line;
    while (in.readLineInto(&line)) {
        if (line.startsWith(QStringLiteral("sdk.dir="))) {
            return line.mid(8).trimmed()
                .replace(QStringLiteral("\\:"), QStringLiteral(":"))
                .replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        }
    }
    return {};
}

// 从程序所在目录向上回溯最多 6 级，寻找 phone_native/local.properties（仅 dev build）
static QString findProjectLocalProperties() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        const QString candidate = dir.absoluteFilePath(QStringLiteral("phone_native/local.properties"));
        if (QFileInfo::exists(candidate)) return candidate;
        if (!dir.cdUp()) break;
    }
    return {};
}

QStringList AdbLocator::locateAdbCandidates() {
    QStringList candidates;

    // 加入候选并去重：路径规范化 + Windows 下大小写不敏感比较
    auto appendCandidate = [&candidates](const QString& path) {
        const QString norm = QDir::toNativeSeparators(QDir::cleanPath(path)).trimmed();
        if (norm.isEmpty()) return;
        for (const QString& c : candidates) {
            if (c.compare(norm, Qt::CaseInsensitive) == 0) return;
        }
        candidates.append(norm);
    };

    // 1. 环境变量显式覆盖（最高优先级）
    //    现有逻辑把 PHONECAM_ADB_PATH 直接视为文件路径：只有存在且是文件才纳入候选
    //    注：用 QProcessEnvironment 读取（Windows 下按 Unicode 解码，避免中文路径乱码）
    const QProcessEnvironment sysEnv = QProcessEnvironment::systemEnvironment();
    const QString envPath = sysEnv.value(QStringLiteral("PHONECAM_ADB_PATH")).trimmed();
    if (!envPath.isEmpty()) {
        const QFileInfo fi(envPath);
        if (fi.exists() && fi.isFile()) appendCandidate(fi.absoluteFilePath());
    }

    // 2. QSettings 缓存的路径（与现有代码一致：IniFormat + UserScope + org/app = PhoneCam）
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("PhoneCam"), QStringLiteral("PhoneCam"));
    const QString cached = settings.value(QStringLiteral("adb/path")).toString();
    if (!cached.isEmpty()) appendCandidate(cached);

    // 3. 系统 PATH 中查找 adb / adb.exe
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("adb"));
    if (!fromPath.isEmpty()) appendCandidate(fromPath);

    // 4/5. ANDROID_SDK_ROOT / ANDROID_HOME
    //    宽容处理：环境变量可能指向 SDK 根目录，也可能直接指向 platform-tools 目录，
    //    因此同时推导 <值>/platform-tools/adb.exe 与 <值>/adb.exe 两个候选
    for (const char* name : {"ANDROID_SDK_ROOT", "ANDROID_HOME"}) {
        const QString val = sysEnv.value(QLatin1String(name)).trimmed();
        if (val.isEmpty()) continue;
        appendCandidate(QDir(val).absoluteFilePath(QStringLiteral("platform-tools/adb.exe")));
        appendCandidate(QDir(val).absoluteFilePath(QStringLiteral("adb.exe")));
    }

    // 6. Android Studio 默认 SDK 位置：%LOCALAPPDATA%\Android\Sdk
    //    环境变量缺失时回退到 <home>/AppData/Local
    QString localAppData = sysEnv.value(QStringLiteral("LOCALAPPDATA")).trimmed();
    if (localAppData.isEmpty()) localAppData = QDir::homePath() + QStringLiteral("/AppData/Local");
    appendCandidate(QDir(localAppData).absoluteFilePath(QStringLiteral("Android/Sdk/platform-tools/adb.exe")));

    // 7. 其他常见 SDK 安装目录
    appendCandidate(QDir(QStringLiteral("C:/Android/Sdk")).absoluteFilePath(QStringLiteral("platform-tools/adb.exe")));
    appendCandidate(QDir(QStringLiteral("D:/Android/Sdk")).absoluteFilePath(QStringLiteral("platform-tools/adb.exe")));
    appendCandidate(QDir(QDir::homePath()).absoluteFilePath(QStringLiteral("Android/Sdk/platform-tools/adb.exe")));

    // 8. local.properties 推导（dev build 专用，保留原逻辑）
    const QString localProps = findProjectLocalProperties();
    if (!localProps.isEmpty()) {
        const QString sdkDir = readSdkDirFromLocalProperties(localProps);
        if (!sdkDir.isEmpty()) {
            appendCandidate(QDir(sdkDir).absoluteFilePath(QStringLiteral("platform-tools/adb.exe")));
        }
    }

    return candidates;
}

QString AdbLocator::resolveAdb() {
    // 进程内缓存：验证成功的路径直接复用，避免每次调用都重复执行 adb version
    if (!s_resolvedPath.isEmpty()) return s_resolvedPath;

    const QStringList candidates = locateAdbCandidates();
    for (const QString& cand : candidates) {
        if (!QFileInfo::exists(cand)) continue;  // 不存在的候选直接跳过
        const AdbValidation v = validateAdb(cand);
        if (v.valid) {
            s_resolvedPath = cand;  // 缓存有效路径
            return cand;
        }
        qWarning().noquote() << "[AdbLocator] 候选无效:" << cand << "-" << v.error;
    }
    return {};
}

void AdbLocator::clearCache() {
    s_resolvedPath.clear();
}

AdbValidation AdbLocator::validateAdb(const QString& adbPath) {
    AdbValidation result;
    const QString path = QDir::toNativeSeparators(adbPath.trimmed());

    if (path.isEmpty() || !QFileInfo::exists(path)) {
        result.error = QStringLiteral("文件不存在: %1").arg(path);
        return result;
    }

    // 依次尝试 `version` 与 `--version`（adb 对两种写法都支持）
    const QStringList argSet = { QStringLiteral("version"), QStringLiteral("--version") };
    for (const QString& arg : argSet) {
        QProcess proc;
        proc.start(path, { arg });

        // 1) 进程能否启动（最多等 3 秒；通常失败会立即返回）
        if (!proc.waitForStarted(3000)) {
            // 同一个 exe 换参数不会改变"无法启动"的结论，直接返回
            result.error = QStringLiteral("无法启动: %1").arg(path);
            break;
        }

        // 2) 是否在合理时间内结束（5 秒）
        if (!proc.waitForFinished(5000)) {
            proc.kill();
            proc.waitForFinished(1000);
            result.error = QStringLiteral("执行超时: %1").arg(path);
            continue;  // 换 --version 再试一次
        }

        // 3) 退出码必须为 0
        const int code = proc.exitCode();
        if (code != 0) {
            result.error = QStringLiteral("exit code=%1: %2").arg(code).arg(path);
            continue;  // 换 --version 再试一次
        }

        // 4) 输出必须能识别出 "Android Debug Bridge"
        QString out = QString::fromUtf8(proc.readAllStandardOutput());
        if (!out.contains(QStringLiteral("Android Debug Bridge"))) {
            // 少数环境把版本信息输出到 stderr，宽容合并判断
            out += QString::fromUtf8(proc.readAllStandardError());
        }
        if (!out.contains(QStringLiteral("Android Debug Bridge"))) {
            result.error = QStringLiteral("输出不合法: %1").arg(path);
            continue;  // 换 --version 再试一次
        }

        // 验证通过：versionText 取 "Android Debug Bridge version ..." 所在行，否则取第一行
        const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            const QString t = line.trimmed();
            if (t.contains(QStringLiteral("Android Debug Bridge version"))) {
                result.versionText = t;
                break;
            }
        }
        if (result.versionText.isEmpty() && !lines.isEmpty()) {
            result.versionText = lines.first().trimmed();
        }
        result.valid = true;
        return result;
    }

    return result;
}

} // namespace phonecam
