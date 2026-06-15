#include "core/connection_manager.h"
#include <QProcess>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <QtConcurrent>

namespace phonecam {

ConnectionManager::ConnectionManager(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &ConnectionManager::checkConnection);
}

ConnectionManager::~ConnectionManager() {
    stop();
}

void ConnectionManager::start(quint16 port) {
    m_port = port;
    m_info.state = ConnectionState::Searching;
    emit stateChanged(m_info);

    // HIGH-2 fix: move ADB setup off main thread (was blocking UI for 25 seconds)
    QtConcurrent::run([this]() {
        m_adbReverseOk = setupAdbReverse();
        // After ADB setup, start periodic checks on the main thread
        QMetaObject::invokeMethod(this, [this]() {
            checkConnection();
            m_timer->start(3000);
        }, Qt::QueuedConnection);
    });
}

void ConnectionManager::stop() {
    m_timer->stop();
    m_info.state = ConnectionState::Disconnected;
    emit stateChanged(m_info);
}

void ConnectionManager::confirmStreamActive() {
    if (m_info.state == ConnectionState::WaitingForPhone) {
        m_info.state = ConnectionState::Connected;
        m_streamConfirmed = true;
        emit stateChanged(m_info);
    }
}

void ConnectionManager::checkConnection() {
    if (m_adbReverseOk) {
        if (m_info.state != ConnectionState::WaitingForPhone &&
            m_info.state != ConnectionState::Connected) {
            m_info.state = ConnectionState::WaitingForPhone;
            m_info.connectionType = "usb";
            m_info.url = QString("127.0.0.1:%1").arg(m_port);
            emit stateChanged(m_info);
            emit connectionReady(m_info.url);
        }
    }
    // TODO: Hotspot discovery fallback (Phase 3.2)
}

QString ConnectionManager::findAdb() {
    // 1. Check PATH
    QString adb = QStandardPaths::findExecutable("adb");
    if (!adb.isEmpty()) return adb;

    // 2. Check local.properties sdk.dir (project is at D:\PhoneCam\)
    QFile props("D:/PhoneCam/phone_native/local.properties");
    if (props.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&props);
        QString line;
        while (in.readLineInto(&line)) {
            if (line.startsWith("sdk.dir=")) {
                QString sdkDir = line.mid(8).trimmed().replace("\\:", ":").replace("\\\\", "\\");
                QString candidate = sdkDir + "/platform-tools/adb.exe";
                if (QFileInfo::exists(candidate)) return candidate;
            }
        }
    }

    // 3. Common paths
    QStringList paths = {
        "D:/Android/Sdk/platform-tools/adb.exe",
        "C:/Android/Sdk/platform-tools/adb.exe",
        QDir::homePath() + "/AppData/Local/Android/Sdk/platform-tools/adb.exe"
    };
    for (const QString& p : paths) {
        if (QFileInfo::exists(p)) return p;
    }

    return {};
}

bool ConnectionManager::setupAdbReverse() {
    QString adb = findAdb();
    if (adb.isEmpty()) {
        qWarning() << "[ADB] adb not found, skipping port forwarding";
        m_info.error = "adb not found";
        return false;
    }

    // Start adb server
    QProcess proc;
    proc.start(adb, {"start-server"});
    if (!proc.waitForFinished(10000)) {
        qWarning() << "[ADB] start-server timed out";
        proc.kill();
    }

    QString portStr = QString::number(m_port);
    QString tcpSpec = QString("tcp:%1").arg(portStr);

    // Remove existing reverse
    proc.start(adb, {"reverse", "--remove", tcpSpec});
    if (!proc.waitForFinished(5000)) {
        qWarning() << "[ADB] reverse --remove timed out";
        proc.kill();
    }

    // Setup reverse
    proc.start(adb, {"reverse", tcpSpec, tcpSpec});
    if (!proc.waitForFinished(10000)) {
        qWarning() << "[ADB] reverse setup timed out";
        proc.kill();
        m_info.error = "adb reverse timed out";
        return false;
    }

    if (proc.exitCode() == 0) {
        qDebug() << "[ADB] Port reverse tcp:9999 established";
        return true;
    }

    qWarning() << "[ADB] Port reverse failed:" << proc.readAllStandardError();
    m_info.error = proc.readAllStandardError();
    return false;
}

} // namespace phonecam
