#include "core/adb_command_runner.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>

namespace phonecam {
namespace {

constexpr qsizetype kMaxLoggedCharacters = 4096;

QString boundedForLog(const QString& value)
{
    if (value.size() <= kMaxLoggedCharacters) return value;
    return value.left(kMaxLoggedCharacters) + QStringLiteral("...[truncated]");
}

} // namespace

QString AdbCommandResult::combinedOutput() const
{
    if (standardOutput.isEmpty()) return standardError;
    if (standardError.isEmpty()) return standardOutput;
    return standardOutput + QLatin1Char('\n') + standardError;
}

AdbCommandResult QProcessAdbCommandRunner::run(const QString& adbPath,
                                               const QStringList& arguments,
                                               int timeoutMs)
{
    AdbCommandResult result;
    QElapsedTimer timer;
    timer.start();

    const QFileInfo adbInfo(adbPath);
    if (!adbInfo.isAbsolute() || !adbInfo.exists() || !adbInfo.isFile()) {
        result.standardError = QStringLiteral("ADB 路径不是存在的绝对文件：%1").arg(adbPath);
        result.elapsedMs = timer.elapsed();
        return result;
    }

    QProcess process;
    process.start(adbInfo.absoluteFilePath(), arguments);
    result.started = process.waitForStarted(5000);
    if (!result.started) {
        result.standardError = process.errorString();
        result.elapsedMs = timer.elapsed();
        return result;
    }

    if (!process.waitForFinished(timeoutMs)) {
        result.timedOut = true;
        process.kill();
        process.waitForFinished(1000);
    }

    result.exitCode = process.exitCode();
    result.standardOutput = QString::fromUtf8(process.readAllStandardOutput());
    result.standardError = QString::fromUtf8(process.readAllStandardError());
    result.elapsedMs = timer.elapsed();

    qInfo().noquote() << "[ADB-COMMAND] program=" << adbInfo.absoluteFilePath()
                      << "arguments=" << arguments
                      << "started=" << result.started
                      << "timedOut=" << result.timedOut
                      << "exitCode=" << result.exitCode
                      << "elapsedMs=" << result.elapsedMs
                      << "stdout=" << boundedForLog(result.standardOutput)
                      << "stderr=" << boundedForLog(result.standardError);
    return result;
}

} // namespace phonecam
