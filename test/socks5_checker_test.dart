import 'dart:async';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:lansocks/models/socks5_config.dart';
import 'package:lansocks/services/socks5_checker.dart';

void main() {
  group('Socks5Checker', () {
    test('performs no-auth greeting and a domain CONNECT request', () async {
      final server = await ServerSocket.bind(InternetAddress.loopbackIPv4, 0);
      final served = Completer<List<int>>();
      final subscription = server.listen((socket) async {
        final reader = _Reader(socket);
        try {
          final greeting = await reader.read(3);
          socket.add([0x05, 0x00]);
          await socket.flush();
          final prefix = await reader.read(5);
          final request = [...prefix];
          request.addAll(await reader.read(prefix[4] + 2));
          socket.add([0x05, 0x00, 0x00, 0x01, 127, 0, 0, 1, 0x01, 0xbb]);
          await socket.flush();
          served.complete([...greeting, ...request]);
        } catch (error, stack) {
          if (!served.isCompleted) served.completeError(error, stack);
        } finally {
          await reader.close();
          socket.destroy();
        }
      });

      final result = await const Socks5Checker().check(
        Socks5Config(address: '127.0.0.1', port: server.port),
        targetHost: 'example.com',
        targetPort: 443,
      );
      final request = await served.future;

      expect(result.success, isTrue, reason: result.message);
      expect(request.sublist(0, 3), [0x05, 0x01, 0x00]);
      expect(request[3], 0x05);
      expect(request[4], 0x01);
      expect(request[6], 0x03);
      expect(request[7], 'example.com'.length);
      expect(String.fromCharCodes(request.sublist(8, 19)), 'example.com');
      expect(request.sublist(19), [0x01, 0xbb]);

      await subscription.cancel();
      await server.close();
    });

    test('authenticates without including secrets in its response', () async {
      final server = await ServerSocket.bind(InternetAddress.loopbackIPv4, 0);
      final served = Completer<List<int>>();
      final subscription = server.listen((socket) async {
        final reader = _Reader(socket);
        try {
          await reader.read(3);
          socket.add([0x05, 0x02]);
          await socket.flush();
          final authHeader = await reader.read(2);
          final username = await reader.read(authHeader[1]);
          final passwordLength = (await reader.read(1))[0];
          final password = await reader.read(passwordLength);
          socket.add([0x01, 0x00]);
          await socket.flush();
          final connectPrefix = await reader.read(5);
          await reader.read(connectPrefix[4] + 2);
          socket.add([0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0]);
          await socket.flush();
          served.complete([...username, ...password]);
        } catch (error, stack) {
          if (!served.isCompleted) served.completeError(error, stack);
        } finally {
          await reader.close();
          socket.destroy();
        }
      });

      const password = 'private-token';
      final result = await const Socks5Checker().check(
        Socks5Config(
          address: '127.0.0.1',
          port: server.port,
          username: 'lan-user',
          password: password,
        ),
        targetHost: 'example.com',
        targetPort: 443,
      );

      expect(result.success, isTrue, reason: result.message);
      expect(await served.future, [
        ...'lan-user'.codeUnits,
        ...password.codeUnits,
      ]);
      expect(result.message, isNot(contains(password)));
      await subscription.cancel();
      await server.close();
    });

    test('reports a rejected CONNECT request', () async {
      final server = await ServerSocket.bind(InternetAddress.loopbackIPv4, 0);
      final subscription = server.listen((socket) async {
        final reader = _Reader(socket);
        try {
          await reader.read(3);
          socket.add([0x05, 0x00]);
          await socket.flush();
          final header = await reader.read(5);
          await reader.read(header[4] + 2);
          socket.add([0x05, 0x05, 0x00, 0x01, 0, 0, 0, 0, 0, 0]);
          await socket.flush();
        } finally {
          await reader.close();
          socket.destroy();
        }
      });

      final result = await const Socks5Checker().check(
        Socks5Config(address: '127.0.0.1', port: server.port),
        targetHost: 'example.com',
        targetPort: 443,
      );

      expect(result.success, isFalse);
      expect(result.message, contains('目标连接被拒绝'));
      await subscription.cancel();
      await server.close();
    });
  });
}

class _Reader {
  _Reader(Socket socket) : _iterator = StreamIterator<List<int>>(socket);

  final StreamIterator<List<int>> _iterator;
  final List<int> _buffer = [];

  Future<List<int>> read(int length) async {
    while (_buffer.length < length) {
      if (!await _iterator.moveNext().timeout(const Duration(seconds: 3))) {
        throw const SocketException('closed');
      }
      _buffer.addAll(_iterator.current);
    }
    final result = _buffer.sublist(0, length);
    _buffer.removeRange(0, length);
    return result;
  }

  Future<void> close() => _iterator.cancel();
}
