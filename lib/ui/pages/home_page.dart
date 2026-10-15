import 'dart:io';

import 'package:flutter/material.dart';

import '../../models/socks5_config.dart';
import '../../models/tunnel_settings.dart';
import '../../models/tunnel_status.dart';
import '../../services/tunnel_manager.dart';
import '../widgets/connect_button.dart';
import '../widgets/status_card.dart';
import '../widgets/traffic_view.dart';

class HomePage extends StatefulWidget {
  const HomePage({required this.manager, required this.settings, super.key});

  final TunnelManager manager;
  final TunnelSettings settings;

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  final _formKey = GlobalKey<FormState>();
  final _address = TextEditingController();
  final _port = TextEditingController(text: '1080');
  final _username = TextEditingController();
  final _password = TextEditingController();
  final _target = TextEditingController(text: 'example.com');
  final _targetPort = TextEditingController(text: '443');
  bool _showPassword = false;
  bool _testing = false;

  @override
  void dispose() {
    _address.dispose();
    _port.dispose();
    _username.dispose();
    _password.dispose();
    _target.dispose();
    _targetPort.dispose();
    super.dispose();
  }

  Socks5Config _readConfig() => Socks5Config(
    address: _address.text,
    port: int.tryParse(_port.text) ?? 0,
    username: _username.text,
    password: _password.text,
  );

