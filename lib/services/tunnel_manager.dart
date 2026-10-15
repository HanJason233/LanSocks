import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';

import '../models/socks5_config.dart';
import '../models/tunnel_settings.dart';
import '../models/tunnel_status.dart';
import '../platform/tunnel_platform.dart';
import 'hev_config.dart';
import 'log_store.dart';
import 'network_monitor.dart';
import 'socks5_checker.dart';

class TunnelManager {
  TunnelManager({
    required this._platform,
    this._checker = const Socks5Checker(),
    this._networkMonitor = const NetworkMonitor(),
  });

  final TunnelPlatform _platform;
  final Socks5Checker _checker;
  final NetworkMonitor _networkMonitor;
  final status = ValueNotifier(const TunnelStatus());
  final logs = LogStore();
  Timer? _poller;
  bool _polling = false;
  bool _recoveredThisRun = false;
  String? _activePassword;
  String? _configPath;
  Socks5Config? _lastConfig;
  TunnelSettings? _lastSettings;
  String? _lastTargetHost;
  int? _lastTargetPort;

  bool get isSupported => _platform.isSupported;

  Future<void> prepare() async {
    if (!isSupported) {
      logs.add('此平台的 TUN 实现尚未完成。', level: 'warning');
      return;
    }
    try {
      await _platform.prepare();
      final current = await _platform.getStatus();
      status.value = current;
      if (current.recoveryAvailable) {
        logs.add('发现上次运行遗留的网络恢复记录，正在恢复。', level: 'warning');
        await _platform.recover();
        status.value = const TunnelStatus();
        logs.add('网络恢复记录已清理。');
      }
    } catch (error) {
      status.value = TunnelStatus(
        phase: TunnelPhase.error,
        message: _clean(error.toString()),
        recoveryAvailable: true,
      );
      logs.add(
        '初始化 Windows 隧道组件失败：${_clean(error.toString())}',
        level: 'error',
      );
    }
  }

  Future<Socks5CheckResult> testSocks5(
    Socks5Config config, {
    required String targetHost,
    required int targetPort,
  }) async {
    final result = await _checker.check(
      config,
      targetHost: targetHost,
      targetPort: targetPort,
    );
    logs.add(
      result.success ? 'SOCKS5 握手与 CONNECT 检测成功。' : result.message,
      level: result.success ? 'info' : 'error',
    );
    return result;
  }

  Future<void> connect(
    Socks5Config socks5,
    TunnelSettings settings, {
    required String targetHost,
    required int targetPort,
  }) async {
    final validation = socks5.validate() ?? settings.validate();
    if (validation != null) {
      _setError(validation);
      return;
    }
    if (!isSupported) {
      _setError('当前平台尚未实现 TUN、路由和 DNS 接管。');
      return;
    }
    if (targetHost.trim().isEmpty || targetPort < 1 || targetPort > 65535) {
      _setError('请输入有效的流量验证目标。');
      return;
    }
    await _connect(
      socks5,
      settings,
      targetHost: targetHost.trim(),
      targetPort: targetPort,
    );
  }

  Future<void> _connect(
    Socks5Config socks5,
    TunnelSettings settings, {
    required String targetHost,
    required int targetPort,
  }) async {
    status.value = const TunnelStatus(
      phase: TunnelPhase.checking,
      message: '正在检查 SOCKS5 服务',
    );
    final proxyCheck = await _checker.check(
      socks5,
      targetHost: targetHost,
      targetPort: targetPort,
    );
    if (!proxyCheck.success) {
      _setError(proxyCheck.message);
      logs.add('SOCKS5 连接检查失败：${proxyCheck.message}', level: 'error');
      return;
    }

    status.value = const TunnelStatus(
      phase: TunnelPhase.connecting,
      message: '正在创建 TUN 并配置 IPv4 路由',
    );
    var startAttempted = false;
    try {
      final config = HevConfig.generate(socks5, settings);
      final path = await _writeRuntimeConfig(config);
      _configPath = path;
      _activePassword = socks5.password.isEmpty ? null : socks5.password;
      _lastConfig = socks5;
      _lastSettings = settings;
      _lastTargetHost = targetHost;
      _lastTargetPort = targetPort;
      startAttempted = true;
      await _platform.start(socks5, settings, path);
      logs.add('HEV 已启动，MapDNS 与 Wintun IPv4 路由验证通过。');
      logs.add('Windows 活动网卡 DNS 已验证：IPv4 使用 MapDNS，IPv6 DNS 被回环阻止。');

      // Resolve after route installation, then confirm both route selection and
      // a real TCP handshake use the host system's routing table.
      final targetAddress = await _networkMonitor.resolveIpv4(targetHost);
      await _platform.verifyRoute(targetAddress);
      await _networkMonitor.verifyRoutedConnectivity(targetAddress, targetPort);
      status.value = const TunnelStatus(
        phase: TunnelPhase.connected,
        message: '已连接，系统流量正通过 HEV 转发',
      );
      _recoveredThisRun = false;
      _startPolling();
      logs.add('TUN 转发连通性验证成功。');
    } catch (error) {
      final message = _clean(error.toString());
      logs.add('连接失败：$message', level: 'error');
      var recoveryAvailable = false;
      if (startAttempted) {
        try {
          final current = await _platform.getStatus();
          if (current.isConnected || current.recoveryAvailable) {
            await _platform.recover();
          }
        } catch (rollbackError) {
          recoveryAvailable = true;
          logs.add(
            '回滚失败，请执行网络恢复：${_clean(rollbackError.toString())}',
            level: 'error',
          );
        }
      }
      await _removeRuntimeConfig();
      status.value = TunnelStatus(
        phase: TunnelPhase.error,
        message: message,
        recoveryAvailable: recoveryAvailable,
      );
    }
  }

