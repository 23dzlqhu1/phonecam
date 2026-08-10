#include "core/connection_manager.h"
#include <QProcess>
#include <QDebug>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrent>
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
    // 8月9日修复 A: 协议不兼容的设备不发起 TCP, 只展示版本不兼容 (A13)
    DeviceCandidate* cand = findCandidate(deviceId);
    if (cand && !cand->compatible) {
        m_manualSelection = true;
        m_activeDeviceId.clear();
        m_info.state = ConnectionState::Disconnected;
        // 完整版本不兼容提示: 手机端版本/协议 + 电脑端版本/支持协议
        const QString phoneApp = cand->appVersion.isEmpty()
            ? QString::fromUtf8("未知")
            : QString("PhoneCam Android %1").arg(cand->appVersion);
        const QString phonePcp = cand->pcpVersion > 0
            ? QString("PCP v%1").arg(cand->pcpVersion)
            : QString::fromUtf8("未知");
        const QString pcVersion = QCoreApplication::applicationVersion();
        m_info.error = QString::fromUtf8(
            "版本不兼容：手机端 %1（手机协议 %2），电脑端 PhoneCam %3 仅支持 PCP v%4。"
            "请升级 PhoneCam 手机端或电脑端，确保两端协议版本兼容。")
            .arg(phoneApp, phonePcp, pcVersion)
            .arg(kSupportedPcpVersion);
        m_info.url = cand->url;
        m_info.connectionType = cand->transport;
        qDebug() << "[CONN] Incompatible device selected, not connecting:" << cand->displayName
                 << cand->compatibilityError;
        emit stateChanged(m_info);
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

        // 第7节：把 adb version 缓存快照传给 worker，避免跨线程直接访问成员；
        // worker 只在解析到的 ADB 路径与缓存不一致时补一次 version 探测
        const QString cachedAdbPath = m_adbCachedPath;
        const QString cachedAdbVersion = m_adbVersionText;

        QtConcurrent::run([this, basePort, cachedAdbPath, cachedAdbVersion]() {
            // ── Worker thread: all blocking I/O ──
            QString adb = adbPath();  // 共享 AdbLocator：进程内缓存，日常不重复执行 adb version
            QStringList adbDeviceLines;
            QString adbStatus = "unavailable";
            QString deviceState = "unavailable";  // AdbUnavailable：adb 路径为空或启动失败
            QString deviceModel;
            QString adbVersionText = cachedAdbVersion;
            QVector<AdbDeviceState> deviceStates;

            QVector<DeviceCandidate> usbCandidates;
            if (adb.isEmpty()) {
                // 找不到有效 ADB → AdbUnavailable（第30节：与"没插手机"是不同故障层）
                deviceState = "unavailable";
            } else {
                QProcess proc;
                proc.start(adb, {"devices", "-l"});
                proc.waitForFinished(5000);
                QString output = proc.readAllStandardOutput();
                adbDeviceLines = output.split('\n');

                // 只有启动 adb devices 明确失败时才报告 ADB 不可用（第7节）
                if (proc.exitCode() != 0 || output.trimmed().isEmpty()) {
                    adbStatus = "unavailable";
                    deviceState = "unavailable";
                } else {
                    adbStatus = "ready";
                    // 路径首次发现/变化时补一次 adb version 并缓存（第7节：日常不重复执行）
                    if (adb.compare(cachedAdbPath, Qt::CaseInsensitive) != 0) {
                        const AdbValidation v = AdbLocator::validateAdb(adb);
                        if (v.valid) adbVersionText = v.versionText;
                    }

                    // ── adb devices -l 四态解析（第25-29节）──
                    bool anyDevice = false;
                    bool anyUnauthorized = false;
                    bool anyOffline = false;
                    int portIndex = 0;
                    for (int i = 1; i < adbDeviceLines.size(); ++i) {
                        QString line = adbDeviceLines[i].trimmed();
                        if (line.isEmpty()) continue;
                        if (line.startsWith("List of devices")) continue;  // 防御输出顺序异常
                        QStringList parts = line.split(QRegularExpression("\\s+"));
                        if (parts.size() < 2) continue;

                        const QString serial = parts[0];
                        const QString state = parts[1];

                        AdbDeviceState ds;
                        ds.serial = serial;
                        ds.state = state;

                        if (state == "device") {
                            // DeviceReady：提取 model:（如 model:OPPO_xxx）
                            QRegularExpression re("model:(\\S+)");
                            auto match = re.match(line);
                            if (match.hasMatch()) ds.model = match.captured(1);
                            if (deviceModel.isEmpty() && !ds.model.isEmpty()) {
                                deviceModel = ds.model;
                            }

                            // 只有 DeviceReady 设备才做 adb forward 并生成 USB candidate（协议不变）
                            int localPort = basePort + (portIndex++);
                            bool fwdOk = setupAdbForwardForDevice(serial, localPort);

                            DeviceCandidate cand;
                            cand.id = "usb:" + serial;
                            cand.displayName = QString("USB - %1")
                                .arg(ds.model.isEmpty() ? serial : ds.model);
                            cand.transport = "usb";
                            cand.url = QString("127.0.0.1:%1").arg(localPort);
                            cand.adbSerial = serial;
                            cand.status = fwdOk ? "Found" : "Failed";
                            cand.lastError = fwdOk ? "" : "adb forward failed";
                            cand.lastSeen = QDateTime::currentMSecsSinceEpoch();
                            usbCandidates.append(cand);
                            anyDevice = true;
                        } else if (state == "unauthorized") {
                            anyUnauthorized = true;
                        } else if (state == "offline") {
                            anyOffline = true;
                        }
                        // 其他未知状态（sideload/no permissions 等）只保留在设备列表，不参与聚合
                        deviceStates.append(ds);
                    }

                    // 聚合四态：unauthorized > offline > device > no-devices（只要存在更严重状态就优先提示）
                    if (anyUnauthorized) deviceState = "unauthorized";
                    else if (anyOffline) deviceState = "offline";
                    else if (anyDevice) deviceState = "device";
                    else deviceState = "no-devices";
                }
            }

            // UDP PhoneCam Discovery V1: 只有通过验证的 PHONECAM_HERE 响应才成为 candidate
            DiscoveryResult discResult = m_discovery->discover(9997, 1000);
            QVector<DeviceCandidate> wifiCandidates;
            for (const auto& dev : discResult.devices) {
                DeviceCandidate cand;
                cand.id = "wifi:" + dev.deviceId;
                cand.displayName = QString("WiFi - %1 (%2)").arg(dev.name, dev.ip);
                cand.transport = "wifi";
                cand.url = dev.url;
                cand.lastSeen = QDateTime::currentMSecsSinceEpoch();
                // 8月9日修复 A: 版本 metadata + 协议兼容性判断 (只判断 pcpVersion)
                cand.appVersion = dev.appVersion;
                cand.appVersionCode = dev.appVersionCode;
                cand.discoveryVersion = dev.discoveryVersion;
                cand.pcpVersion = dev.pcpVersion;
                if (dev.pcpVersion == kSupportedPcpVersion) {
                    cand.compatible = true;
                    cand.status = "Found";
                } else if (dev.pcpVersion > 0) {
                    cand.compatible = false;
                    cand.status = "Incompatible";
                    cand.compatibilityError =
                        QString::fromUtf8("手机协议为 PCP v%1，当前电脑端仅支持 PCP v%2")
                            .arg(dev.pcpVersion).arg(kSupportedPcpVersion);
                } else {
                    cand.compatible = false;
                    cand.status = "Incompatible";
                    cand.compatibilityError =
                        QString::fromUtf8("手机未报告有效的 PCP 协议版本");
                }
                wifiCandidates.append(cand);
            }

            // ── Merge back on main thread ──
            QMetaObject::invokeMethod(this, [this, usbCandidates, wifiCandidates,
                discResult, adbStatus, adbDeviceLines, adb, deviceState,
                deviceModel, deviceStates, adbVersionText]() {
                m_adbProbeRunning = false;
                m_hotspotDiscoveryRunning = false;

                // ── Logging: discovery results ──
                qDebug() << "[CONN] === Discovery cycle ===";
                qDebug() << "[CONN] ADB status:" << adbStatus
                         << "path:" << (adb.isEmpty() ? QStringLiteral("(none)") : adb)
                         << "device state:" << deviceState
                         << "model:" << (deviceModel.isEmpty() ? QStringLiteral("(none)") : deviceModel)
                         << "raw lines:" << adbDeviceLines.size() - 1;
                qDebug() << "[CONN] USB candidates this cycle:" << usbCandidates.size();
                for (const auto& c : usbCandidates) {
                    qDebug() << "[CONN]   USB:" << c.id << c.displayName
                             << c.url << c.status;
                }
                qDebug() << "[CONN] Discovery interfaces:" << discResult.discoveryInterfaces.size()
                         << "status:" << discResult.discoveryStatus
                         << "broadcastSent:" << discResult.broadcastSent
                         << "devices found:" << discResult.devices.size();
                for (const auto& i : discResult.discoveryInterfaces) {
                    qDebug() << "[CONN]   iface:" << i;
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

                m_diagnostics.localNics = discResult.networkAdapters;
                m_diagnostics.discoveryInterfaces = discResult.discoveryInterfaces;
                m_diagnostics.discoveryStatus = discResult.discoveryStatus;
                m_diagnostics.adbStatus = adbStatus;
                m_diagnostics.adbDevices = adbDeviceLines;
                m_diagnostics.adbPath = adb;
                m_diagnostics.adbVersion = adbVersionText;
                m_diagnostics.deviceState = deviceState;
                m_diagnostics.deviceModel = deviceModel;
                m_diagnostics.adbDeviceStates = deviceStates;

                // 更新 adb version 进程内缓存（第7节：路径不变则日常不再重复探测）
                m_adbCachedPath = adb;
                m_adbVersionText = adbVersionText;

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
                    // 3. WiFi (8月9日修复 A: 只自动连接 compatible 的已验证 PhoneCam)
                    for (const auto& c : m_candidates) {
                        if (c.transport == "wifi" && c.status == "Found" && c.compatible) {
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
    QString adb = adbPath();
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

// ── ADB 定位已统一交给共享 AdbLocator（core/adb_locator.h）──
// 旧的 findAdb() / findProjectLocalProperties() / readSdkDirFromLocalProperties()
// 实现已删除：搜索顺序、QSettings 缓存、local.properties 推导等逻辑全部收敛到
// AdbLocator（与 phonecam-adb-setup 共用同一套规则）。日常检测调用 adbPath()
// 即 AdbLocator::resolveAdb()，进程内缓存已验证路径，避免重复执行 adb version（第7节）。

} // namespace phonecam
