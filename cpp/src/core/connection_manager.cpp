#include "core/connection_manager.h"
#include <QProcess>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrent>
#include <QSettings>
#include <QCoreApplication>

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
        m_lastConnectedDeviceId = m_activeDeviceId;  // P2-1: remember last connected
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
    }
    // BUG-012: Clear active device so auto-select can pick WiFi/manual fallback
    qDebug() << "[CONN] markStreamLost: clearing active device" << m_activeDeviceId;
    m_activeDeviceId.clear();
    if (m_info.state == ConnectionState::Connected ||
        m_info.state == ConnectionState::WaitingForPhone) {
        m_info.state = ConnectionState::Searching;
        emit stateChanged(m_info);
    }
    emit candidatesChanged(m_candidates);
    // Trigger immediate re-probe on next checkConnection() tick
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
    // BUG-012: Force re-probe even if stream was confirmed
    m_streamConfirmed = false;
    m_adbProbeRunning = false;
    m_hotspotDiscoveryRunning = false;
    if (m_info.state == ConnectionState::Connected) {
        m_info.state = ConnectionState::Searching;
        emit stateChanged(m_info);
    }
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
            for (const auto& dev : discResult.devices) {
                DeviceCandidate cand;
                cand.id = "wifi:" + dev.ip;
                cand.displayName = QString("WiFi - %1").arg(dev.ip);
                cand.transport = "wifi";
                cand.url = dev.url;
                cand.status = "Found";
                cand.lastSeen = QDateTime::currentMSecsSinceEpoch();
                wifiCandidates.append(cand);
            }

            // ── Merge back on main thread ──
            QMetaObject::invokeMethod(this, [this, usbCandidates, wifiCandidates,
                discResult, adbStatus, adbDeviceLines]() {
                m_adbProbeRunning = false;
                m_hotspotDiscoveryRunning = false;

                // ── Logging: discovery results ──
                qDebug() << "[CONN] === Discovery cycle ===";
                qDebug() << "[CONN] ADB status:" << adbStatus
                         << "devices:" << adbDeviceLines.size() - 1;
                qDebug() << "[CONN] USB candidates this cycle:" << usbCandidates.size();
                for (const auto& c : usbCandidates) {
                    qDebug() << "[CONN]   USB:" << c.id << c.displayName
                             << c.url << c.status;
                }
                qDebug() << "[CONN] Gateways found:" << discResult.diagnostics.size()
                         << "devices found:" << discResult.devices.size();
                for (const auto& d : discResult.diagnostics) {
                    qDebug() << "[CONN]   probe:" << d.host << ":" << d.port
                             << "iface:" << d.interfaceName
                             << "result:" << static_cast<int>(d.result)
                             << d.errorDetail;
                }
                qDebug() << "[CONN] WiFi candidates this cycle:" << wifiCandidates.size();
                for (const auto& c : wifiCandidates) {
                    qDebug() << "[CONN]   WiFi:" << c.id << c.displayName
                             << c.url << c.status;
                }

                // ── Merge USB candidates ──
                QSet<QString> currentUsbIds;
                for (const auto& cand : usbCandidates) {
                    currentUsbIds.insert(cand.id);
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
                // BUG-012: Mark stale USB candidates (not in this cycle) as Failed
                for (auto& c : m_candidates) {
                    if (c.transport == "usb" && !currentUsbIds.contains(c.id)
                        && c.status != "Connected") {
                        if (c.status != "Failed" || c.lastError != "Device disconnected") {
                            qDebug() << "[CONN] Marking stale USB as Failed:" << c.id;
                        }
                        c.status = "Failed";
                        c.lastError = "Device disconnected";
                    }
                }

                // ── Merge WiFi candidates ──
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

                // Stale cleanup: remove candidates not seen for 30s
                qint64 now = QDateTime::currentMSecsSinceEpoch();
                m_candidates.erase(std::remove_if(m_candidates.begin(), m_candidates.end(),
                    [&](const DeviceCandidate& c) {
                        return c.status != "Connected" && (now - c.lastSeen) > 30000;
                    }), m_candidates.end());

                emit candidatesChanged(m_candidates);
                emit diagnosticsChanged(m_diagnostics);

                // ── Auto-select ──
                qDebug() << "[CONN] === Candidate table ===";
                for (const auto& c : m_candidates) {
                    qDebug() << "[CONN]" << c.id << "transport:" << c.transport
                             << "status:" << c.status << "url:" << c.url
                             << "lastError:" << c.lastError;
                }
                qDebug() << "[CONN] activeDeviceId:" << m_activeDeviceId
                         << "manualSelection:" << m_manualSelection;

                if (!m_manualSelection && m_activeDeviceId.isEmpty() && !m_candidates.isEmpty()) {
                    // P2-1 Loop 4: Priority: last connected > USB > WiFi > manual
                    // 1. Last connected device
                    if (!m_lastConnectedDeviceId.isEmpty()) {
                        for (const auto& c : m_candidates) {
                            if (c.id == m_lastConnectedDeviceId && c.status == "Found") {
                                qDebug() << "[CONN] Auto-selecting last connected:" << c.displayName;
                                m_activeDeviceId = c.id; connectToCandidate(c.id); return;
                            }
                        }
                    }
                    // 2. USB
                    for (const auto& c : m_candidates) {
                        if (c.transport == "usb" && c.status == "Found") {
                            qDebug() << "[CONN] Auto-selecting USB device:" << c.displayName << c.url;
                            m_activeDeviceId = c.id; connectToCandidate(c.id); return;
                        }
                    }
                    // 3. WiFi
                    for (const auto& c : m_candidates) {
                        if (c.transport == "wifi" && c.status == "Found") {
                            qDebug() << "[CONN] Auto-selecting WiFi device:" << c.displayName << c.url;
                            m_activeDeviceId = c.id; connectToCandidate(c.id); return;
                        }
                    }
                    // 4. Manual (lowest priority in auto mode)
                    for (const auto& c : m_candidates) {
                        if (c.transport == "manual" && c.status == "Found") {
                            qDebug() << "[CONN] Auto-selecting manual device:" << c.displayName;
                            m_activeDeviceId = c.id; connectToCandidate(c.id); return;
                        }
                    }
                    qDebug() << "[CONN] No auto-select candidate found (all Failed or empty)";
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

// 从 local.properties 中解析 sdk.dir（支持 Gradle 的 Windows 路径转义）
static QString readSdkDirFromLocalProperties(const QString& filePath) {
    QFile props(filePath);
    if (!props.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream in(&props);
    QString line;
    while (in.readLineInto(&line)) {
        if (line.startsWith("sdk.dir=")) {
            return line.mid(8).trimmed()
                .replace("\\:", ":")
                .replace("\\\\", "\\");
        }
    }
    return {};
}

// 从程序所在目录向上回溯，寻找 phone_native/local.properties
static QString findProjectLocalProperties() {
    QString appDir = QCoreApplication::applicationDirPath();
    QDir dir(appDir);
    for (int i = 0; i < 6; ++i) {
        QString candidate = dir.absoluteFilePath("phone_native/local.properties");
        if (QFileInfo::exists(candidate)) return candidate;
        if (!dir.cdUp()) break;
    }
    return {};
}

QString ConnectionManager::findAdb() {
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       "PhoneCam", "PhoneCam");

    // 1. 环境变量显式覆盖（最高优先级）
    QString envPath = qEnvironmentVariable("PHONECAM_ADB_PATH");
    if (!envPath.isEmpty() && QFileInfo::exists(envPath)) {
        return envPath;
    }

    // 2. 使用上次缓存的成功路径
    QString cached = settings.value("adb/path").toString();
    if (!cached.isEmpty() && QFileInfo::exists(cached)) {
        return cached;
    }

    // 3. 在系统 PATH 中查找 adb/adb.exe
    QString fromPath = QStandardPaths::findExecutable("adb");
    if (!fromPath.isEmpty()) {
        settings.setValue("adb/path", fromPath);
        return fromPath;
    }

    // 收集所有可能的 Android SDK 根目录
    QStringList sdkRoots;

    // 4. 从环境变量 ANDROID_SDK_ROOT / ANDROID_HOME 推导
    for (const char* name : {"ANDROID_SDK_ROOT", "ANDROID_HOME"}) {
        QString val = qEnvironmentVariable(name);
        if (!val.isEmpty()) sdkRoots.append(val);
    }

    // 5. 从项目 local.properties 推导
    QString localProps = findProjectLocalProperties();
    if (!localProps.isEmpty()) {
        QString sdkDir = readSdkDirFromLocalProperties(localProps);
        if (!sdkDir.isEmpty()) sdkRoots.append(sdkDir);
    }

    // 6. 常见安装路径
    sdkRoots << QDir::homePath() + "/AppData/Local/Android/Sdk"
             << "C:/Android/Sdk"
             << "D:/Android/Sdk"
             << QDir::homePath() + "/Android/Sdk";

    // 去重并尝试 platform-tools/adb.exe
    QSet<QString> seen;
    for (const QString& root : sdkRoots) {
        if (root.isEmpty() || seen.contains(root)) continue;
        seen.insert(root);
        QString candidate = QDir(root).absoluteFilePath("platform-tools/adb.exe");
        if (QFileInfo::exists(candidate)) {
            settings.setValue("adb/path", candidate);
            return candidate;
        }
    }

    return {};
}

} // namespace phonecam
