import 'dart:async';
import 'dart:convert';
import 'dart:io';

import '../models/socks5_config.dart';

class Socks5CheckResult {
  const Socks5CheckResult({
    required this.success,
    required this.message,
    this.elapsed = Duration.zero,
  });

  final bool success;
  final String message;
  final Duration elapsed;
}

class Socks5Checker {
  const Socks5Checker({this.timeout = const Duration(seconds: 8)});

  final Duration timeout;

  Future<Socks5CheckResult> check(
    Socks5Config config, {
    required String targetHost,
    required int targetPort,
  }) async {
    final invalid = config.validate();
    if (invalid != null) {
      return Socks5CheckResult(success: false, message: invalid);
    }
    if (targetHost.trim().isEmpty || targetPort < 1 || targetPort > 65535) {
      return const Socks5CheckResult(success: false, message: '请输入有效的流量验证目标。');
    }

    final stopwatch = Stopwatch()..start();
    Socket? socket;
    _SocketReader? reader;
    try {
      socket = await Socket.connect(
        config.address.trim(),
        config.port,
        timeout: timeout,
      );
      reader = _SocketReader(socket, timeout);
      final method = config.usesAuthentication ? 0x02 : 0x00;
      socket.add([0x05, 0x01, method]);
      await socket.flush();
      final greeting = await reader.read(2);
      if (greeting[0] != 0x05 || greeting[1] != method) {
        throw const _Socks5Exception('SOCKS5 握手失败：服务端未接受所需认证方式。');
      }

      if (method == 0x02) {
        final username = utf8.encode(config.username);
        final password = utf8.encode(config.password);
        socket.add([
          0x01,
          username.length,
          ...username,
          password.length,
          ...password,
        ]);
        await socket.flush();
        final authReply = await reader.read(2);
        if (authReply[0] != 0x01 || authReply[1] != 0x00) {
          throw const _Socks5Exception('SOCKS5 认证失败，请检查用户名和密码。');
        }
      }

      socket.add(_connectRequest(targetHost.trim(), targetPort));
      await socket.flush();
      final reply = await reader.read(4);
      if (reply[0] != 0x05 || reply[2] != 0x00) {
        throw const _Socks5Exception('SOCKS5 CONNECT 返回了无效响应。');
      }
      if (reply[1] != 0x00) {
        throw _Socks5Exception(_replyMessage(reply[1]));
      }
      await _consumeBoundAddress(reader, reply[3]);
      stopwatch.stop();
      return Socks5CheckResult(
        success: true,
        message: '握手与 CONNECT 检测成功。',
        elapsed: stopwatch.elapsed,
      );
    } on SocketException catch (error) {
      return Socks5CheckResult(
        success: false,
        message: '无法连接 SOCKS5 服务：${error.message}',
      );
    } on TimeoutException {
      return const Socks5CheckResult(success: false, message: 'SOCKS5 检测超时。');
    } on _Socks5Exception catch (error) {
      return Socks5CheckResult(success: false, message: error.message);
    } finally {
      await reader?.cancel();
      socket?.destroy();
    }
  }

  List<int> _connectRequest(String host, int port) {
    final ip = InternetAddress.tryParse(host);
    if (ip != null && ip.type == InternetAddressType.IPv4) {
      return [0x05, 0x01, 0x00, 0x01, ...ip.rawAddress, port >> 8, port & 0xff];
    }
    if (ip != null && ip.type == InternetAddressType.IPv6) {
      return [0x05, 0x01, 0x00, 0x04, ...ip.rawAddress, port >> 8, port & 0xff];
    }
    final domain = utf8.encode(host);
    if (domain.isEmpty || domain.length > 255) {
      throw const _Socks5Exception('检测目标域名长度无效。');
    }
    return [
      0x05,
      0x01,
      0x00,
      0x03,
      domain.length,
      ...domain,
      port >> 8,
      port & 0xff,
    ];
  }

  Future<void> _consumeBoundAddress(
    _SocketReader reader,
    int addressType,
  ) async {
    final length = switch (addressType) {
      0x01 => 4,
      0x04 => 16,
      0x03 => (await reader.read(1))[0],
      _ => throw const _Socks5Exception('SOCKS5 返回了未知地址类型。'),
    };
    await reader.read(length + 2);
  }

  String _replyMessage(int reply) => switch (reply) {
    0x01 => 'SOCKS5 服务器发生一般错误。',
    0x02 => 'SOCKS5 服务器拒绝此请求。',
    0x03 => 'SOCKS5 网络不可达。',
    0x04 => 'SOCKS5 主机不可达。',
    0x05 => 'SOCKS5 目标连接被拒绝。',
    0x06 => 'SOCKS5 TTL 已过期。',
    0x07 => 'SOCKS5 服务器不支持 CONNECT。',
    0x08 => 'SOCKS5 不支持目标地址类型。',
    _ => 'SOCKS5 CONNECT 失败（状态码 $reply）。',
  };
}

class _SocketReader {
  _SocketReader(Socket socket, this.timeout)
    : _iterator = StreamIterator<List<int>>(socket);

  final Duration timeout;
  final StreamIterator<List<int>> _iterator;
  final List<int> _buffer = [];

  Future<List<int>> read(int length) async {
    while (_buffer.length < length) {
      final hasMore = await _iterator.moveNext().timeout(timeout);
      if (!hasMore) throw const _Socks5Exception('SOCKS5 服务端提前关闭了连接。');
      _buffer.addAll(_iterator.current);
    }
    final result = _buffer.sublist(0, length);
    _buffer.removeRange(0, length);
    return result;
  }

  Future<void> cancel() => _iterator.cancel();
}

class _Socks5Exception implements Exception {
  const _Socks5Exception(this.message);
  final String message;
}
