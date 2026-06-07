import 'dart:async';
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'camera_service.dart';
import 'stream_server.dart';
import 'ui/home_page.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  SystemChrome.setPreferredOrientations([DeviceOrientation.portraitUp]);
  runApp(const PhoneCamApp());
}

class PhoneCamApp extends StatelessWidget {
  const PhoneCamApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'PhoneCam',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.blue,
          brightness: Brightness.dark,
        ),
        useMaterial3: true,
      ),
      home: const PhoneCamController(),
    );
  }
}

class PhoneCamController extends StatefulWidget {
  const PhoneCamController({super.key});

  @override
  State<PhoneCamController> createState() => _PhoneCamControllerState();
}

class _PhoneCamControllerState extends State<PhoneCamController>
    with WidgetsBindingObserver {
  final CameraService _camera = CameraService();
  final StreamServer _server = StreamServer();

  bool _isStreaming = false;
  bool _isCameraReady = false;
  String _statusText = '正在初始化摄像头...';
  String? _serverUrl;
  String _codecInfo = 'H.264';

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _initCamera();
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _stopStreaming();
    _camera.dispose();
    _server.stop();
    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.inactive && _isStreaming) {
      _stopStreaming();
    }
  }

  Future<void> _initCamera() async {
    try {
      await _camera.initialize();

      // H.264 帧 → WebSocket 推送
      _camera.onH264Frame = (frame) {
        if (_isStreaming) {
          _server.sendH264Frame(frame);
        }
      };

      // 关键帧请求回调
      _server.onKeyframeRequest = () {
        _camera.requestKeyframe();
      };

      setState(() {
        _isCameraReady = true;
        _statusText = '摄像头就绪 (H.264)';
      });
    } catch (e) {
      setState(() {
        _statusText = '摄像头初始化失败: $e';
      });
    }
  }

  Future<void> _startStreaming() async {
    if (!_isCameraReady) return;
    try {
      final port = await _server.start(port: 8080);
      final ip = await _getLocalIp();
      await _camera.startStream();

      setState(() {
        _isStreaming = true;
        _statusText = '推流中 (H.264)';
        _serverUrl = 'http://$ip:$port/stream';
        _codecInfo = 'H.264 · 1Mbps';
      });

      // 定期更新统计
      Timer.periodic(const Duration(seconds: 2), (timer) {
        if (!_isStreaming) {
          timer.cancel();
          return;
        }
        final stats = _camera.stats;
        setState(() {
          _codecInfo = 'H.264 · ${stats['fps']?.round() ?? 0}fps · ${stats['avgBitrate'] ?? 0}kbps';
        });
      });
    } catch (e) {
      setState(() => _statusText = '启动失败: $e');
    }
  }

  void _stopStreaming() {
    _camera.stopStream();
    _server.stop();
    setState(() {
      _isStreaming = false;
      _statusText = '已停止推流';
      _serverUrl = null;
    });
  }

  Future<String> _getLocalIp() async {
    try {
      final interfaces = await NetworkInterface.list(
        type: InternetAddressType.IPv4,
        includeLinkLocal: false,
      );
      for (final iface in interfaces) {
        for (final addr in iface.addresses) {
          if (!addr.isLoopback) return addr.address;
        }
      }
    } catch (_) {}
    return '0.0.0.0';
  }

  @override
  Widget build(BuildContext context) {
    return HomePage(
      onStartStreaming: _startStreaming,
      onStopStreaming: _stopStreaming,
      isStreaming: _isStreaming,
      isCameraReady: _isCameraReady,
      statusText: _statusText,
      serverUrl: _serverUrl,
      clientCount: _server.isConnected ? 1 : 0,
    );
  }
}