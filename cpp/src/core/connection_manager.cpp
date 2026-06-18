#include "core/connection_manager.h"
#include <QProcess>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <QDateTime>
#include <QRegularExpression>
#include <QtConcurrent>

namespace phonecam {

ConnectionManager::ConnectionManager(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
    , m_discovery(new DeviceDiscovery(this))
{
    connect(m_timer, &QTimer::timeout, this, &ConnectionManager::checkConnection);
}

ConnectionManager::~ConnectionManager() {
    stop();
}

void ConnectionManager::start(quint16 port) {
    m_port = port;
    m_info.state = ConnectionState::Searching;
    m_manualSelection = false;
    m_streamConfirmed = false;
    m_activeDeviceId.clear();
    qDebug() << "[CONN] Starting connection manager on port" << port;
    emit stateChanged(m_info);

    checkConnection();
    m_timer->start(3000);
}

void ConnectionManager::stop() {
    m_timer->stop();
    m_info.state = ConnectionState::Disconnected;
    m_streamConfirmed = false;
    m_candidates.clear();
    emit stateChanged(m_info);
    emit candidatesChanged(m_candidates);
}

// ── Candidate state loop ──

void ConnectionManager::confirmStreamActive() {
    if (m_info.state == ConnectionState::WaitingForPhone) {
        m_info.state = ConnectionState::Connected;
        m_streamConfirmed = true;
        DeviceCandidate* cand = findCandidate(m_activeDeviceId);
        if (cand) {
            cand->status = "Connected";
            cand->lastError.clear();
        }
        emit stateChanged(m_info);
        if (cand) emit candidatesChanged(m_candidates);
    }
}

void ConnectionManager::markStreamLost() {
    m_streamConfirmed = false;
    DeviceCandidate* cand = findCandidate(m_activeDeviceId);
    if (cand) {
        cand->status = "Failed";
        cand->lastError = "Stream lost";
        emit candidatesChanged(m_candidates);
    }
    if (m_info.state == ConnectionState::Connected) {
        m_info.state = ConnectionState::Searching;
        emit stateChanged(m_info);
    }
}

void ConnectionManager::onConnectionFailed(const QString& error) {
    DeviceCandidate* cand = findCandidate(m_activeDeviceId);
    if (cand) {
        cand->status = "Failed";
        cand->lastError = error;
        emit candidatesChanged(m_candidates);
    }
}

// ── Device selection ──

void ConnectionManager::selectDevice(const QString& deviceId) {
    if (deviceId.isEmpty()) {
        m_manualSelection = false;
        m_activeDeviceId.clear();
        qDebug() << "[CONN] Auto-select mode";
        checkConnection();
        return;
    }
    m_manualSelection = true;
    m_activeDeviceId = deviceId;
    connectToCandidate(deviceId);
}

void ConnectionManager::addManualDevice(const QString& host, quint16 port) {
    m_manualSelection = true;
    DeviceCandidate cand;
    cand.id = QString("manual:%1:%2").arg(host).arg(port);
    cand.displayName = QString("Manual - %1:%2").arg(host).arg(port);
    cand.transport = "manual";
    cand.url = QString("%1:%2").arg(host).arg(port);
    cand.status = "Found";
    cand.lastSeen = QDateTime::currentMSecsSinceEpoch();

    DeviceCandidate* existing = findCandidate(cand.id);
    if (existing) *existing = cand;
    else m_candidates.append(cand);

    m_activeDeviceId = cand.id;
    connectToCandidate(cand.id);
    emit candidatesChanged(m_candidates);
}

void ConnectionManager::refreshDevices() {
    m_adbProbeRunning = false;
    m_hotspotDiscoveryRunning = false;
    checkConnection();
}

void ConnectionManager::connectToCandidate(const QString& id) {
    DeviceCandidate* cand = findCandidate(id);
    if (!cand) return;
    cand->status = "Connecting";
    emit candidatesChanged(m_candidates);
    m_info.url = cand->url;
    m_info.connectionType = cand->transport;
    m_info.state = ConnectionState::WaitingForPhone;
    emit stateChanged(m_info);
    emit connectionReady(cand->url);
}

// ── checkConnection: all blocking I/O in worker thread ──

void ConnectionManager::checkConnection() {
    if (m_info.state == ConnectionState::Connected && m_streamConfirmed) return;

    if (!m_adbProbeRunning && !m_hotspotDiscoveryRunning) {
        m_adbProbeRunning = true;
        m_hotspotDiscoveryRunning = true;
        // Pre-allocate ports on main thread
        int basePort = m_nextLocalPort;
        m_nextLocalPort += 16;

        QtConcurrent::run([this, basePort]() {
            // ── Worker thread: all blocking I/O ──
            QString adb = findAdb();
            QStringList adbDeviceLines;
            QString adbStatus = "not found";

            QVector<DeviceCandidate> usbCandidates;
            if (!adb.isEmpty()) {
                QProcess proc;
                proc.start(adb, {"devices", "-l"});
                proc.waitForFinished(5000);
                QString output = proc.readAllStandardOutput();
                adbDeviceLines = output.split('\n');
                adbStatus = adbDeviceLines.size() > 1 ? "ok" : "no devices";

                int portIndex = 0;
                for (int i = 1; i < adbDeviceLines.size(); ++i) {
                    QString line = adbDeviceLines[i].trimmed();
                    if (line.isEmpty()) continue;
                    QStringList parts = line.split(QRegularExpression("\\s+"));
                    if (parts.size() < 2 || parts[1] != "device") continue;

                    QString serial = parts[0];
                    QString model = serial;
                    QRegularExpression re("model:(\\S+)");
                    auto match = re.match(line);
                    if (match.hasMatch()) model = match.captured(1);

                    int localPort = basePort + (portIndex++);
                    bool fwdOk = setupAdbForwardForDevice(serial, localPort);

                    DeviceCandidate cand;
                    cand.id = "usb:" + serial;
                    cand.displayName = QString("USB - %1").arg(model);
                    cand.transport = "usb";
                    cand.url = QString("127.0.0.1:%1").arg(localPort);
                    cand.adbSerial = serial;
                    cand.status = fwdOk ? "Found" : "Failed";
                    cand.lastError = fwdOk ? "" : "adb forward failed";
                    cand.lastSeen = QDateTime::currentMSecsSinceEpoch();
                    usbCandidates.append(cand);
                }
            }

            DiscoveryResult discResult = m_discovery->findPhoneWithDiagnostics(m_port, 2.0);
            QVector<DeviceCandidate> wifiCandidates;
            if (discResult.found) {
                DeviceCandidate cand;
                cand.id = "wifi:" + discResult.device.ip;
                cand.displayName = QString("WiFi - %1").arg(discResult.device.ip);
                cand.transport = "wifi";
                cand.url = discResult.device.url;
                cand.status = "Found";
                cand.lastSeen = QDateTime::currentMSecsSinceEpoch();
                wifiCandidates.append(cand);
            }

            // ── Merge back on main thread ──
            QMetaObject::invokeMethod(this, [this, usbCandidates, wifiCandidates,
                discResult, adbStatus, adbDeviceLines]() {
                m_adbProbeRunning = false;
                m_hotspotDiscoveryRunning = false;

                for (const auto& cand : usbCandidates) {
                    DeviceCandidate* existing = findCandidate(cand.id);
                    if (!existing) m_candidates.append(cand);
                    else {
                        existing->lastSeen = cand.lastSeen;
                        if (existing->status != "Connected") {
                            existing->status = cand.status;
                            existing->lastError = cand.lastError;
                            existing->url = cand.url;
                        }
                    }
                }
                for (const auto& cand : wifiCandidates) {
                    DeviceCandidate* existing = findCandidate(cand.id);
                    if (!existing) m_candidates.append(cand);
                    else {
                        existing->lastSeen = cand.lastSeen;
                        if (existing->status != "Connected") existing->status = cand.status;
                    }
                }

                m_diagnostics.gatewayIp = discResult.gatewayIp;
                m_diagnostics.localNics = discResult.networkAdapters;
                m_diagnostics.probeResults = discResult.diagnostics;
                m_diagnostics.adbStatus = adbStatus;
                m_diagnostics.adbDevices = adbDeviceLines;

                qint64 now = QDateTime::currentMSecsSinceEpoch();
                m_candidates.erase(std::remove_if(m_candidates.begin(), m_candidates.end(),
                    [&](const DeviceCandidate& c) {
                        return c.status != "Connected" && (now - c.lastSeen) > 30000;
                    }), m_candidates.end());

                emit candidatesChanged(m_candidates);
                emit diagnosticsChanged(m_diagnostics);

                if (!m_manualSelection && m_activeDeviceId.isEmpty() && !m_candidates.isEmpty()) {
                    for (const auto& c : m_candidates) {
                        if (c.transport == "usb" && c.status == "Found") {
                            qDebug() << "[CONN] Auto-selecting USB device:" << c.displayName << c.url;
                            m_activeDeviceId = c.id; connectToCandidate(c.id); return;
                        }
                    }
                    for (const auto& c : m_candidates) {
                        if (c.transport == "wifi" && c.status == "Found") {
                            qDebug() << "[CONN] Auto-selecting WiFi device:" << c.displayName << c.url;
                            m_activeDeviceId = c.id; connectToCandidate(c.id); return;
                        }
                    }
                }
            }, Qt::QueuedConnection);
        });
    }
}

// ── ADB helpers ──

bool ConnectionManager::setupAdbForwardForDevice(const QString& serial, quint16 localPort) {
    QString adb = findAdb();
    if (adb.isEmpty()) return false;
    QString localTcp = QString("tcp:%1").arg(localPort);
    QString remoteTcp = QString("tcp:%1").arg(m_port);
    QProcess proc;
    proc.start(adb, {"-s", serial, "forward", "--remove", localTcp});
    proc.waitForFinished(3000);
    proc.start(adb, {"-s", serial, "forward", localTcp, remoteTcp});
    if (!proc.waitForFinished(10000)) { proc.kill(); return false; }
    if (proc.exitCode() != 0) { qWarning() << "[ADB] forward failed" << serial; return false; }
    qDebug() << "[ADB] Forward" << serial << localTcp << "->" << remoteTcp;
    return true;
}

DeviceCandidate* ConnectionManager::findCandidate(const QString& id) {
    for (auto& c : m_candidates) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

QString ConnectionManager::findAdb() {
    QString adb = QStandardPaths::findExecutable("adb");
    if (!adb.isEmpty()) return adb;
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
    QStringList paths = {"D:/Android/Sdk/platform-tools/adb.exe",
                         "C:/Android/Sdk/platform-tools/adb.exe",
                         QDir::homePath() + "/AppData/Local/Android/Sdk/platform-tools/adb.exe"};
    for (const QString& p : paths) if (QFileInfo::exists(p)) return p;
    return {};
}

} // namespace phonecam
