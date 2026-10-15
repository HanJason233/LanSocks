enum UdpMode { udp, tcp }

enum ReconnectPolicy { disabled, once }

enum AppTheme { system, light, dark }

class TunnelSettings {
  const TunnelSettings({
    this.mtu = 1500,
    this.udpMode = UdpMode.udp,
    this.logLevel = 'info',
    this.reconnectPolicy = ReconnectPolicy.disabled,
    this.theme = AppTheme.system,
  });

  final int mtu;
  final UdpMode udpMode;
  final String logLevel;
  final ReconnectPolicy reconnectPolicy;
  final AppTheme theme;

  String? validate() {
    if (mtu < 576 || mtu > 9000) return 'TUN MTU 必须在 576 到 9000 之间。';
    if (!const {'debug', 'info', 'warn', 'error'}.contains(logLevel)) {
      return '日志级别无效。';
    }
    return null;
  }

  TunnelSettings copyWith({
    int? mtu,
    UdpMode? udpMode,
    String? logLevel,
    ReconnectPolicy? reconnectPolicy,
    AppTheme? theme,
  }) {
    return TunnelSettings(
      mtu: mtu ?? this.mtu,
      udpMode: udpMode ?? this.udpMode,
      logLevel: logLevel ?? this.logLevel,
      reconnectPolicy: reconnectPolicy ?? this.reconnectPolicy,
      theme: theme ?? this.theme,
    );
  }

  Map<String, Object> toMap() => {
    'mtu': mtu,
    'udpMode': udpMode.name,
    'logLevel': logLevel,
    'reconnectPolicy': reconnectPolicy.name,
    'theme': theme.name,
  };

  factory TunnelSettings.fromMap(Map<String, Object?> map) {
    T read<T extends Enum>(List<T> values, Object? raw, T fallback) {
      return values.firstWhere(
        (item) => item.name == raw,
        orElse: () => fallback,
      );
    }

    return TunnelSettings(
      mtu: map['mtu'] is int ? map['mtu']! as int : 1500,
      udpMode: read(UdpMode.values, map['udpMode'], UdpMode.udp),
      logLevel: map['logLevel'] is String ? map['logLevel']! as String : 'info',
      reconnectPolicy: read(
        ReconnectPolicy.values,
        map['reconnectPolicy'],
        ReconnectPolicy.disabled,
      ),
      theme: read(AppTheme.values, map['theme'], AppTheme.system),
    );
  }
}
