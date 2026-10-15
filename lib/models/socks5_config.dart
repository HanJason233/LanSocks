import 'dart:convert';
import 'dart:io';

class Socks5Config {
  const Socks5Config({
    required this.address,
    required this.port,
    this.username = '',
    this.password = '',
  });

  final String address;
  final int port;
  final String username;
  final String password;

  bool get usesAuthentication => username.isNotEmpty || password.isNotEmpty;

  String? validate() {
    final parsed = InternetAddress.tryParse(address.trim());
    if (parsed == null || parsed.type != InternetAddressType.IPv4) {
      return '请输入有效的 IPv4 地址。';
    }
    if (port < 1 || port > 65535) return '端口范围必须是 1 到 65535。';
    if ((username.isEmpty) != (password.isEmpty)) return '用户名和密码需要同时填写。';
    if (username.contains(RegExp(r'[\u0000-\u001f\u007f]')) ||
        password.contains(RegExp(r'[\u0000-\u001f\u007f]'))) {
      return '用户名和密码不能包含控制字符。';
    }
    if (utf8.encode(username).length > 255 ||
        utf8.encode(password).length > 255) {
      return 'SOCKS5 用户名和密码最长为 255 字节。';
    }
    return null;
  }
}
