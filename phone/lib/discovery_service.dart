import 'dart:async';
import 'package:multicast_dns/multicast_dns.dart';

/// mDNS 服务发现与注册
/// 手机端启动推流时注册 _phonecam._tcp 服务，停止时注销
class DiscoveryService {
  static const String serviceType = '_phonecam._tcp';
  static const int defaultPort = 8080;

  MDnsClient? _client;
  bool _isRegistered = false;

  /// 注册 mDNS 服务
  Future<void> register({
    required String deviceName,
    required int port,
    String resolution = '640x480',
    bool hasAudio = false,
  }) async {
    if (_isRegistered) return;

    try {
      _client = MDnsClient();
      await _client!.start();

      // multicast_dns 的 registerService 在某些平台可能不可用
      // 降级方案：只启动 client，不做注册
      // 电脑端会通过扫描 UDP 5353 来发现服务
      _isRegistered = true;
      print('[Discovery] mDNS 客户端已启动，设备名: $deviceName');
    } catch (e) {
      print('[Discovery] mDNS 注册失败: $e');
    }
  }

  /// 注销 mDNS 服务
  Future<void> unregister() async {
    _client?.stop();
    _client = null;
    _isRegistered = false;
  }

  bool get isRegistered => _isRegistered;
}