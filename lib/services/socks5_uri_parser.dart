import 'dart:convert';

import '../models/socks5_config.dart';

class Socks5UriImport {
  const Socks5UriImport({required this.config, this.name});

  final Socks5Config config;
  final String? name;
}

class Socks5UriParser {
  const Socks5UriParser._();

  static Socks5UriImport parse(String value) {
    final input = value.trim();
    final uri = Uri.tryParse(_escapeUnencodedSlashes(input));
    if (uri == null || !uri.hasScheme || !uri.hasAuthority) {
      throw const FormatException('URI 格式无效。');
    }
    final scheme = uri.scheme.toLowerCase();
    if (scheme != 'socks' && scheme != 'socks5') {
      throw const FormatException('仅支持 socks:// 或 socks5:// 格式。');
    }
    if (uri.host.isEmpty || !uri.hasPort) {
      throw const FormatException('URI 必须包含服务器 IPv4 地址和端口。');
    }
    if ((uri.path.isNotEmpty && uri.path != '/') || uri.hasQuery) {
      throw const FormatException('SOCKS URI 不应包含路径或查询参数。');
    }

    var username = '';
    var password = '';
    final rawUserInfo = _rawUserInfo(input);
    if (rawUserInfo.isNotEmpty) {
      final separator = rawUserInfo.indexOf(':');
      if (separator >= 0) {
        username = _decodeComponent(rawUserInfo.substring(0, separator));
        password = _decodeComponent(rawUserInfo.substring(separator + 1));
      } else {
        final encodedCredentials = _decodeComponent(rawUserInfo);
        final credentials = _decodeBase64Credentials(encodedCredentials);
        final credentialSeparator = credentials.indexOf(':');
        if (credentialSeparator < 0) {
          throw const FormatException('URI 认证信息中缺少用户名和密码分隔符。');
        }
        username = credentials.substring(0, credentialSeparator);
        password = credentials.substring(credentialSeparator + 1);
      }
    }

    final config = Socks5Config(
      address: uri.host,
      port: uri.port,
      username: username,
      password: password,
    );
    final validation = config.validate();
    if (validation != null) throw FormatException(validation);

    final fragmentStart = input.indexOf('#');
    final fragment = fragmentStart < 0
        ? ''
        : _decodeComponent(input.substring(fragmentStart + 1)).trim();
    return Socks5UriImport(
      config: config,
      name: fragment.isEmpty ? null : fragment,
    );
  }

  static String _decodeBase64Credentials(String value) {
    try {
      final bytes = base64Url.decode(base64Url.normalize(value));
      return utf8.decode(bytes);
    } on FormatException {
      throw const FormatException(
        '无法解析 URI 认证信息，请检查 Base64 用户名和密码。',
      );
    }
  }

  static String _decodeComponent(String value) {
    try {
      return Uri.decodeComponent(value);
    } on FormatException {
      throw const FormatException('URI 中包含无效的百分号编码。');
    }
  }

  static String _rawUserInfo(String input) {
    final schemeEnd = input.indexOf('://');
    if (schemeEnd < 0) return '';
    final userInfoStart = schemeEnd + 3;
    final at = input.indexOf('@', userInfoStart);
    if (at < userInfoStart) return '';
    final boundary = input.indexOf(RegExp(r'[?#]'), userInfoStart);
    if (boundary >= 0 && boundary < at) return '';
    return input.substring(userInfoStart, at);
  }

  static String _escapeUnencodedSlashes(String input) {
    final schemeEnd = input.indexOf('://');
    if (schemeEnd < 0) return input;
    final userInfoStart = schemeEnd + 3;
    final at = input.indexOf('@', userInfoStart);
    if (at < userInfoStart) return input;
    final boundary = input.indexOf(RegExp(r'[?#]'), userInfoStart);
    if (boundary >= 0 && boundary < at) return input;
    final userInfo = input.substring(userInfoStart, at).replaceAll('/', '%2F');
    return '${input.substring(0, userInfoStart)}$userInfo'
        '${input.substring(at)}';
  }
}
