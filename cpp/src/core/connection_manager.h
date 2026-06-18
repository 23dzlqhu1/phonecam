#pragma once
#include <QObject>
#include <QTimer>
#include <QString>
#include <memory>

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
    QString connectionType;  // "usb" or "hotspot"
    QString url;
    QString error;
};

// Manages ADB reverse port forwarding and connection state machine.
// Priority: USB (adb reverse) > Hotspot (gateway probe).
class ConnectionManager : public QObject {
    Q_OBJECT
public:
    explicit ConnectionManager(QObject* parent = nullptr);
    ~ConnectionManager() override;

    void start(quint16 port = 9999);
    void stop();
    void confirmStreamActive();

    ConnectionInfo info() const { return m_info; }

signals:
    void stateChanged(const phonecam::ConnectionInfo& info);
    void connectionReady(const QString& url);

private slots:
    void checkConnection();

private:
    bool setupAdbReverse();
    QString findAdb();

    QTimer* m_timer = nullptr;
    ConnectionInfo m_info;
    quint16 m_port = 9999;
    bool m_adbReverseOk = false;
    bool m_streamConfirmed = false;
};

} // namespace phonecam
