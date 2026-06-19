#include "core/device_discovery.h"
#include <QTcpSocket>
#include <QProcess>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QHostAddress>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QDateTime>
#include <QSet>
#include <QDebug>

namespace phonecam {

static const QStringList HOTSPOT_GATEWAYS = {
    "192.168.43.1",
    "192.168.42.129",
    "172.20.10.1",
    "192.168.1.1",
    "192.168.0.1",
    "10.0.0.1",
};

DeviceDiscovery::DeviceDiscovery(QObject* parent) : QObject(parent) {}

// ── Old API (thin wrapper) ──

DiscoveredDevice DeviceDiscovery::findPhone(quint16 port, double timeoutSec) {
    return findPhoneWithDiagnostics(port, timeoutSec).device;
}

// ── Diagnostic probe ──

ProbeDiagnostic DeviceDiscovery::probeHostWithDiagnostics(
    const QString& host, quint16 port, int timeoutMs, const QString& ifaceName)
{
    ProbeDiagnostic diag;
    diag.host = host;
    diag.port = port;
    diag.interfaceName = ifaceName;

    QElapsedTimer timer;
    timer.start();

    QTcpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);  // BUG-012: bypass system/Clash proxy for LAN
    socket.connectToHost(host, port);
    bool ok = socket.waitForConnected(timeoutMs);
    diag.latencyMs = ok ? static_cast<int>(timer.elapsed()) : -1;

    if (ok) {
        diag.result = ProbeResult::Success;
        diag.errorDetail = QString("Connected in %1ms").arg(diag.latencyMs);
        socket.disconnectFromHost();
    } else {
        switch (socket.error()) {
        case QAbstractSocket::ConnectionRefusedError:
            diag.result = ProbeResult::ConnectionRefused;
            diag.errorDetail = QString("Connection refused — phone app may not be streaming on %1:%2")
                .arg(host).arg(port);
            break;
        case QAbstractSocket::SocketTimeoutError:
            diag.result = ProbeResult::Timeout;
            diag.errorDetail = QString("Timeout after %1ms — firewall blocking or wrong network?")
                .arg(timeoutMs);
            break;
        case QAbstractSocket::HostNotFoundError:
            diag.result = ProbeResult::Unreachable;
            diag.errorDetail = QString("Host %1 not found").arg(host);
            break;
        case QAbstractSocket::NetworkError:
            diag.result = ProbeResult::Unreachable;
            diag.errorDetail = QString("Network unreachable to %1 — PC may not be on same network")
                .arg(host);
            break;
        default:
            diag.result = ProbeResult::Unreachable;
            diag.errorDetail = QString("Error: %1").arg(socket.errorString());
            break;
        }
    }

    return diag;
}

// ── Full diagnostic discovery ──

