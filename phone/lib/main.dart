import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'camera_service.dart';
import 'stream_server.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  // 强制竖屏
  SystemChrome.setPreferredOrientations([
    DeviceOrientation.portraitUp,
  ]);
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
      home: const HomePage(),
    );
  }
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> with WidgetsBindingObserver {
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
    // App 进入后台时暂停推流
    if (state == AppLifecycleState.inactive && _isStreaming) {
      _stopStreaming();
    }
  }

  Future<void> _initCamera() async {
    try {
      await _camera.initialize();
      setState(() {
        _isCameraReady = true;
        _statusText = '摄像头就绪，点击开始推流';
      });
    } catch (e) {
      setState(() {
        _statusText = '摄像头初始化失败: $e';
      });
    }
  }

  Future<void> _toggleStreaming() async {
    if (_isStreaming) {
      _stopStreaming();
    } else {
      await _startStreaming();
    }
  }

  Future<void> _startStreaming() async {
    if (!_isCameraReady) return;

    try {
      // 启动 HTTP 服务
      final port = await _server.start(port: 8080);

      // 获取本机 IP
      final ip = await _getLocalIp();

      setState(() {
        _isStreaming = true;
        _statusText = '推流中...';
        _serverUrl = 'http://$ip:$port/video';
      });

      // 开始定时捕获帧并推流
      _captureTimer = Timer.periodic(
        const Duration(milliseconds: 66), // ~15fps
        (_) => _captureAndPush(),
      );
    } catch (e) {
      setState(() {
        _statusText = '启动失败: $e';
      });
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
    if (jpeg != null) {
      _server.updateFrame(jpeg);
    }
  }

  Future<String> _getLocalIp() async {
    try {
      final interfaces = await NetworkInterface.list(
        type: InternetAddressType.IPv4,
        includeLinkLocal: false,
      );
      for (final iface in interfaces) {
        for (final addr in iface.addresses) {
          if (!addr.isLoopback) {
            return addr.address;
          }
        }
      }
    } catch (_) {}
    return '0.0.0.0';
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('PhoneCam'),
        centerTitle: true,
        actions: [
          if (_isStreaming)
            Container(
              margin: const EdgeInsets.only(right: 16),
              child: const Center(
                child: Icon(Icons.fiber_manual_record, color: Colors.red, size: 16),
              ),
            ),
        ],
      ),
      body: Column(
        children: [
          // 摄像头预览区
          Expanded(
            child: Container(
              color: Colors.black,
              child: _isCameraReady
                  ? ClipRect(child: _camera.buildPreview())
                  : const Center(child: CircularProgressIndicator()),
            ),
          ),

          // 控制面板
          Container(
            padding: const EdgeInsets.all(24),
            child: Column(
              children: [
                // 状态文字
                Text(
                  _statusText,
                  style: Theme.of(context).textTheme.bodyLarge,
                  textAlign: TextAlign.center,
                ),
                if (_serverUrl != null) ...[
                  const SizedBox(height: 8),
                  Container(
                    padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                    decoration: BoxDecoration(
                      color: Colors.grey.shade900,
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: Text(
                      _serverUrl!,
                      style: const TextStyle(
                        fontFamily: 'monospace',
                        fontSize: 13,
                        color: Colors.greenAccent,
                      ),
                    ),
                  ),
                ],
                const SizedBox(height: 16),
                // 开始/停止按钮
                SizedBox(
                  width: double.infinity,
                  height: 56,
                  child: FilledButton.icon(
                    onPressed: _isCameraReady ? _toggleStreaming : null,
                    icon: Icon(_isStreaming ? Icons.stop : Icons.play_arrow),
                    label: Text(
                      _isStreaming ? '停止推流' : '开始推流',
                      style: const TextStyle(fontSize: 18),
                    ),
                    style: FilledButton.styleFrom(
                      backgroundColor: _isStreaming ? Colors.red : null,
                    ),
                  ),
                ),
                if (_isStreaming) ...[
                  const SizedBox(height: 8),
                  Text(
                    '连接数: ${_server.clientCount}',
                    style: Theme.of(context).textTheme.bodySmall,
                  ),
                ],
              ],
            ),
          ),
        ],
      ),
    );
  }
}