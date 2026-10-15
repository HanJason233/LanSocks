import 'dart:async';
import 'dart:io';

class NetworkMonitor {
  const NetworkMonitor({this.timeout = const Duration(seconds: 10)});

  final Duration timeout;

  Future<String> resolveIpv4(String host) async {
    final literal = InternetAddress.tryParse(host.trim());
    if (literal != null) {
      if (literal.type != InternetAddressType.IPv4) {
        throw const NetworkVerificationException(
          'Windows 当前只接管 IPv4，请使用 IPv4 流量验证目标。',
        );
      }
      return literal.address;
    }
    try {
      final addresses = await InternetAddress.lookup(
        host.trim(),
        type: InternetAddressType.IPv4,
      ).timeout(timeout);
      if (addresses.isEmpty) {
        throw const NetworkVerificationException('流量验证目标没有 IPv4 地址。');
      }
      return addresses.first.address;
    } on SocketException catch (error) {
      throw NetworkVerificationException('通过 TUN 解析验证目标失败：${error.message}');
    } on TimeoutException {
      throw const NetworkVerificationException('通过 TUN 解析验证目标超时。');
    }
  }

  Future<void> verifyRoutedConnectivity(String host, int port) async {
    Socket? socket;
    try {
      socket = await Socket.connect(host.trim(), port, timeout: timeout);
    } on SocketException catch (error) {
      throw NetworkVerificationException('TUN 流量验证失败：${error.message}');
    } on TimeoutException {
      throw const NetworkVerificationException('TUN 流量验证超时。');
    } finally {
      socket?.destroy();
    }
  }
}

class NetworkVerificationException implements Exception {
  const NetworkVerificationException(this.message);
  final String message;
}
