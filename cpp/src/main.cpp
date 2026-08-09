#include <QApplication>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QMutex>
#include <QMutexLocker>
#include "gui/main_window.h"
#include "core/video_frame.h"
#include "core/nv12_frame.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// ── File logger (qInstallMessageHandler) ──
// Writes all qDebug/qWarning/qCritical to logs/phonecam-pc-YYYYMMDD-HHMMSS.log
// Also forwards to OutputDebugStringA on Windows.
static QFile s_logFile;
static QMutex s_logMutex;
static bool s_logInitialized = false;

static void phonecamLogHandler(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    QMutexLocker lock(&s_logMutex);

    // Lazy-init log file on first message
    if (!s_logInitialized) {
        QDir().mkpath(QStringLiteral("logs"));
        QString filename = QStringLiteral("logs/phonecam-pc-%1.log")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
        s_logFile.setFileName(filename);
        if (s_logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            // Write UTF-8 BOM for Notepad compatibility
            s_logFile.write("\xEF\xBB\xBF");
        }
        s_logInitialized = true;
    }

    // Timestamp
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz"));

    // Level
    const char* levelStr = "DEBUG";
    switch (type) {
    case QtWarningMsg:  levelStr = "WARN "; break;
    case QtCriticalMsg: levelStr = "ERROR"; break;
    case QtFatalMsg:    levelStr = "FATAL"; break;
    case QtInfoMsg:     levelStr = "INFO "; break;
    default:            break;
    }

    const QByteArray line = QStringLiteral("[%1] [%2] %3\n")
        .arg(ts, QString::fromLatin1(levelStr), msg)
        .toUtf8();

    // Write to file (flush every line for crash-safety)
    if (s_logFile.isOpen()) {
        s_logFile.write(line);
        s_logFile.flush();
    }

    // Forward to debugger (preserve existing OutputDebugString behavior on Windows)
#ifdef Q_OS_WIN
    OutputDebugStringA(line.constData());
#endif
}

int main(int argc, char *argv[]) {
    // Install file logger BEFORE any qDebug calls
    qInstallMessageHandler(phonecamLogHandler);

    QApplication app(argc, argv);
    app.setApplicationName("PhoneCam");
    app.setApplicationVersion("2.0.2");
    app.setOrganizationName("PhoneCam");
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/phonecam.png")));

    // CRITICAL: register custom types for cross-thread queued connections.
    // Without this, PcpReceiver::frameReceived (main thread) → DecodeWorker::decodeFrame
    // (decode thread) silently fails — frames are received but never decoded.
    qRegisterMetaType<phonecam::VideoFrame>("phonecam::VideoFrame");
    qRegisterMetaType<phonecam::Nv12Frame>("phonecam::Nv12Frame");
    qRegisterMetaType<phonecam::DeviceCandidate>("phonecam::DeviceCandidate");
    qRegisterMetaType<QVector<phonecam::DeviceCandidate>>("QVector<phonecam::DeviceCandidate>");
    qRegisterMetaType<phonecam::ConnectionDiagnostics>("phonecam::ConnectionDiagnostics");

    // Parse --sw-decode flag for diagnostic use
    bool forceSwDecode = false;
    // Parse --legacy-qimage-compose flag (forces old QImage compose backend for A/B testing)
    bool useLegacyCompose = false;
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--sw-decode") {
            forceSwDecode = true;
        }
        if (QString(argv[i]) == "--legacy-qimage-compose") {
            useLegacyCompose = true;
        }
    }

    // Parse --dump-h264 <path> flag
    QString h264DumpPath;
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--dump-h264" && i + 1 < argc) {
            h264DumpPath = argv[++i];
            break;
        }
    }

    // Parse --dump-canonical-nv12 <path> flag
    QString canonicalDumpPath;
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--dump-canonical-nv12" && i + 1 < argc) {
            canonicalDumpPath = argv[++i];
            break;
        }
    }

    qDebug() << "PhoneCam C++ v2.0.2 starting...";
    if (useLegacyCompose) {
        qDebug() << "[MAIN] --legacy-qimage-compose: using QImage compose backend";
    } else {
        qDebug() << "[MAIN] fast YUV compose backend (sws_scale)";
    }

    phonecam::MainWindow window;
    if (forceSwDecode) {
        window.setForceSoftwareDecode(true);
    }
    if (useLegacyCompose) {
        window.setUseLegacyCompose(true);
    }
    if (!canonicalDumpPath.isEmpty()) {
        window.enableCanonicalDump(canonicalDumpPath);
    }
    if (!h264DumpPath.isEmpty()) {
        window.enableH264Dump(h264DumpPath);
    }
    window.show();

    return app.exec();
}
