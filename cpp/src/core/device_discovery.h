#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QStringList>
#include <QJsonObject>
#include <QHostAddress>

namespace phonecam {

// 协议版本常量 (8月9日修复 A): 避免 magic number 散落
constexpr int kSupportedDiscoveryVersion = 1;  // Discovery Protocol V1
constexpr int kSupportedPcpVersion = 2;        // PCP Protocol V2

// 通过 PhoneCam Discovery V1 验证后发现的真实 PhoneCam 设备
struct DiscoveredDevice {
    QString deviceId;          // Android ANDROID_ID (稳定设备身份)
    QString name;              // deviceName, e.g. "OPPO PLC110"
    QString ip;                // 手机地址 = UDP datagram senderAddress
    quint16 port = 0;          // 视频 TCP port (响应中的 tcpPort)
    QString url;               // "ip:port"
    int discoveryVersion = 0;  // PHONECAM_HERE.version
    int pcpVersion = 0;        // 手机报告的 pcpVersion
    // 8月9日修复 A: Android App 版本 (仅展示/日志用, 不用于协议兼容判断; 旧版手机可能缺失)
    QString appVersion;        // e.g. "0.2.9"
    int appVersionCode = 0;    // e.g. 18
    QString interfaceName;     // 收到响应的本地接口 (diagnostics 用)
};

// 完整 discovery 结果 (只包含通过验证的 PhoneCam, 不再包含 TCP 可连接的 endpoint)
struct DiscoveryResult {
    bool found = false;
    QVector<DiscoveredDevice> devices;  // 所有回复 PHONECAM_HERE 的设备
    QStringList discoveryInterfaces;    // 参与广播的有效 IPv4 接口 (diagnostics)
    bool broadcastSent = false;         // 是否成功发送了广播
    QString discoveryStatus;            // "ok-found" / "ok-no-devices" / "no-interfaces" / "send-failed"
    QStringList networkAdapters;        // 本地 IPv4 地址 (backward compat)
};

// 基于 UDP PhoneCam Discovery V1 的设备发现。
//
// 判断"发现了 PhoneCam"的唯一标准是:
//   收到并验证通过一个 PHONECAM_HERE 响应 (nonce 匹配 + 合法字段)。
// 不再使用 网关枚举 / 硬编码 IP / TCP :9999 probe 猜测设备。
class DeviceDiscovery : public QObject {
    Q_OBJECT
public:
    explicit DeviceDiscovery(QObject* parent = nullptr);

    // 执行一轮 UDP discovery:
    //   1. 生成随机 nonce
    //   2. 枚举所有有效 IPv4 接口 (IsUp + IsRunning + 非 loopback + 有 broadcast)
    //   3. 对每个接口 bind 到 local IP 后向 directed broadcast 发 request
    //   4. 在统一 windowMs 时间窗口内收集并验证 PHONECAM_HERE 响应
    // 阻塞发生在调用方线程 (应保持为 worker thread), 不阻塞 Qt GUI thread。
    DiscoveryResult discover(quint16 discoveryPort = 9997, int windowMs = 1000);

signals:
    void deviceFound(const phonecam::DiscoveredDevice& device);
    void diagnosticsReady(const phonecam::DiscoveryResult& result);

private:
    // 验证单个响应是否满足 PC 端验证规则 (见 docs/8月9日修复.md 第四节)
    static bool validateResponse(const QJsonObject& obj, const QString& nonce,
                                 const QHostAddress& sender);
};

} // namespace phonecam
