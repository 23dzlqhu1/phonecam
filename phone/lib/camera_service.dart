import 'dart:async';
import 'dart:typed_data';
import 'package:camera/camera.dart';
import 'package:flutter/material.dart';

/// 摄像头采集服务
/// 初始化后置摄像头，提供 JPEG 帧捕获
class CameraService {
  CameraController? _controller;
  bool _isInitialized = false;

  /// 初始化摄像头
  Future<void> initialize() async {
    if (_isInitialized) return;

    final cameras = await availableCameras();
    if (cameras.isEmpty) {
      throw Exception('未找到可用摄像头');
    }

    // 优先后置摄像头
    final camera = cameras.firstWhere(
      (c) => c.lensDirection == CameraLensDirection.back,
      orElse: () => cameras.first,
    );

    _controller = CameraController(
      camera,
      ResolutionPreset.medium, // 640x480 附近
      enableAudio: false,
      imageFormatGroup: ImageFormatGroup.jpeg,
    );

    await _controller!.initialize();
    _isInitialized = true;
  }

  /// 获取摄像头预览 Widget
  Widget buildPreview() {
    if (!_isInitialized || _controller == null) {
      return const Center(child: Text('摄像头未初始化'));
    }
    return CameraPreview(_controller!);
  }

  /// 捕获一帧 JPEG
  Future<Uint8List?> captureJpeg() async {
    if (!_isInitialized || _controller == null) return null;
    try {
      final file = await _controller!.takePicture();
      return await file.readAsBytes();
    } catch (e) {
      return null;
    }
  }

  /// 获取摄像头分辨率
  Size? get resolution {
    if (!_isInitialized || _controller == null) return null;
    return _controller!.value.previewSize;
  }

  /// 释放资源
  Future<void> dispose() async {
    await _controller?.dispose();
    _controller = null;
    _isInitialized = false;
  }

  bool get isInitialized => _isInitialized;
}

