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

/// 业务控制器，连接 CameraService + StreamServer + UI
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
  Timer? _captureTimer;

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
      setState(() {
        _isCameraReady = true;
        _statusText = '摄像头就绪';
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
      setState(() {
        _isStreaming = true;
        _statusText = '推流中...';
        _serverUrl = 'http://$ip:$port/video';
      });
      _captureTimer = Timer.periodic(
        const Duration(milliseconds: 66),
        (_) => _captureAndPush(),
      );
    } catch (e) {
      setState(() => _statusText = '启动失败: $e');
    }
  }

  void _stopStreaming() {
    _captureTimer?.cancel();
    _captureTimer = null;
    _server.stop();
    setState(() {
      _isStreaming = false;
      _statusText = '已停止推流';
      _serverUrl = null;
    });
  }

  Future<void> _captureAndPush() async {
    if (!_isStreaming) return;
    final jpeg = await _camera.captureJpeg();
    if (jpeg != null) _server.updateFrame(jpeg);
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
      clientCount: _server.clientCount,
    );
  }
}