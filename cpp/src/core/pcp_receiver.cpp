#include "core/pcp_receiver.h"
#include <QDebug>
#include <QCoreApplication>
#include <QDateTime>
#include <chrono>

namespace phonecam {

PcpReceiver::PcpReceiver(QObject* parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &PcpReceiver::onNewConnection);
}

PcpReceiver::~PcpReceiver() {
    stop();
}

void PcpReceiver::start(quint16 port) {
    if (m_running) return;

    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        if (m_server->serverError() == QAbstractSocket::AddressInUseError) {
            emit portInUse(port);
            emit errorOccurred(QString("Port %1 is already in use").arg(port));
        } else {
            emit errorOccurred(QString("Failed to listen: %1").arg(m_server->errorString()));
        }
        return;
    }

    m_running = true;
    emit stateChanged("listening");
    qDebug() << "[PCP] Listening on 127.0.0.1:" << port;
}

void PcpReceiver::stop() {
    if (!m_running) return;
    m_running = false;
    if (m_socket) {
        m_socket->disconnectFromHost();
        m_socket = nullptr;
    }
    m_server->close();
    resetParser();
    // Don't emit signals if called from destructor (connected objects may be gone)
}

void PcpReceiver::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();

        // Only one connection at a time
        if (m_socket) {
            qDebug() << "[PCP] Rejecting extra connection from" << socket->peerAddress();
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }

        m_socket = socket;
        m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        connect(m_socket, &QTcpSocket::readyRead, this, &PcpReceiver::onReadyRead);
        connect(m_socket, &QTcpSocket::disconnected, this, &PcpReceiver::onDisconnected);

        resetParser();
        emit connectionEstablished();
        emit stateChanged("connected");
        qDebug() << "[PCP] Phone connected from" << m_socket->peerAddress();
    }
}

void PcpReceiver::onReadyRead() {
    if (!m_socket) return;
    processBuffer();
}

void PcpReceiver::onDisconnected() {
    qDebug() << "[PCP] Phone disconnected";
    QTcpSocket* old = m_socket;
    m_socket = nullptr;
    resetParser();
    if (old) {
        old->deleteLater();  // Clean up socket to avoid leak on reconnect
    }
    emit connectionLost();
    emit stateChanged("listening");
}

void PcpReceiver::onAcceptError(QAbstractSocket::SocketError error) {
    Q_UNUSED(error)
    emit errorOccurred(QString("Accept error: %1").arg(m_server->errorString()));
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
            if (m_expectedPayloadLen == 0) {
                // No payload — emit empty frame and reset
                VideoFrame frame;
                frame.sequence = m_currentHeader.sequence;
                frame.pts_us = m_currentHeader.pts_us;
                frame.pts_ns = m_currentHeader.pts_ns;
                frame.is_keyframe = (m_currentHeader.flags & FLAG_KEYFRAME) != 0;
                frame.rotation = decode_rotation(m_currentHeader.flags);
                frame.codec = static_cast<int>(m_currentHeader.codec);
                frame.receive_time = QDateTime::currentMSecsSinceEpoch() / 1000.0;
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
            frame.receive_time = QDateTime::currentMSecsSinceEpoch() / 1000.0;
            emit frameReceived(std::move(frame));

            // Reset for next frame
            resetParser();
            break;
        }
        } // switch
    } // while
}

} // namespace phonecam