DiscoveryResult DeviceDiscovery::findPhoneWithDiagnostics(quint16 port, double timeoutSec) {
    DiscoveryResult result;
    int timeoutMs = static_cast<int>(timeoutSec * 1000);

    // Collect all gateways from all network interfaces
    QVector<GatewayInfo> gateways = getAllGateways();

    // Record primary gateway for backward compat
    result.gatewayIp = gateways.isEmpty() ? QString() : gateways.first().gatewayIp;

    // Record local NIC IPv4
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            QHostAddress addr = entry.ip();
            if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
                result.networkAdapters << addr.toString();
            }
        }
    }

    // Track already-probed IPs to avoid duplicates
    QSet<QString> probedIps;

    // Probe ALL gateways from all interfaces
    for (const auto& gw : gateways) {
        if (gw.gatewayIp.isEmpty() || gw.gatewayIp == "0.0.0.0") continue;
        if (probedIps.contains(gw.gatewayIp)) continue;
        probedIps.insert(gw.gatewayIp);

        auto diag = probeHostWithDiagnostics(gw.gatewayIp, port, timeoutMs, gw.interfaceName);
        result.diagnostics << diag;
        if (diag.result == ProbeResult::Success) {
            DiscoveredDevice dev;
            dev.name = gw.interfaceName + "@" + gw.gatewayIp;
            dev.ip = gw.gatewayIp;
            dev.port = port;
            dev.url = QString("%1:%2").arg(gw.gatewayIp).arg(port);
            result.devices.append(dev);
            if (!result.found) {
                result.found = true;
                result.device = dev;  // backward compat: first successful
            }
            emit deviceFound(dev);
        }
    }

    // Probe fixed HOTSPOT_GATEWAYS as fallback (skip already-probed)
    for (const QString& ip : HOTSPOT_GATEWAYS) {
        if (probedIps.contains(ip)) continue;
        probedIps.insert(ip);

        auto diag = probeHostWithDiagnostics(ip, port, timeoutMs, "hotspot-fallback");
        result.diagnostics << diag;
        if (diag.result == ProbeResult::Success) {
            DiscoveredDevice dev;
            dev.name = "Hotspot@" + ip;
            dev.ip = ip;
            dev.port = port;
            dev.url = QString("%1:%2").arg(ip).arg(port);
            result.devices.append(dev);
            if (!result.found) {
                result.found = true;
                result.device = dev;
            }
            emit deviceFound(dev);
        }
    }

    emit diagnosticsReady(result);
    return result;
}

// ── Legacy probe (kept for internal use) ──

bool DeviceDiscovery::probeHost(const QString& host, quint16 port, int timeoutMs) {
    QTcpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);  // BUG-012: bypass system/Clash proxy for LAN
    socket.connectToHost(host, port);
    bool ok = socket.waitForConnected(timeoutMs);
    if (ok) socket.disconnectFromHost();
    return ok;
}

// ── Gateway detection — enumerate ALL interfaces ──

