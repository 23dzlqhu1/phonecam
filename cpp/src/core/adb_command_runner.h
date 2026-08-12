#pragma once

#include <QString>
#include <QStringList>

namespace phonecam {

struct AdbCommandResult {
    bool started = false;
    bool timedOut = false;
    int exitCode = -1;
    QString standardOutput;
    QString standardError;
    qint64 elapsedMs = 0;

    QString combinedOutput() const;
};

// ADB command execution seam. Production uses QProcess; tests inject a fake
// runner so no unit test can touch a real phone.
class IAdbCommandRunner {
public:
    virtual ~IAdbCommandRunner() = default;
    virtual AdbCommandResult run(const QString& adbPath,
                                 const QStringList& arguments,
                                 int timeoutMs) = 0;
};

class QProcessAdbCommandRunner final : public IAdbCommandRunner {
public:
    AdbCommandResult run(const QString& adbPath,
                         const QStringList& arguments,
                         int timeoutMs) override;
};

} // namespace phonecam
