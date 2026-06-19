#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QByteArray>
#include <atomic>
#include "core/pcp_protocol.h"
#include "core/video_frame.h"

namespace phonecam {

// PCP protocol receiver with TCP client and partial-read state machine.
// Connects to a phone's TcpStreamServer (via adb forward), parses PCP stream,
// emits frameReceived() signals for complete frames.
// Auto-reconnects on disconnect with a 3-second timer.
class PcpReceiver : public QObject {
    Q_OBJECT
public:
    explicit PcpReceiver(QObject* parent = nullptr);
    ~PcpReceiver() override;

    void start(quint16 port = 9999);
    void start(const QString& host, quint16 port = 9999);
    void stop();
    bool isRunning() const { return m_running; }

    // Send a reverse-control command to the connected phone (e.g. "PLI" for keyframe request).
    // The phone's TcpStreamServer reads from its input stream and dispatches commands.
    void sendCommand(const QByteArray& data);

signals:
    void frameReceived(phonecam::VideoFrame frame);
    void stateChanged(const QString& state);
    void connectionEstablished();
    void connectionLost(const QString& reason = "");
    void connectionRefused();
    void errorOccurred(const QString& error);

private slots:
    void tryConnect();
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    void processBuffer();
    void resetParser();
    void cleanupSocket();

    QTcpSocket* m_socket = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    bool m_running = false;
    QString m_host = QStringLiteral("127.0.0.1");
    quint16 m_port = 9999;

    // TCP partial read state machine
    enum class ReadState { READING_HEADER, READING_PAYLOAD };
    ReadState m_readState = ReadState::READING_HEADER;
    QByteArray m_headerBuf;
    QByteArray m_payloadBuf;
    int m_expectedHeaderSize = PCP_HEADER_SIZE_V2;
    uint32_t m_expectedPayloadLen = 0;
    PcpHeader m_currentHeader{};
};

} // namespace phonecam
