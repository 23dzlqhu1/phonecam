import 'dart:async';
import 'dart:typed_data';
import 'package:flutter/services.dart';

/// H.264 硬编码器 (通过 Platform Channel 调用 Android MediaCodec)
class H264Encoder {
  static const _channel = MethodChannel('com.phonecam/h264');

  bool _isInitialized = false;

  /// 初始化编码器
  Future<Map<String, dynamic>?> init({
    int width = 640,
    int height = 480,
    int fps = 30,
    int bitrate = 1_000_000,
  }) async {
    try {
      final result = await _channel.invokeMethod('init', {
        'width': width,
        'height': height,
        'fps': fps,
        'bitrate': bitrate,
      });
      _isInitialized = true;
      return Map<String, dynamic>.from(result);
    } catch (e) {
      _isInitialized = false;
      return null;
    }
  }

  /// 编码一帧 YUV420 数据，返回 H.264 NAL 单元
  Future<H264Frame?> encode(Uint8List yuvData) async {
    if (!_isInitialized) return null;
    try {
      final result = await _channel.invokeMethod('encode', {'data': yuvData});
      if (result == null) return null;
      final map = Map<String, dynamic>.from(result);
      return H264Frame(
        data: map['data'] as Uint8List,
        pts: map['pts'] as int,
        isKeyFrame: map['keyframe'] as bool,
        size: map['size'] as int,
      );
    } catch (e) {
      return null;
    }
  }

  /// 请求关键帧
  Future<bool> requestKeyframe() async {
    try {
      final result = await _channel.invokeMethod('requestKeyframe');
      return result == true;
    } catch (e) {
      return false;
    }
  }

  /// 释放编码器
  Future<void> release() async {
    try {
      await _channel.invokeMethod('release');
    } catch (_) {}
    _isInitialized = false;
  }

  bool get isInitialized => _isInitialized;
}

/// H.264 编码帧
class H264Frame {
  final Uint8List data;
  final int pts; // presentation timestamp (微秒)
  final bool isKeyFrame;
  final int size;

  const H264Frame({
    required this.data,
    required this.pts,
    required this.isKeyFrame,
    required this.size,
  });
}