QVector<GatewayInfo> DeviceDiscovery::getAllGateways() {
    QVector<GatewayInfo> result;

#ifdef Q_OS_WIN
    // Parse ipconfig output to extract ALL adapter sections with their gateways.
    //
    // CRITICAL: Windows ipconfig has empty lines AFTER adapter headers:
    //   Wireless LAN adapter WLAN:
    //                                          ← empty line (natural, NOT section end)
    //      Connection-specific DNS Suffix:
    //      ...
    //      Default Gateway . . . . . . . . . : fe80::1%1
    //                                          10.142.34.164
    //
    // Rule: ONLY switch adapter on new adapter header, NEVER on blank lines.

    QProcess proc;
    proc.start("ipconfig", {});
    proc.waitForFinished(5000);
    QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());
    QStringList lines = output.split('\n');

    // Regex for IPv4 address extraction
    const QRegularExpression ipv4Re("(\\d+\\.\\d+\\.\\d+\\.\\d+)");

    QString currentAdapter;
    bool inAdapterSection = false;
    bool lookingForNextLineGw = false;  // true when same-line value was IPv6

    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i].trimmed();
        QString rawLine = lines[i];

        // ── Detect adapter section header ──
        //   "Ethernet adapter Ethernet:"  "Wireless LAN adapter WLAN:"
        //   "以太网适配器 以太网:"  "无线局域网适配器 WLAN:"
        //   "以太网适配器 以太网 4:"
        bool isAdapterHeader = false;
        if (line.contains("adapter", Qt::CaseInsensitive) ||
            line.contains(QString::fromUtf8("\u9002\u914D\u5668"))) {
            int colonPos = line.lastIndexOf(':');
            // Must end with ':' and have content before it
            if (colonPos > 0 && colonPos == line.size() - 1) {
                isAdapterHeader = true;
            }
        }

        if (isAdapterHeader) {
            int colonPos = line.lastIndexOf(':');
            currentAdapter = line.left(colonPos).trimmed();
            static const QStringList prefixes = {
                QString("Ethernet adapter "),
                QString("Wireless LAN adapter "),
                QString("Local Area Connection adapter "),
                QString::fromUtf8("\u4EE5\u592A\u7F51\u9002\u914D\u5668 "),
                QString::fromUtf8("\u65E0\u7EBF\u5C40\u57DF\u7F51\u9002\u914D\u5668 ")
            };
            for (const QString& prefix : prefixes) {
                if (currentAdapter.startsWith(prefix)) {
                    currentAdapter = currentAdapter.mid(prefix.size()).trimmed();
                    break;
                }
            }
            inAdapterSection = true;
            lookingForNextLineGw = false;
            continue;
        }

        // Skip if not in any adapter section
        if (!inAdapterSection) continue;

        // ── Handle continuation line (IPv4 after IPv6 on previous line) ──
        if (lookingForNextLineGw) {
            lookingForNextLineGw = false;
            // Must be indented and non-empty
            if (!line.isEmpty() && rawLine.startsWith(' ')) {
                QRegularExpressionMatch contMatch = ipv4Re.match(line);
                if (contMatch.hasMatch()) {
                    GatewayInfo info;
                    info.interfaceName = currentAdapter;
                    info.gatewayIp = contMatch.captured(1);
                    result.append(info);
                }
            }
            // Fall through — this line might also be another field
        }

        // ── Detect Default Gateway line ──
        if (line.contains("Default Gateway") ||
            line.contains(QString::fromUtf8("\u9ED8\u8BA4\u7F51\u5173"))) {
            // Find the colon AFTER the keyword, not the last colon in the line.
            // IPv6 addresses contain multiple colons, so rfind(':') would find
            // the wrong one (inside the IPv6 address).
            int gwKeywordPos = line.indexOf("Default Gateway");
            if (gwKeywordPos < 0) {
                gwKeywordPos = line.indexOf(QString::fromUtf8("\u9ED8\u8BA4\u7F51\u5173"));
            }
            int colonPos = line.indexOf(':', gwKeywordPos);
            if (colonPos >= 0 && colonPos + 1 < line.size()) {
                QString valuePart = line.mid(colonPos + 1).trimmed();
                QRegularExpressionMatch gwMatch = ipv4Re.match(valuePart);
                if (gwMatch.hasMatch()) {
                    // Found IPv4 on same line
                    GatewayInfo info;
                    info.interfaceName = currentAdapter;
                    info.gatewayIp = gwMatch.captured(1);
                    result.append(info);
                } else {
                    // No IPv4 on this line (e.g. only IPv6 "fe80::...") — check next line
                    lookingForNextLineGw = true;
                }
            } else {
                // Colon at end or no value — check next line
                lookingForNextLineGw = true;
            }
        }
        // NOTE: Do NOT end adapter section on blank lines — only on new adapter header
    }
#else
    // Linux/macOS: parse "ip route show default"
    // May output multiple lines like:
    //   default via 192.168.1.1 dev eth0
    //   default via 10.0.0.1 dev wlan0
    QProcess proc;
    proc.start("ip", {"route", "show", "default"});
    proc.waitForFinished(3000);
    QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());

    for (const QString& line : output.split('\n')) {
        QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        for (int i = 0; i < parts.size() - 1; ++i) {
            if (parts[i] == "via") {
                GatewayInfo info;
                info.gatewayIp = parts[i + 1];
                // Try to get dev name
                for (int j = 0; j < parts.size() - 1; ++j) {
                    if (parts[j] == "dev") {
                        info.interfaceName = parts[j + 1];
                        break;
                    }
                }
                if (info.interfaceName.isEmpty()) info.interfaceName = "default";
                result.append(info);
                break;
            }
        }
    }
#endif

    // Log all discovered gateways
    for (const auto& gw : result) {
        qDebug() << "[DISC] Gateway found:" << gw.interfaceName << gw.gatewayIp;
    }
    if (result.isEmpty()) {
        qDebug() << "[DISC] No gateways found in ipconfig output";
    }

    return result;
}

} // namespace phonecam
