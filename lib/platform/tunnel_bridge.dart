import 'dart:io';

import 'package:flutter/services.dart';

import '../models/socks5_config.dart';
import '../models/tunnel_settings.dart';
import '../models/tunnel_status.dart';
import 'tunnel_platform.dart';

class TunnelBridge implements TunnelPlatform {
  TunnelBridge({MethodChannel? channel})
    : _channel = channel ?? const MethodChannel('lansocks/tunnel');

  final MethodChannel _channel;

  @override
  bool get isSupported => Platform.isWindows;

  @override
  Future<void> prepare() async {
    if (!isSupported) throw UnsupportedError('Windows 原生隧道仅在 Windows 可用。');
    await _channel.invokeMethod<void>('prepare');
  }

  @override
  Future<void> start(
    Socks5Config socks5,
    TunnelSettings settings,
    String hevConfigPath,
  ) async {
    if (!isSupported) throw UnsupportedError('此平台尚未实现原生隧道。');
    await _channel.invokeMethod<void>('start', {
      'configPath': hevConfigPath,
      'socksAddress': socks5.address.trim(),
      'socksPort': socks5.port,
    });
  }

  @override
  Future<void> stop() async {
    if (!isSupported) throw UnsupportedError('此平台尚未实现原生隧道。');
    await _channel.invokeMethod<void>('stop');
  }

  @override
  Future<void> recover() async {
    if (!isSupported) throw UnsupportedError('此平台尚未实现原生隧道。');
    await _channel.invokeMethod<void>('recover');
  }

  @override
  Future<void> verifyRoute(String ipv4Address) async {
    if (!isSupported) throw UnsupportedError('此平台尚未实现原生路由检查。');
    final routed = await _channel.invokeMethod<bool>('verifyRoute', {
      'address': ipv4Address,
    });
    if (routed != true) {
      throw StateError('验证目标没有通过 LanSocks TUN 路由。');
    }
  }

  @override
  Future<TunnelStatus> getStatus() async {
    if (!isSupported) return const TunnelStatus();
    final result = await _channel.invokeMapMethod<String, Object?>('status');
    if (result == null) return const TunnelStatus();
    final active = result['active'] == true;
    final recoveryAvailable = result['recoveryAvailable'] == true;
    return TunnelStatus(
      phase: active
          ? TunnelPhase.connected
          : recoveryAvailable
          ? TunnelPhase.error
          : TunnelPhase.disconnected,
      message: result['message'] as String? ?? '',
      uploadBytes: result['uploadBytes'] as int? ?? 0,
      downloadBytes: result['downloadBytes'] as int? ?? 0,
      recoveryAvailable: recoveryAvailable,
    );
  }

  @override
  Future<List<String>> readLogs() async {
    if (!isSupported) return const [];
    final lines = await _channel.invokeListMethod<String>('logs');
    return lines ?? const [];
  }
}
