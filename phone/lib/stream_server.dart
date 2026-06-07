import 'dart:async';
import 'dart:io';
import 'dart:typed_data';
import 'h264_encoder.dart';

/// H.264 WebSocket 推流服务
class StreamServer {
  HttpServer? _server;
  WebSocket? _client;
  bool _isRunning = false;
  int _sequence = 0;
  int _totalBytes = 0;
  int _keyframeCount = 0;

  /// H.264 帧回调，由 CameraService 调用
  void sendH264Frame(H264Frame frame) {
    if (_client == null || !_client!.closeCode.isNaN) return;

    try {
      // 二进制帧格式: [4B seq][4B pts][4B flags][NAL data...]
      final header = ByteData(12);
      header.setUint32(0, _sequence++);
      header.setUint32(4, frame.pts & 0xFFFFFFFF);
      header.setUint32(8, frame.isKeyFrame ? 1 : 0);

      final packet = Uint8List(12 + frame.size);
      packet.setRange(0, 12, header.buffer.asUint8List());
      packet.setRange(12, 12 + frame.size, frame.data);

      _client!.add(packet);
      _totalBytes += packet.length;
      if (frame.isKeyFrame) _keyframeCount++;
    } catch (e) {
      // 发送失败，断开连接
      _client = null;
    }
  }

  Future<int> start({int port = 8080}) async {
    if (_isRunning) return port;

    _server = await HttpServer.bind(InternetAddress.anyIPv4, port);
    _isRunning = true;

    _server!.listen((HttpRequest request) {
      switch (request.uri.path) {
        case '/stream':
          _handleWebSocket(request);
          break;
        case '/info':
          _handleInfo(request);
          break;
        default:
          request.response
            ..statusCode = HttpStatus.notFound
            ..write('Not Found')
            ..close();
      }
    });

    return port;
  }

  void _handleWebSocket(HttpRequest request) {
    WebSocketTransformer.upgrade(request).then((ws) {
      _client = ws;
      _sequence = 0;
      _totalBytes = 0;
      _keyframeCount = 0;

      ws.listen(
        (data) {
          // 处理客户端消息
          if (data is String) {
            _handleMessage(data);
          }
        },
        onDone: () {
          _client = null;
        },
        onError: (_) {
          _client = null;
        },
      );
    }).catchError((e) {
      // WebSocket 升级失败
    });
  }

  void _handleMessage(String message) {
    // 简单的 JSON 消息处理
    // 可以处理: keyframe_request, config, quality 等
    if (message.contains('keyframe_request')) {
      // 通知编码器请求关键帧
      _onKeyframeRequest?.call();
    }
  }

  void _handleInfo(HttpRequest request) {
    request.response.headers
      ..set('Content-Type', 'application/json')
      ..set('Access-Control-Allow-Origin', '*');
    request.response
      ..write('{"device_name":"PhoneCam","codec":"h264","protocol":"websocket"}')
      ..close();
  }

  Future<void> stop() async {
    await _client?.close();
    _client = null;
    await _server?.close(force: true);
    _server = null;
    _isRunning = false;
  }

  bool get isRunning => _isRunning;
  bool get isConnected => _client != null;
  int get totalBytes => _totalBytes;
  int get keyframeCount => _keyframeCount;

  // 关键帧请求回调
  void Function()? _onKeyframeRequest;
  set onKeyframeRequest(void Function()? callback) {
    _onKeyframeRequest = callback;
  }
}