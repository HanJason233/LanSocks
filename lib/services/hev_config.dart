import '../models/socks5_config.dart';
import '../models/tunnel_settings.dart';

class HevConfig {
  static const tunnelName = 'LanSocks';
  static const tunnelGuid = '6f06c4a2-6f95-4f25-98f0-cf4608d50b41';
  static const tunnelIpv4 = '198.18.0.1';

  static String generate(Socks5Config socks5, TunnelSettings settings) {
    final configError = socks5.validate() ?? settings.validate();
    if (configError != null) throw ArgumentError(configError);

    final lines = <String>[
      'tunnel:',
      '  name: $tunnelName',
      '  guid: $tunnelGuid',
      '  mtu: ${settings.mtu}',
      '  multi-queue: false',
      '  ipv4: $tunnelIpv4',
      '  icmp: \'off\'',
      'socks5:',
      '  address: ${socks5.address.trim()}',
      '  port: ${socks5.port}',
      '  udp: \'${settings.udpMode.name}\'',
    ];
    if (socks5.usesAuthentication) {
      lines
        ..add('  username: ${_yamlString(socks5.username)}')
        ..add('  password: ${_yamlString(socks5.password)}');
    }
    lines
      ..add('misc:')
      ..add('  log-file: stdout')
      ..add('  log-level: ${settings.logLevel}');
    return lines.join(String.fromCharCode(10)) + String.fromCharCode(10);
  }

  static String _yamlString(String value) => "'${value.replaceAll("'", "''")}'";
}
