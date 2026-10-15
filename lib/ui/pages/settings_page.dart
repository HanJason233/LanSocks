import 'package:flutter/material.dart';

import '../../models/tunnel_settings.dart';

class SettingsPage extends StatefulWidget {
  const SettingsPage({
    required this.settings,
    required this.onChanged,
    super.key,
  });

  final TunnelSettings settings;
  final ValueChanged<TunnelSettings> onChanged;

  @override
  State<SettingsPage> createState() => _SettingsPageState();
}

class _SettingsPageState extends State<SettingsPage> {
  late final TextEditingController _mtu = TextEditingController(
    text: widget.settings.mtu.toString(),
  );

  @override
  void didUpdateWidget(covariant SettingsPage oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.settings.mtu != widget.settings.mtu) {
      _mtu.text = widget.settings.mtu.toString();
    }
  }

  @override
  void dispose() {
    _mtu.dispose();
    super.dispose();
  }

  void _update(TunnelSettings settings) => widget.onChanged(settings);

  @override
  Widget build(BuildContext context) {
    final settings = widget.settings;
    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 780),
        child: ListView(
          padding: const EdgeInsets.all(20),
          children: [
            Text('隧道设置', style: Theme.of(context).textTheme.headlineSmall),
            const SizedBox(height: 16),
            Card(
              child: Column(
                children: [
                  SwitchListTile(
                    value: false,
                    onChanged: null,
                    title: const Text('开机自动启动'),
                    subtitle: const Text('尚未实现'),
                    secondary: const Icon(Icons.power_settings_new),
                  ),
                  const Divider(height: 1),
                  ListTile(
                    leading: const Icon(Icons.replay),
                    title: const Text('异常退出后的重连'),
                    subtitle: const Text('最多自动尝试一次'),
                    trailing: DropdownButton<ReconnectPolicy>(
                      value: settings.reconnectPolicy,
                      onChanged: (value) {
                        if (value != null) {
                          _update(settings.copyWith(reconnectPolicy: value));
                        }
                      },
                      items: const [
                        DropdownMenuItem(
                          value: ReconnectPolicy.disabled,
                          child: Text('关闭'),
                        ),
                        DropdownMenuItem(
                          value: ReconnectPolicy.once,
                          child: Text('尝试一次'),
                        ),
                      ],
                    ),
                  ),
                  const Divider(height: 1),
                  ListTile(
                    leading: const Icon(Icons.traffic),
                    title: const Text('SOCKS5 UDP 模式'),
                    subtitle: Text(
                      settings.udpMode == UdpMode.udp
                          ? '标准 UDP ASSOCIATE；服务端需支持 UDP 转发'
                          : 'HEV UDP-in-TCP；需兼容的 SOCKS5 服务端',
                    ),
                    trailing: DropdownButton<UdpMode>(
                      value: settings.udpMode,
                      onChanged: (value) {
                        if (value != null) {
                          _update(settings.copyWith(udpMode: value));
                        }
                      },
                      items: const [
                        DropdownMenuItem(
                          value: UdpMode.udp,
                          child: Text('UDP'),
                        ),
                        DropdownMenuItem(
                          value: UdpMode.tcp,
                          child: Text('TCP'),
                        ),
                      ],
                    ),
                  ),
                  const Divider(height: 1),
                  const ListTile(
                    leading: Icon(Icons.dns_outlined),
                    title: Text('DNS'),
                    subtitle: Text(
                      '保留系统 DNS 地址；IPv4 DNS 服务器路由到 HEV。当前不修改 DNS 配置。',
                    ),
                  ),
                  const Divider(height: 1),
                  ListTile(
                    leading: const Icon(Icons.swap_vert),
                    title: const Text('TUN MTU'),
                    subtitle: const Text('范围 576–9000'),
                    trailing: SizedBox(
                      width: 100,
                      child: TextField(
                        controller: _mtu,
                        textAlign: TextAlign.end,
                        keyboardType: TextInputType.number,
                        decoration: const InputDecoration(
                          suffixText: '字节',
                          isDense: true,
                        ),
                        onSubmitted: _saveMtu,
                        onEditingComplete: () => _saveMtu(_mtu.text),
                      ),
                    ),
                  ),
                  const Divider(height: 1),
                  ListTile(
                    leading: const Icon(Icons.subject),
                    title: const Text('日志级别'),
                    trailing: DropdownButton<String>(
                      value: settings.logLevel,
                      onChanged: (value) {
                        if (value != null) {
                          _update(settings.copyWith(logLevel: value));
                        }
                      },
                      items: const [
                        DropdownMenuItem(value: 'debug', child: Text('Debug')),
                        DropdownMenuItem(value: 'info', child: Text('Info')),
                        DropdownMenuItem(value: 'warn', child: Text('Warn')),
                        DropdownMenuItem(value: 'error', child: Text('Error')),
                      ],
                    ),
                  ),
                  const Divider(height: 1),
                  ListTile(
                    leading: const Icon(Icons.palette_outlined),
                    title: const Text('主题'),
                    trailing: DropdownButton<AppTheme>(
                      value: settings.theme,
                      onChanged: (value) {
                        if (value != null) {
                          _update(settings.copyWith(theme: value));
                        }
                      },
                      items: const [
                        DropdownMenuItem(
                          value: AppTheme.system,
                          child: Text('跟随系统'),
                        ),
                        DropdownMenuItem(
                          value: AppTheme.light,
                          child: Text('浅色'),
                        ),
                        DropdownMenuItem(
                          value: AppTheme.dark,
                          child: Text('深色'),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 12),
            const Card(
              child: ListTile(
                leading: Icon(Icons.info_outline),
                title: Text('IPv6 支持状态'),
                subtitle: Text(
                  'Windows 版本尚未接管 IPv6。应用不会修改系统 IPv6 设置；IPv6 流量可能直连。',
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  void _saveMtu(String value) {
    final parsed = int.tryParse(value);
    if (parsed == null || parsed < 576 || parsed > 9000) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('TUN MTU 必须在 576 到 9000 之间。')),
      );
      _mtu.text = widget.settings.mtu.toString();
      return;
    }
    _update(widget.settings.copyWith(mtu: parsed));
  }
}