  Future<void> disconnect() async {
    if (!_platform.isSupported) {
      status.value = const TunnelStatus();
      return;
    }
    _poller?.cancel();
    _poller = null;
    status.value = const TunnelStatus(
      phase: TunnelPhase.disconnecting,
      message: '正在恢复原始路由',
    );
    try {
      await _platform.stop();
      await _removeRuntimeConfig();
      status.value = const TunnelStatus();
      logs.add('HEV 已停止；本次添加的路由已删除，原始 DNS 设置已恢复。');
    } catch (error) {
      status.value = TunnelStatus(
        phase: TunnelPhase.error,
        message: _clean(error.toString()),
        recoveryAvailable: true,
      );
      logs.add('断开或网络恢复失败：${_clean(error.toString())}', level: 'error');
    }
  }

  Future<void> recover() async {
    if (!_platform.isSupported) return;
    try {
      await _platform.recover();
      await _removeRuntimeConfig();
      status.value = const TunnelStatus();
      logs.add('恢复记录已清理，程序创建的路由已删除。');
    } catch (error) {
      _setError('恢复网络失败：${_clean(error.toString())}', recoverable: true);
    }
  }

  Future<void> refreshStatus() async {
    if (!isSupported || _polling) return;
    _polling = true;
    try {
      final current = await _platform.getStatus();
      if (current.isConnected) {
        status.value = current.copyWithPhase(TunnelPhase.connected);
      } else if (current.recoveryAvailable &&
          status.value.phase == TunnelPhase.connected) {
        _poller?.cancel();
        status.value = TunnelStatus(
          phase: TunnelPhase.error,
          message: current.message.isEmpty
              ? 'HEV 已退出，正在恢复网络路由。'
              : current.message,
          recoveryAvailable: true,
        );
        logs.add('HEV 进程异常退出；正在清理路由。', level: 'error');
        if (!_recoveredThisRun) {
          _recoveredThisRun = true;
          await recover();
          final config = _lastConfig;
          final settings = _lastSettings;
          final host = _lastTargetHost;
          final port = _lastTargetPort;
          if (settings?.reconnectPolicy == ReconnectPolicy.once &&
              config != null &&
              settings != null &&
              host != null &&
              port != null) {
            logs.add('按设置尝试重新连接一次。', level: 'warning');
            await _connect(
              config,
              settings,
              targetHost: host,
              targetPort: port,
            );
          }
        }
      }
      final nativeLogs = await _platform.readLogs();
      if (nativeLogs.isNotEmpty) {
        logs.replaceHevLines(nativeLogs.map(_clean));
      }
    } catch (error) {
      logs.add('读取隧道状态失败：${_clean(error.toString())}', level: 'error');
    } finally {
      _polling = false;
    }
  }

  void _startPolling() {
    _poller?.cancel();
    _poller = Timer.periodic(const Duration(seconds: 1), (_) {
      unawaited(refreshStatus());
    });
  }

  Future<String> _writeRuntimeConfig(String content) async {
    final localAppData = Platform.environment['LOCALAPPDATA'];
    if (localAppData == null || localAppData.isEmpty) {
      throw const FileSystemException('无法确定 Windows 本地应用数据目录。');
    }
    final separator = Platform.pathSeparator;
    final directory = Directory(
      '$localAppData${separator}LanSocks${separator}runtime',
    );
    await directory.create(recursive: true);
    final file = File('${directory.path}${separator}hev.yml');
    await file.writeAsString(content, flush: true);
    return file.path;
  }

  Future<void> _removeRuntimeConfig() async {
    final path = _configPath;
    _configPath = null;
    if (path == null) return;
    final file = File(path);
    if (await file.exists()) await file.delete();
  }

  String _clean(String value) {
    final password = _activePassword;
    return password == null || password.isEmpty
        ? value
        : value.replaceAll(password, '••••');
  }

  void _setError(String message, {bool recoverable = false}) {
    status.value = TunnelStatus(
      phase: TunnelPhase.error,
      message: message,
      recoveryAvailable: recoverable,
    );
  }

  void dispose() {
    _poller?.cancel();
    status.dispose();
    logs.dispose();
  }
}

extension on TunnelStatus {
  TunnelStatus copyWithPhase(TunnelPhase phase) => TunnelStatus(
    phase: phase,
    message: message,
    uploadBytes: uploadBytes,
    downloadBytes: downloadBytes,
    recoveryAvailable: recoveryAvailable,
  );
}
