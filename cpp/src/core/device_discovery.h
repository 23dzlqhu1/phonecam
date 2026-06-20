#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QStringList>

namespace phonecam {

struct DiscoveredDevice {
    QString name;
    QString ip;
    quint16 port = 0;
    QString url;
};

// Probe result classification
enum class ProbeResult {
    Success,
    Timeout,
    ConnectionRefused,
    Unreachable,
    NotAttempted
};

// Gateway with interface context
struct GatewayInfo {
    QString interfaceName;  // "Ethernet", "WLAN", etc.
    QString gatewayIp;
};

// Single probe diagnostic
struct ProbeDiagnostic {
    QString host;
    quint16 port = 0;
    ProbeResult result = ProbeResult::NotAttempted;
    int latencyMs = -1;
    QString errorDetail;
    QString interfaceName;  // which interface/gateway this probe was for
};

// Full discovery result with diagnostics
struct DiscoveryResult {
    bool found = false;
    DiscoveredDevice device;                    // first successful (backward compat)
    QVector<DiscoveredDevice> devices;          // all successful probes
    QVector<ProbeDiagnostic> diagnostics;
    QString gatewayIp;                          // primary gateway (backward compat)
    QStringList networkAdapters;
};

// Hotspot mode device discovery.
class DeviceDiscovery : public QObject {
    Q_OBJECT
public:
    explicit DeviceDiscovery(QObject* parent = nullptr);

    // New diagnostic API
    DiscoveryResult findPhoneWithDiagnostics(quint16 port = 9999, double timeoutSec = 2.0);

    // Keep old API for backward compat
    DiscoveredDevice findPhone(quint16 port = 9999, double timeoutSec = 2.0);

signals:
    void deviceFound(const phonecam::DiscoveredDevice& device);
    void diagnosticsReady(const phonecam::DiscoveryResult& result);

private:
    QVector<GatewayInfo> getAllGateways();
    bool probeHost(const QString& host, quint16 port, int timeoutMs);
    ProbeDiagnostic probeHostWithDiagnostics(const QString& host, quint16 port,
                                             int timeoutMs, const QString& ifaceName = QString());
};

} // namespace phonecam
