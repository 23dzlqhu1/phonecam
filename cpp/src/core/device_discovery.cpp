#include "core/device_discovery.h"
#include <QUdpSocket>
#include <QNetworkInterface>
#include <QNetworkAddressEntry>
#include <QNetworkProxy>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>
#include <QElapsedTimer>
#include <QSet>
#include <QDebug>

namespace phonecam {

DeviceDiscovery::DeviceDiscovery(QObject* parent) : QObject(parent) {}

// ── PHONECAM_HERE 验证规则 ──
// 只有同时满足以下条件才接受一个 response (见 docs/8月9日修复.md 第四节):
//   type == "PHONECAM_HERE" && version == 1 && nonce 匹配 && deviceId/deviceName 非空
//   && tcpPort 在 1..65535 && sender 是合法 IPv4 且不是 loopback
bool DeviceDiscovery::validateResponse(const QJsonObject& obj, const QString& nonce,
                                       const QHostAddress& sender)
{
    if (obj.value("type").toString() != "PHONECAM_HERE") return false;
    if (obj.value("version").toInt(-1) != 1) return false;
    if (obj.value("nonce").toString() != nonce) return false;
    if (obj.value("deviceId").toString().isEmpty()) return false;
    if (obj.value("deviceName").toString().isEmpty()) return false;
    const int tcpPort = obj.value("tcpPort").toInt(-1);
    if (tcpPort < 1 || tcpPort > 65535) return false;
    if (sender.protocol() != QAbstractSocket::IPv4Protocol) return false;
    if (sender == QHostAddress(QHostAddress::LocalHost)) return false;
    return true;
}

DiscoveryResult DeviceDiscovery::discover(quint16 discoveryPort, int windowMs) {
    DiscoveryResult result;

    // 1. 每一轮 discovery 新生成一个随机 nonce
    const QString nonce = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // 2. 枚举所有有效 IPv4 接口 (IsUp + IsRunning + 非 loopback + 有 broadcast)
    struct IfaceEntry {
        QHostAddress localAddr;
        QHostAddress broadcastAddr;
        QString name;
    };
    QVector<IfaceEntry> ifaces;

    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        const QNetworkInterface::InterfaceFlags flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp)) continue;
        if (!(flags & QNetworkInterface::IsRunning)) continue;
        if (flags & QNetworkInterface::IsLoopBack) continue;
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (ip == QHostAddress(QHostAddress::LocalHost)) continue;
            const QHostAddress bcast = entry.broadcast();
            if (bcast.isNull() || bcast == QHostAddress(QHostAddress::LocalHost)) continue;
            IfaceEntry e{ip, bcast, iface.name()};
            ifaces.append(e);
            result.discoveryInterfaces << QString("%1 (%2)").arg(iface.name(), ip.toString());
            result.networkAdapters << ip.toString();
        }
    }

    if (ifaces.isEmpty()) {
        qDebug() << "[DISC] 没有可用 IPv4 接口, 跳过 discovery";
        result.discoveryStatus = "no-interfaces";
        emit diagnosticsReady(result);
        return result;
    }

    // 3. 构造 request: UTF-8 JSON
    QJsonObject req;
    req.insert("type", "PHONECAM_DISCOVER");
    req.insert("version", 1);
    req.insert("nonce", nonce);
    const QByteArray reqJson = QJsonDocument(req).toJson(QJsonDocument::Compact);

    // 4. 对每个接口 bind 到 local IPv4 后发送 directed broadcast
    struct SocketEntry {
        QUdpSocket* socket = nullptr;
        QString interfaceName;
    };
    QVector<SocketEntry> sockets;
    for (const IfaceEntry& e : ifaces) {
        auto* sock = new QUdpSocket;
        sock->setProxy(QNetworkProxy::NoProxy);  // BUG-012: 绕过系统/Clash 代理
        if (!sock->bind(e.localAddr, 0)) {
            qWarning() << "[DISC] bind 失败:" << e.localAddr.toString() << sock->errorString();
            delete sock;
            continue;
        }
        const qint64 n = sock->writeDatagram(reqJson, e.broadcastAddr, discoveryPort);
        if (n < 0) {
            qWarning() << "[DISC] 广播发送失败:" << e.broadcastAddr.toString()
                       << "iface:" << e.name << sock->errorString();
            delete sock;
            continue;
        }
        result.broadcastSent = true;
        qDebug() << "[DISC] 已广播 discovery 到" << e.broadcastAddr.toString()
                 << "iface:" << e.name << "local:" << e.localAddr.toString();
        sockets.append({sock, e.name});
    }

    if (sockets.isEmpty()) {
        qWarning() << "[DISC] 没有 socket 成功发送广播";
        result.discoveryStatus = "send-failed";
        emit diagnosticsReady(result);
        return result;
    }

    // 5. 统一时间窗口内收集所有响应 (不是每个接口各等 1 秒)
    QElapsedTimer timer;
    timer.start();
    QSet<QString> seenDeviceIds;  // 同一轮内按 deviceId 去重, 保留最先收到的可用地址

    while (timer.elapsed() < windowMs) {
        const int remaining = windowMs - timer.elapsed();
        if (remaining <= 0) break;

        // 先处理所有已缓冲 datagram
        for (auto& se : sockets) {
            QUdpSocket* sock = se.socket;
            while (sock->hasPendingDatagrams()) {
                QByteArray buf;
                buf.resize(static_cast<int>(sock->pendingDatagramSize()));
                QHostAddress sender;
                quint16 senderPort = 0;
                sock->readDatagram(buf.data(), buf.size(), &sender, &senderPort);

                // 随机 UDP 数据 / 无效 JSON → 忽略
                QJsonParseError perr;
                const QJsonDocument doc = QJsonDocument::fromJson(buf, &perr);
                if (perr.error != QJsonParseError::NoError || !doc.isObject()) continue;

                // 只有通过验证规则的响应才接受
                const QJsonObject obj = doc.object();
                if (!validateResponse(obj, nonce, sender)) continue;

                const QString deviceId = obj.value("deviceId").toString();
                if (seenDeviceIds.contains(deviceId)) continue;  // 同一轮去重
                seenDeviceIds.insert(deviceId);

                DiscoveredDevice dev;
                dev.deviceId = deviceId;
                dev.name = obj.value("deviceName").toString();
                dev.ip = sender.toString();  // 必须用 datagram senderAddress 作为手机地址
                dev.port = static_cast<quint16>(obj.value("tcpPort").toInt());
                dev.url = QString("%1:%2").arg(dev.ip).arg(dev.port);
                dev.discoveryVersion = obj.value("version").toInt();
                dev.pcpVersion = obj.value("pcpVersion").toInt(0);
                // 8月9日修复 A: App 版本 metadata (旧版手机可能缺失, 非强制字段)
                dev.appVersion = obj.value("appVersion").toString();
                dev.appVersionCode = obj.value("appVersionCode").toInt(0);
                dev.interfaceName = se.interfaceName;

                result.devices.append(dev);
                result.found = true;
                // 8月9日修复 A: 日志含完整版本信息 (用于诊断, 不扩大 UI 展示)
                const QString appVer = dev.appVersion.isEmpty()
                    ? QString::fromUtf8("unknown")
                    : QString("%1(%2)").arg(dev.appVersion).arg(dev.appVersionCode);
                qDebug().noquote() << "[DISC] device=" << dev.name
                                   << "android=" << appVer
                                   << "discovery=v" << dev.discoveryVersion
                                   << "pcp=v" << dev.pcpVersion
                                   << "ip=" << dev.url
                                   << "iface=" << dev.interfaceName;
                emit deviceFound(dev);
            }
        }

        // 短等待新数据到达 (总等待上限 ≈ windowMs)
        const int waitStep = qMin(10, remaining);
        for (auto& se : sockets) {
            se.socket->waitForReadyRead(waitStep);
        }
    }

    // 清理 socket
    for (auto& se : sockets) {
        se.socket->close();
        delete se.socket;
    }

    result.discoveryStatus = result.found ? "ok-found" : "ok-no-devices";
    qDebug() << "[DISC] discovery 完成: devices=" << result.devices.size()
             << "status=" << result.discoveryStatus;
    emit diagnosticsReady(result);
    return result;
}

} // namespace phonecam
