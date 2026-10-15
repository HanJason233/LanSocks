import '../models/socks5_config.dart';
import '../models/tunnel_settings.dart';
import '../models/tunnel_status.dart';

abstract interface class TunnelPlatform {
  bool get isSupported;

  Future<void> prepare();

  Future<void> start(
    Socks5Config socks5,
    TunnelSettings settings,
    String hevConfigPath,
  );

  Future<void> stop();

  Future<void> recover();

  Future<void> verifyRoute(String ipv4Address);

  Future<TunnelStatus> getStatus();

  Future<List<String>> readLogs();
}

class UnsupportedTunnelPlatform implements TunnelPlatform {
  const UnsupportedTunnelPlatform();

  @override
  bool get isSupported => false;

  Never _unsupported() => throw UnsupportedError('此平台尚未实现原生 TUN 和路由功能。');

  @override
  Future<void> prepare() async => _unsupported();

  @override
  Future<void> start(
    Socks5Config socks5,
    TunnelSettings settings,
    String hevConfigPath,
  ) async => _unsupported();

  @override
  Future<void> stop() async => _unsupported();

  @override
  Future<void> recover() async => _unsupported();

  @override
  Future<void> verifyRoute(String ipv4Address) async => _unsupported();

  @override
  Future<TunnelStatus> getStatus() async => _unsupported();

  @override
  Future<List<String>> readLogs() async => _unsupported();
}
