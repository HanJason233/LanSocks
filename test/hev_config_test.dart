import 'package:flutter_test/flutter_test.dart';
import 'package:lansocks/models/socks5_config.dart';
import 'package:lansocks/models/tunnel_settings.dart';
import 'package:lansocks/services/hev_config.dart';

void main() {
  group('HevConfig.generate', () {
    test('uses the submitted address and port with official HEV keys', () {
      final yaml = HevConfig.generate(
        const Socks5Config(address: '192.168.1.50', port: 1080),
        const TunnelSettings(),
      );

      expect(yaml, contains('guid: ${HevConfig.tunnelGuid}'));
      expect(yaml, contains('address: 192.168.1.50'));
      expect(yaml, contains('port: 1080'));
      expect(yaml, contains('mapdns:'));
      expect(yaml, contains('  address: ${HevConfig.mapDnsAddress}'));
      expect(yaml, contains('  port: 53'));
      expect(yaml, contains('  network: ${HevConfig.mapDnsNetwork}'));
      expect(yaml, contains('  netmask: ${HevConfig.mapDnsNetmask}'));
      expect(yaml, contains('  cache-size: 10000'));
      expect(yaml, contains("udp: 'udp'"));
      expect(yaml, contains('log-file: stdout'));
      expect(yaml, isNot(contains('192.168.1.50:1080')));
    });

    test('quotes optional credentials and reflects validated settings', () {
      final yaml = HevConfig.generate(
        const Socks5Config(
          address: '10.1.0.9',
          port: 4567,
          username: "user's name",
          password: "a'quoted secret",
        ),
        const TunnelSettings(
          mtu: 1400,
          udpMode: UdpMode.tcp,
          logLevel: 'debug',
        ),
      );

      expect(yaml, contains('mtu: 1400'));
      expect(yaml, contains("username: 'user''s name'"));
      expect(yaml, contains("password: 'a''quoted secret'"));
      expect(yaml, contains("udp: 'tcp'"));
      expect(yaml, contains('log-level: debug'));
    });

    test('rejects invalid SOCKS5 configuration', () {
      expect(
        () => HevConfig.generate(
          const Socks5Config(address: 'localhost', port: 1080),
          const TunnelSettings(),
        ),
        throwsArgumentError,
      );
      expect(
        const Socks5Config(
          address: '192.168.1.50',
          port: 1080,
          username: 'user\npassword: injected',
          password: 'secret',
        ).validate(),
        isNotNull,
      );
    });
  });
}
