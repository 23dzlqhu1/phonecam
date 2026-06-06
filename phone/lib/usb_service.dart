import 'dart:async';
import 'package:flutter/services.dart';

/// USB 连接状态检测与引导
/// 检查 USB Tethering 是否开启，引导用户开启
class UsbService {
  static const _channel = MethodChannel('com.phonecam/usb');

  /// 检查 USB 是否已连接
  Future<bool> isUsbConnected() async {
    try {
      final result = await _channel.invokeMethod<bool>('isUsbConnected');
      return result ?? false;
    } catch (e) {
      // 非 Android 平台或方法不可用
      return false;
    }
  }

  /// 检查 USB Tethering 是否已开启
  Future<bool> isTetheringActive() async {
    try {
      final result = await _channel.invokeMethod<bool>('isTetheringActive');
      return result ?? false;
    } catch (e) {
      return false;
    }
  }

  /// 引导用户开启 USB Tethering
  Future<void> openTetheringSettings() async {
    try {
      await _channel.invokeMethod('openTetheringSettings');
    } catch (e) {
      print('[UsbService] 无法打开设置: $e');
    }
  }

  /// 获取 USB 网络接口 IP
  Future<String?> getUsbIpAddress() async {
    try {
      final result = await _channel.invokeMethod<String>('getUsbIpAddress');
      return result;
    } catch (e) {
      return null;
    }
  }
}