  Future<void> _test() async {
    if (!_formKey.currentState!.validate()) return;
    setState(() => _testing = true);
    final result = await widget.manager.testSocks5(
      _readConfig(),
      targetHost: _target.text,
      targetPort: int.tryParse(_targetPort.text) ?? 0,
    );
    if (!mounted) return;
    setState(() => _testing = false);
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(result.message),
        behavior: SnackBarBehavior.floating,
      ),
    );
  }

  Future<void> _toggle(TunnelStatus status) async {
    if (status.isConnected) {
      await widget.manager.disconnect();
      return;
    }
    if (status.recoveryAvailable) {
      await widget.manager.recover();
      return;
    }
    if (!_formKey.currentState!.validate()) return;
    await widget.manager.connect(
      _readConfig(),
      widget.settings,
      targetHost: _target.text,
      targetPort: int.tryParse(_targetPort.text) ?? 0,
    );
  }

  @override
  Widget build(BuildContext context) => Center(
    child: ConstrainedBox(
      constraints: const BoxConstraints(maxWidth: 820),
      child: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          Text(
            '将本机 IPv4 流量转发到手机上的 SOCKS5 服务。',
            style: Theme.of(context).textTheme.bodyLarge,
          ),
          const SizedBox(height: 16),
          Card(
            child: Padding(
              padding: const EdgeInsets.all(20),
              child: Form(
                key: _formKey,
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      '手机 SOCKS5 服务',
                      style: Theme.of(context).textTheme.titleMedium,
                    ),
                    const SizedBox(height: 16),
                    Row(
                      children: [
                        Expanded(
                          flex: 3,
                          child: TextFormField(
                            controller: _address,
                            decoration: const InputDecoration(
                              labelText: '服务端 IPv4 地址',
                              hintText: '例如 192.168.1.50',
                              border: OutlineInputBorder(),
                            ),
                            keyboardType: const TextInputType.numberWithOptions(
                              decimal: true,
                            ),
                            validator: (value) {
                              final parsed = value == null
                                  ? null
                                  : InternetAddress.tryParse(value.trim());
                              return parsed == null ||
                                      parsed.type != InternetAddressType.IPv4
                                  ? '请输入有效的 IPv4 地址'
                                  : null;
                            },
                          ),
                        ),
                        const SizedBox(width: 12),
                        Expanded(
                          child: TextFormField(
                            controller: _port,
                            decoration: const InputDecoration(
                              labelText: '端口',
                              border: OutlineInputBorder(),
                            ),
                            keyboardType: TextInputType.number,
                            validator: _validatePort,
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 12),
                    TextFormField(
                      controller: _username,
                      decoration: const InputDecoration(
                        labelText: '用户名（可选）',
                        border: OutlineInputBorder(),
                      ),
                      autofillHints: const [AutofillHints.username],
                    ),
                    const SizedBox(height: 12),
                    TextFormField(
                      controller: _password,
                      obscureText: !_showPassword,
                      decoration: InputDecoration(
                        labelText: '密码（可选）',
                        border: const OutlineInputBorder(),
                        suffixIcon: IconButton(
                          tooltip: _showPassword ? '隐藏密码' : '显示密码',
                          onPressed: () =>
                              setState(() => _showPassword = !_showPassword),
                          icon: Icon(
                            _showPassword
                                ? Icons.visibility_off
                                : Icons.visibility,
                          ),
                        ),
                      ),
                      autofillHints: const [AutofillHints.password],
                    ),
                    const SizedBox(height: 16),
                    Text(
                      '流量验证目标',
                      style: Theme.of(context).textTheme.titleSmall,
                    ),
                    const SizedBox(height: 8),
                    Row(
                      children: [
                        Expanded(
                          flex: 3,
                          child: TextFormField(
                            controller: _target,
                            decoration: const InputDecoration(
                              labelText: '主机名或 IP',
                              hintText: 'example.com',
                              border: OutlineInputBorder(),
                            ),
                            validator: (value) =>
                                value == null || value.trim().isEmpty
                                ? '请输入验证目标'
                                : null,
                          ),
                        ),
                        const SizedBox(width: 12),
                        Expanded(
                          child: TextFormField(
                            controller: _targetPort,
                            decoration: const InputDecoration(
                              labelText: '端口',
                              border: OutlineInputBorder(),
                            ),
                            keyboardType: TextInputType.number,
                            validator: _validatePort,
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 8),
                    Text(
                      '检测会通过 SOCKS5 请求 CONNECT；连接后还会用系统路由直连此目标，验证真实 TUN 转发。',
                      style: Theme.of(context).textTheme.bodySmall,
                    ),
                    const SizedBox(height: 16),
                    OutlinedButton.icon(
                      onPressed: _testing || !widget.manager.isSupported
                          ? null
                          : _test,
                      icon: _testing
                          ? const SizedBox.square(
                              dimension: 16,
                              child: CircularProgressIndicator(strokeWidth: 2),
                            )
                          : const Icon(Icons.network_check),
                      label: const Text('测试 SOCKS5'),
                    ),
                  ],
                ),
              ),
            ),
          ),
          const SizedBox(height: 12),
          ValueListenableBuilder<TunnelStatus>(
            valueListenable: widget.manager.status,
            builder: (context, status, _) => Column(
              children: [
                StatusCard(status: status),
                const SizedBox(height: 12),
                TrafficView(status: status),
                const SizedBox(height: 12),
                ConnectButton(
                  status: status,
                  enabled: widget.manager.isSupported && !status.isBusy,
                  onPressed: () => _toggle(status),
                ),
                if (status.recoveryAvailable) ...[
                  const SizedBox(height: 8),
                  OutlinedButton.icon(
                    onPressed: widget.manager.recover,
                    icon: const Icon(Icons.settings_backup_restore),
                    label: const Text('恢复网络配置'),
                  ),
                ],
              ],
            ),
          ),
          const SizedBox(height: 12),
          Card(
            color: Theme.of(context).colorScheme.tertiaryContainer,
            child: const Padding(
              padding: EdgeInsets.all(16),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Icon(Icons.info_outline),
                  SizedBox(width: 12),
                  Expanded(
                    child: Text(
                      '当前 Windows 版本只接管 IPv4。IPv6 设置不会被更改，IPv6 流量可能绕过隧道。',
                    ),
                  ),
                ],
              ),
            ),
          ),
          if (!widget.manager.isSupported) ...[
            const SizedBox(height: 8),
            const Text(
              '此平台当前只提供 Flutter 界面；原生 TUN 功能尚未实现。',
              textAlign: TextAlign.center,
            ),
          ],
        ],
      ),
    ),
  );

  String? _validatePort(String? value) {
    final port = int.tryParse(value ?? '');
    return port == null || port < 1 || port > 65535 ? '范围 1–65535' : null;
  }
}
