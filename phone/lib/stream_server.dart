import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

/// MJPEG HTTP 推流服务
/// 手机端启动 HTTP server，通过 multipart/x-mixed-replace 输出 MJPEG 流
class StreamServer {
  HttpServer? _server;
  final List<Socket> _clients = [];
  bool _isRunning = false;
  Timer? _frameTimer;

  // 帧率控制: 15fps = ~66ms per frame
  static const int fps = 15;
  static const int frameIntervalMs = 1000 ~/ fps;
  static const String boundary = '--frame';

  /// 当前帧数据，由外部更新
  Uint8List? _currentFrame;

  /// 更新当前帧
  void updateFrame(Uint8List jpegData) {
    _currentFrame = jpegData;
  }

  /// 启动 HTTP 服务
  Future<int> start({int port = 8080}) async {
    if (_isRunning) return port;

    _server = await HttpServer.bind(InternetAddress.anyIPv4, port);
    _isRunning = true;

    _server!.listen((HttpRequest request) {
      switch (request.uri.path) {
        case '/video':
          _handleMjpegStream(request);
          break;
        case '/info':
          _handleInfo(request);
          break;
        case '/snapshot':
          _handleSnapshot(request);
          break;
        default:
          request.response
            ..statusCode = HttpStatus.notFound
            ..write('Not Found')
            ..close();
      }
    });

    // 定时推帧给所有客户端
    _frameTimer = Timer.periodic(
      const Duration(milliseconds: frameIntervalMs),
      (_) => _broadcastFrame(),
    );

    return port;
  }

  /// 处理 MJPEG 流请求
  void _handleMjpegStream(HttpRequest request) {
    request.response.headers
      ..set('Content-Type', 'multipart/x-mixed-replace; boundary=$boundary')
      ..set('Cache-Control', 'no-cache')
      ..set('Connection', 'keep-alive')
      ..set('Access-Control-Allow-Origin', '*');

    // 不关闭 response，持续推送
    _clients.add(request.response.connection!.socket);
  }

  /// 处理设备信息请求
  void _handleInfo(HttpRequest request) {
    request.response.headers
      ..set('Content-Type', 'application/json')
      ..set('Access-Control-Allow-Origin', '*');
    request.response
      ..write('{"device_name":"PhoneCam","fps":$fps,"resolution":"640x480"}')
      ..close();
  }

  /// 处理单帧快照请求
  void _handleSnapshot(HttpRequest request) {
    final frame = _currentFrame;
    if (frame == null) {
      request.response
        ..statusCode = HttpStatus.serviceUnavailable
        ..write('No frame available')
        ..close();
      return;
    }
    request.response.headers
      ..set('Content-Type', 'image/jpeg')
      ..set('Access-Control-Allow-Origin', '*');
    request.response
      ..add(frame)
      ..close();
  }

  /// 广播当前帧给所有客户端
  void _broadcastFrame() {
    final frame = _currentFrame;
    if (frame == null || _clients.isEmpty) return;

    final header = '$boundary\r\n'
        'Content-Type: image/jpeg\r\n'
        'Content-Length: ${frame.length}\r\n'
        '\r\n';

    final headerBytes = Uint8List.fromList(header.codeUnits);

    final deadClients = <Socket>[];
    for (final client in _clients) {
      try {
        client.add(headerBytes);
        client.add(frame);
        client.add(Uint8List.fromList([0x0D, 0x0A])); // \r\n
      } catch (e) {
        deadClients.add(client);
      }
    }

    // 清理断开的客户端
    for (final dead in deadClients) {
      _clients.remove(dead);
      try { dead.close(); } catch (_) {}
    }
  }

  /// 停止服务
  Future<void> stop() async {
    _frameTimer?.cancel();
    _frameTimer = null;

    for (final client in _clients) {
      try { client.close(); } catch (_) {}
    }
    _clients.clear();

    await _server?.close(force: true);
    _server = null;
    _isRunning = false;
  }

  bool get isRunning => _isRunning;
  int get clientCount => _clients.length;
}