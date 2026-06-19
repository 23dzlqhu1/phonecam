#include "core/pcp_receiver.h"
#include <QDebug>
#include <QCoreApplication>
#include <QNetworkProxy>
#include <QDateTime>
#include <chrono>

namespace phonecam {

namespace {
constexpr uint32_t MAX_PCP_PAYLOAD_LEN = 16 * 1024 * 1024;
}

PcpReceiver::PcpReceiver(QObject* parent)
    : QObject(parent)
    , m_reconnectTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(false);
    m_reconnectTimer->setInterval(3000);  // retry every 3 seconds
    connect(m_reconnectTimer, &QTimer::timeout, this, &PcpReceiver::tryConnect);
}

PcpReceiver::~PcpReceiver() {
    stop();
}

void PcpReceiver::start(quint16 port) {
    if (m_running) return;

    m_host = QStringLiteral("127.0.0.1");
    m_port = port;
    m_running = true;
    emit stateChanged("connecting");

    // Attempt immediate connection, then let reconnect timer handle retries
    tryConnect();
    m_reconnectTimer->start();

    qDebug() << "[PCP] Client mode started, connecting to" << m_host << ":" << port;
}

void PcpReceiver::start(const QString& host, quint16 port) {
    if (m_running) return;

    m_host = host;
    m_port = port;
    m_running = true;
    emit stateChanged("connecting");

    tryConnect();
    m_reconnectTimer->start();

    qDebug() << "[PCP] Client mode started, connecting to" << m_host << ":" << port;
}

void PcpReceiver::stop() {
    if (!m_running) return;
    m_running = false;
    m_reconnectTimer->stop();
    cleanupSocket();
    resetParser();
    // Don't emit signals if called from destructor (connected objects may be gone)
}

void PcpReceiver::sendCommand(const QByteArray& data) {
    if (m_socket && m_socket->isOpen()) {
        m_socket->write(data);
        qDebug() << "[PCP] Sent command:" << data.trimmed();
    }
}

void PcpReceiver::tryConnect() {
    if (!m_running) return;
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) return;

    cleanupSocket();

    m_socket = new QTcpSocket(this);
    m_socket->setProxy(QNetworkProxy::NoProxy);  // BUG-012: bypass system/Clash proxy for LAN
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    connect(m_socket, &QTcpSocket::connected, this, &PcpReceiver::onConnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &PcpReceiver::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &PcpReceiver::onDisconnected);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &PcpReceiver::onSocketError);

    m_socket->connectToHost(m_host, m_port);
}

void PcpReceiver::onConnected() {
    qDebug() << "[PCP] Connected to phone at" << m_host << ":" << m_port;
    m_reconnectTimer->stop();  // connected, no need to retry
    resetParser();
    emit connectionEstablished();
    emit stateChanged("connected");
}

void PcpReceiver::onReadyRead() {
    if (!m_socket) return;
    processBuffer();
}

void PcpReceiver::onDisconnected() {
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket && socket != m_socket) return;

    qDebug() << "[PCP] Phone disconnected (socket disconnected)";
    cleanupSocket();
    resetParser();
    emit connectionLost("socket_disconnected");
    emit stateChanged("reconnecting");

    // Restart reconnect timer if still running
    if (m_running && !m_reconnectTimer->isActive()) {
        m_reconnectTimer->start();
    }
}

void PcpReceiver::onSocketError(QAbstractSocket::SocketError error) {
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket && socket != m_socket) return;

    if (error == QAbstractSocket::ConnectionRefusedError) {
        // Phone app not running yet — this is expected during startup
        qDebug() << "[PCP] Connection refused, will retry...";
        emit connectionRefused();
        cleanupSocket();
        // Reconnect timer is already running, will retry
        return;
    }

    const QString errorString = m_socket ? m_socket->errorString() : QStringLiteral("socket error");
    qWarning() << "[PCP] Socket error:" << errorString;
    emit errorOccurred(errorString);

    // For other errors, cleanup and let reconnect timer handle it
    cleanupSocket();
    resetParser();
    emit connectionLost("socket_error: " + errorString);
    emit stateChanged("reconnecting");

    if (m_running && !m_reconnectTimer->isActive()) {
        m_reconnectTimer->start();
    }
}

void PcpReceiver::cleanupSocket() {
    if (m_socket) {
        QTcpSocket* socket = m_socket;
        m_socket = nullptr;
        disconnect(socket, nullptr, this, nullptr);
        socket->abort();
        socket->deleteLater();
    }
}

void PcpReceiver::resetParser() {
    m_readState = ReadState::READING_HEADER;
    m_headerBuf.clear();
    m_payloadBuf.clear();
    m_expectedHeaderSize = PCP_HEADER_SIZE_V2;
    m_expectedPayloadLen = 0;
    m_currentHeader = {};
}

