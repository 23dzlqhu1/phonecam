#pragma once
#include <QObject>
#include <QTimer>
#include <QString>
#include <QVector>
#include <QStringList>
#include <memory>
#include "core/device_discovery.h"

namespace phonecam {

enum class ConnectionState {
    Disconnected,
    Searching,
    WaitingForPhone,
    Connected,
    Reconnecting
};

struct ConnectionInfo {
    ConnectionState state = ConnectionState::Disconnected;
    QString connectionType;
    QString url;
    QString error;
};

// Device candidate model
struct DeviceCandidate {
    QString id;           // "usb:<serial>" | "wifi:<deviceId>" | "manual:<host>:<port>"
    QString displayName;  // "USB - vivo V2243A" | "WiFi - OPPO PLC110 (10.72.201.83)"
    QString transport;    // "usb", "wifi", "manual"
    QString url;          // "host:port"
    QString adbSerial;    // ADB serial for USB (empty otherwise)
    QString status;       // "Found", "Connecting", "Connected", "Failed", "Incompatible"
    QString lastError;
    qint64 lastSeen = 0;
    // 8月9日修复 A: 版本 metadata 与协议兼容性 (WiFi/UDP Discovery 来源才有完整值)
    QString appVersion;           // Android App versionName (未知时为空)
    int appVersionCode = 0;       // Android versionCode
    int discoveryVersion = 0;     // Discovery protocol version
    int pcpVersion = 0;           // PCP protocol version (0 = 未报告)
    bool compatible = true;       // 协议是否兼容
    QString compatibilityError;   // 不兼容时的用户可读原因
};

// Aggregated connection diagnostics
struct ConnectionDiagnostics {
    QStringList localNics;          // 本地有效 IPv4 地址
    QStringList discoveryInterfaces;  // 参与 UDP discovery 广播的接口 (name + ip)
    QString discoveryStatus;          // "ok-found" / "ok-no-devices" / "no-interfaces" / "send-failed"
    QString adbStatus;
    QStringList adbDevices;
};

class ConnectionManager : public QObject {
    Q_OBJECT
public:
    explicit ConnectionManager(QObject* parent = nullptr);
    ~ConnectionManager() override;

    void start(quint16 port = 9999);
    void stop();
    void confirmStreamActive();
    void markStreamLost();

    // Device selection
    void selectDevice(const QString& deviceId);     // "" = auto
    void addManualDevice(const QString& host, quint16 port);
    void refreshDevices();
    void onConnectionFailed(const QString& error);
    QVector<DeviceCandidate> candidates() const { return m_candidates; }
    QString activeDeviceId() const { return m_activeDeviceId; }  // P2-1 Loop 4

    ConnectionInfo info() const { return m_info; }

    // 返回当前使用的 ADB 可执行文件路径（为空表示未找到）
    QString adbPath() { return findAdb(); }

signals:
    void stateChanged(const phonecam::ConnectionInfo& info);
    void connectionReady(const QString& url);
    void candidatesChanged(const QVector<phonecam::DeviceCandidate>& candidates);
    void diagnosticsChanged(const phonecam::ConnectionDiagnostics& diag);

private slots:
    void checkConnection();

private:
    bool setupAdbForwardForDevice(const QString& serial, quint16 localPort);
    QString findAdb();
    DeviceCandidate* findCandidate(const QString& id);
    void connectToCandidate(const QString& id);

    QTimer* m_timer = nullptr;
    DeviceDiscovery* m_discovery = nullptr;
    ConnectionInfo m_info;
    quint16 m_port = 9999;
    int m_nextLocalPort = 19999;

    QVector<DeviceCandidate> m_candidates;
    QString m_activeDeviceId;
    bool m_manualSelection = false;
    QString m_lastConnectedDeviceId;  // P2-1 Loop 4: last successful device

    ConnectionDiagnostics m_diagnostics;

    bool m_adbProbeRunning = false;
    bool m_hotspotDiscoveryRunning = false;
    bool m_streamConfirmed = false;
};

} // namespace phonecam

Q_DECLARE_METATYPE(phonecam::DeviceCandidate)
Q_DECLARE_METATYPE(QVector<phonecam::DeviceCandidate>)
Q_DECLARE_METATYPE(phonecam::ConnectionDiagnostics)
