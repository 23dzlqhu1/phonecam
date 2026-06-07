// 🚨 废弃警告 / DEPRECATION WARNING 🚨
//
// 本文件当前实现是：WebSocket + 12 字节头 + H.264 NAL
// 这与项目新协议路线（PCP / TCP / 24 字节头）冲突。
//
// 计划：
// - MVP-1：不修改本文件，电脑端用 `tests/mock_phone/` 模拟
// - MVP-2：完全重写本文件为 TCP + PCP（24 字节头），并删除
//          shelf / web_socket_channel 依赖
//
// 当前状态：**冻结**，禁止添加新功能，只修复编译错误。
//
// 协议规范见 `docs/protocol.md`
// 进度见 `.ai/context.md`

import 'dart:async';
import 'dart:io';
import 'dart:convert';
import 'dart:typed_data';
import 'package:shelf/shelf.dart' as shelf;
import 'package:shelf/shelf_io.dart' as shelf_io;
import 'package:web_socket_channel/web_socket_channel.dart';
import 'h264_encoder.dart';

/// H.264 WebSocket 推流服务
class StreamServer {
  HttpServer? _server;
  WebSocket? _wsClient;
  bool _isRunning = false;
  int _sequence = 0;
  int _totalBytes = 0;

  /// H.264 帧回调
  void Function(H264Frame frame)? onH264Frame;

  /// 关键帧请求回调
  void Function()? onKeyframeRequest;

  void sendH264Frame(H264Frame frame) {
    if (_wsClient == null) return;

    try {
      // 二进制帧: [4B seq][4B pts][4B flags][NAL data...]
      final header = ByteData(12);
      header.setUint32(0, _sequence++);
      header.setUint32(4, frame.pts & 0xFFFFFFFF);
      header.setUint32(8, frame.isKeyFrame ? 1 : 0);

      final packet = Uint8List(12 + frame.size);
      packet.setRange(0, 12, header.buffer.asUint8List());
      packet.setRange(12, 12 + frame.size, frame.data);

      _wsClient!.add(packet);
      _totalBytes += packet.length;
    } catch (e) {
      _wsClient = null;
    }
  }

  Future<int> start({int port = 8080}) async {
    if (_isRunning) return port;

    final handler = const shelf.Pipeline()
        .addMiddleware(shelf.logRequests())
        .addHandler(_handleRequest);

    _server = await shelf_io.serve(handler, InternetAddress.anyIPv4, port);
    _isRunning = true;

    return port;
  }

  shelf.Response _handleRequest(shelf.Request request) {
    if (request.url.path == 'stream') {
      // WebSocket 升级
      final ws = WebSocketTransformer.upgrade(request).then((ws) {
        _wsClient = ws;
        _sequence = 0;
        _totalBytes = 0;

        ws.listen(
          (data) {
            if (data is String) {
              _handleMessage(data);
            }
          },
          onDone: () => _wsClient = null,
          onError: (_) => _wsClient = null,
        );
      });
      return shelf.Response.ok('WebSocket upgrade');
    }

    if (request.url.path == 'info') {
      return shelf.Response.ok(
        '{"device_name":"PhoneCam","codec":"h264","protocol":"websocket"}',
        headers: {'Content-Type': 'application/json'},
      );
    }

    return shelf.Response.notFound('Not Found');
  }

  void _handleMessage(String message) {
    try {
      final json = jsonDecode(message);
      if (json['type'] == 'keyframe_request') {
        onKeyframeRequest?.call();
      }
    } catch (_) {}
  }

  Future<void> stop() async {
    await _wsClient?.close();
    _wsClient = null;
    await _server?.close(force: true);
    _server = null;
    _isRunning = false;
  }

  bool get isRunning => _isRunning;
  bool get isConnected => _wsClient != null;
  int get totalBytes => _totalBytes;
}