void PcpReceiver::processBuffer() {
    // State machine for handling TCP partial reads
    // READING_HEADER: accumulate until we have a full header
    // READING_PAYLOAD: accumulate until we have the full payload
    while (m_socket && m_socket->bytesAvailable() > 0) {
        switch (m_readState) {
        case ReadState::READING_HEADER: {
            // First read: need at least 5 bytes (magic + version) to know header size
            if (m_headerBuf.size() < 5) {
                int need = 5 - m_headerBuf.size();
                m_headerBuf.append(m_socket->read(need));
                if (m_headerBuf.size() < 5) return;  // Not enough data yet

                // Check magic
                uint32_t magic;
                std::memcpy(&magic, m_headerBuf.constData(), 4);
                if (magic != PCP_MAGIC) {
                    qWarning() << "[PCP] Invalid magic, resetting connection";
                    m_socket->disconnectFromHost();
                    return;
                }

                // Determine header size from version
                uint8_t version = static_cast<uint8_t>(m_headerBuf[4]);
                m_expectedHeaderSize = header_size_for_version(version);
            }

            // Read remaining header bytes
            if (m_headerBuf.size() < m_expectedHeaderSize) {
                int need = m_expectedHeaderSize - m_headerBuf.size();
                QByteArray chunk = m_socket->read(need);
                m_headerBuf.append(chunk);
                if (m_headerBuf.size() < m_expectedHeaderSize) return;
            }

            // Parse complete header
            if (!parse_pcp_header(
                    reinterpret_cast<const uint8_t*>(m_headerBuf.constData()),
                    m_headerBuf.size(), m_currentHeader)) {
                qWarning() << "[PCP] Header parse failed, resetting";
                resetParser();
                continue;
            }

            // Transition to payload reading
            m_expectedPayloadLen = m_currentHeader.payload_len;
            if (m_expectedPayloadLen > MAX_PCP_PAYLOAD_LEN) {
                qWarning() << "[PCP] Payload too large:" << m_expectedPayloadLen
                           << "max=" << MAX_PCP_PAYLOAD_LEN
                           << "- resetting connection";
                cleanupSocket();
                resetParser();
                emit connectionLost("payload_too_large: " + QString::number(m_expectedPayloadLen));
                emit stateChanged("reconnecting");
                if (m_running && !m_reconnectTimer->isActive()) {
                    m_reconnectTimer->start();
                }
                return;
            }
            if (m_expectedPayloadLen == 0) {
                // No payload — emit empty frame and reset
                VideoFrame frame;
                frame.sequence = m_currentHeader.sequence;
                frame.pts_us = m_currentHeader.pts_us;
                frame.pts_ns = m_currentHeader.pts_ns;
                frame.is_keyframe = (m_currentHeader.flags & FLAG_KEYFRAME) != 0;
                frame.rotation = decode_rotation(m_currentHeader.flags);
                frame.codec = static_cast<int>(m_currentHeader.codec);
                frame.receive_time = static_cast<double>(QDateTime::currentMSecsSinceEpoch());  // ms
                emit frameReceived(std::move(frame));
                resetParser();
                continue;
            }

            m_readState = ReadState::READING_PAYLOAD;
            m_payloadBuf.clear();
            m_payloadBuf.reserve(m_expectedPayloadLen);
            // Fall through to start reading payload
            [[fallthrough]];
        }

        case ReadState::READING_PAYLOAD: {
            int remaining = m_expectedPayloadLen - m_payloadBuf.size();
            QByteArray chunk = m_socket->read(remaining);
            m_payloadBuf.append(chunk);

            if (m_payloadBuf.size() < static_cast<int>(m_expectedPayloadLen)) {
                return;  // Need more data
            }

            // Complete frame — emit
            VideoFrame frame;
            frame.data.resize(m_payloadBuf.size());
            std::memcpy(frame.data.data(), m_payloadBuf.constData(), m_payloadBuf.size());
            frame.sequence = m_currentHeader.sequence;
            frame.pts_us = m_currentHeader.pts_us;
            frame.pts_ns = m_currentHeader.pts_ns;
            frame.is_keyframe = (m_currentHeader.flags & FLAG_KEYFRAME) != 0;
            frame.rotation = decode_rotation(m_currentHeader.flags);
            frame.codec = static_cast<int>(m_currentHeader.codec);
            frame.receive_time = static_cast<double>(QDateTime::currentMSecsSinceEpoch());  // ms
            emit frameReceived(std::move(frame));

            // Reset for next frame
            resetParser();
            break;
        }
        } // switch
    } // while
}

} // namespace phonecam
