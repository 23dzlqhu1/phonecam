#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QByteArray>
#include <atomic>
#include "core/pcp_protocol.h"
#include "core/video_frame.h"

namespace phonecam {

// PCP protocol receiver with TCP server and partial-read state machine.
// Listens on a port, accepts one phone connection, parses PCP stream,
// emits frameReceived() signals for complete frames.
class PcpReceiver : public QObject {
    Q_OBJECT
public:
    explicit PcpReceiver(QObject* parent = nullptr);
    ~PcpReceiver() override;

    void start(quint16 port = 9999);
    void stop();
    bool isRunning() const { return m_running; }

signals:
    void frameReceived(phonecam::VideoFrame frame);
    void stateChanged(const QString& state);
    void connectionEstablished();
    void connectionLost();
    void portInUse(quint16 port);
    void errorOccurred(const QString& error);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void onAcceptError(QAbstractSocket::SocketError error);

private:
    void processBuffer();
    void resetParser();

    QTcpServer* m_server = nullptr;
    QTcpSocket* m_socket = nullptr;
    bool m_running = false;

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
