#pragma once
#include <QObject>
#include <QString>

namespace phonecam {

struct DiscoveredDevice {
    QString name;
    QString ip;
    quint16 port = 0;
    QString url;
};

// Hotspot mode device discovery.
// Probes common gateway IPs for a PhoneCam server on the given port.
class DeviceDiscovery : public QObject {
    Q_OBJECT
public:
    explicit DeviceDiscovery(QObject* parent = nullptr);

    // Probe for phone. Returns device info if found, empty if not.
    DiscoveredDevice findPhone(quint16 port = 9999, double timeoutSec = 2.0);

signals:
    void deviceFound(const phonecam::DiscoveredDevice& device);

private:
    QString getDefaultGateway();
    bool probeHost(const QString& host, quint16 port, int timeoutMs);
};

} // namespace phonecam
