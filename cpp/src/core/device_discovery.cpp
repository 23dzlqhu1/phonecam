#include "core/device_discovery.h"
#include <QTcpSocket>
#include <QProcess>
#include <QNetworkInterface>
#include <QDebug>

namespace phonecam {

// Common hotspot gateway IPs to probe
static const QStringList HOTSPOT_GATEWAYS = {
    "192.168.43.1",   // Android default
    "192.168.42.129", // Android USB tethering
    "172.20.10.1",    // iPhone
    "192.168.1.1",    // Common router
    "192.168.0.1",    // Common router
    "10.0.0.1",       // Some carriers
};

DeviceDiscovery::DeviceDiscovery(QObject* parent) : QObject(parent) {}

DiscoveredDevice DeviceDiscovery::findPhone(quint16 port, double timeoutSec) {
    int timeoutMs = static_cast<int>(timeoutSec * 1000);

    // 1. Try default gateway first
    QString gateway = getDefaultGateway();
    if (!gateway.isEmpty() && probeHost(gateway, port, timeoutMs)) {
        DiscoveredDevice dev;
        dev.name = "Hotspot@" + gateway;
        dev.ip = gateway;
        dev.port = port;
        dev.url = QString("%1:%2").arg(gateway).arg(port);
        emit deviceFound(dev);
        return dev;
    }

    // 2. Try common hotspot IPs
    for (const QString& ip : HOTSPOT_GATEWAYS) {
        if (ip == gateway) continue;  // Already tried
        if (probeHost(ip, port, timeoutMs)) {
            DiscoveredDevice dev;
            dev.name = "Hotspot@" + ip;
            dev.ip = ip;
            dev.port = port;
            dev.url = QString("%1:%2").arg(ip).arg(port);
            emit deviceFound(dev);
            return dev;
        }
    }

    return {};
}

QString DeviceDiscovery::getDefaultGateway() {
#ifdef Q_OS_WIN
    QProcess proc;
    proc.start("ipconfig", {});
    proc.waitForFinished(5000);
    QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());

    // Parse "Default Gateway" line
    for (const QString& line : output.split('\n')) {
        if (line.contains("Default Gateway") || line.contains("默认网关")) {
            // Extract IP address (last token after colon)
            QStringList parts = line.split(':');
            if (parts.size() >= 2) {
                QString ip = parts.last().trimmed();
                if (!ip.isEmpty() && ip != "0.0.0.0") {
                    return ip;
                }
            }
        }
    }
#else
    QProcess proc;
    proc.start("ip", {"route", "show", "default"});
    proc.waitForFinished(3000);
    QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());
    QStringList parts = output.split(' ');
    for (int i = 0; i < parts.size() - 1; ++i) {
        if (parts[i] == "via") {
            return parts[i + 1];
        }
    }
#endif
    return {};
}

bool DeviceDiscovery::probeHost(const QString& host, quint16 port, int timeoutMs) {
    QTcpSocket socket;
    socket.connectToHost(host, port);
    bool ok = socket.waitForConnected(timeoutMs);
    if (ok) {
        socket.disconnectFromHost();
    }
    return ok;
}

} // namespace phonecam
