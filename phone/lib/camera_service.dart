import 'dart:async';
import 'dart:typed_data';
import 'package:camera/camera.dart';
import 'package:flutter/material.dart';
import 'h264_encoder.dart';

/// 摄像头采集服务 (H.264 硬编码版)
class CameraService {
  CameraController? _controller;
  bool _isInitialized = false;
  final H264Encoder _encoder = H264Encoder();

  // H.264 帧回调
  void Function(H264Frame frame)? onH264Frame;

  // 统计
  int _frameCount = 0;
  double _fps = 0;
  int _totalBytes = 0;
  Timer? _statsTimer;

  /// 初始化摄像头
  Future<void> initialize() async {
    if (_isInitialized) return;

    final cameras = await availableCameras();
    if (cameras.isEmpty) {
      throw Exception('未找到可用摄像头');
    }

    final camera = cameras.firstWhere(
      (c) => c.lensDirection == CameraLensDirection.back,
      orElse: () => cameras.first,
    );

    _controller = CameraController(
      camera,
      ResolutionPreset.medium, // 640x480
      enableAudio: false,
      imageFormatGroup: ImageFormatGroup.yuv420,
    );

    await _controller!.initialize();

    // 初始化 H.264 编码器
    final size = _controller!.value.previewSize;
    await _encoder.init(
      width: size?.width.toInt() ?? 640,
      height: size?.height.toInt() ?? 480,
      fps: 30,
      bitrate: 1_000_000, // 1Mbps
    );

    _isInitialized = true;

    // 启动统计
    _statsTimer = Timer.periodic(const Duration(seconds: 1), (_) {
      _fps = _frameCount.toDouble();
      _frameCount = 0;
    });
  }

  /// 开始帧流
  Future<void> startStream() async {
    if (!_isInitialized || _controller == null) return;
    if (_controller!.value.isStreamingImages) return;

    await _controller!.startImageStream((CameraImage cameraImage) {
      _processFrame(cameraImage);
    });
  }

  /// 停止帧流
  Future<void> stopStream() async {
    if (_controller?.value.isStreamingImages == true) {
      await _controller!.stopImageStream();
    }
    _statsTimer?.cancel();
  }

  /// 处理摄像头帧 → H.264 编码
  void _processFrame(CameraImage cameraImage) async {
    try {
      // YUV420 转 bytes
      final yuvData = _yuv420ToBytes(cameraImage);
      if (yuvData == null) return;

      // H.264 编码
      final frame = await _encoder.encode(yuvData);
      if (frame != null) {
        _frameCount++;
        _totalBytes += frame.size;
        onH264Frame?.call(frame);
      }
    } catch (e) {
      // 静默处理
    }
  }

  /// YUV420 CameraImage 转 bytes
  Uint8List? _yuv420ToBytes(CameraImage image) {
    try {
      final yPlane = image.planes[0].bytes;
      final uPlane = image.planes[1].bytes;
      final vPlane = image.planes[2].bytes;

      // NV21 格式 (Y + VU interleaved)
      final totalSize = yPlane.length + uPlane.length + vPlane.length;
      final result = Uint8List(totalSize);

      // Y 平面
      result.setRange(0, yPlane.length, yPlane);

      // UV 平面 (交错排列)
      int offset = yPlane.length;
      for (int i = 0; i < uPlane.length; i++) {
        result[offset++] = vPlane[i];
        result[offset++] = uPlane[i];
      }

      return result;
    } catch (e) {
      return null;
    }
  }

  /// 请求关键帧
  Future<void> requestKeyframe() async {
    await _encoder.requestKeyframe();
  }

  /// 获取摄像头预览 Widget
  Widget buildPreview() {
    if (!_isInitialized || _controller == null) {
      return const Center(child: Text('摄像头未初始化'));
    }
    return CameraPreview(_controller!);
  }

  /// 获取统计信息
  Map<String, dynamic> get stats => {
    'fps': _fps,
    'totalBytes': _totalBytes,
    'avgBitrate': _fps > 0 ? (_totalBytes * 8 / _fps / 1000).round() : 0, // kbps
  };

  Size? get resolution {
    if (!_isInitialized || _controller == null) return null;
    return _controller!.value.previewSize;
  }

  Future<void> dispose() async {
    await stopStream();
    await _encoder.release();
    await _controller?.dispose();
    _controller = null;
    _isInitialized = false;
  }

  bool get isInitialized => _isInitialized;
  H264Encoder get encoder => _encoder;
}