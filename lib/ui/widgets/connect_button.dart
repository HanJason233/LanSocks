import 'package:flutter/material.dart';

import '../../models/tunnel_status.dart';

class ConnectButton extends StatelessWidget {
  const ConnectButton({
    required this.status,
    required this.enabled,
    required this.onPressed,
    super.key,
  });

  final TunnelStatus status;
  final bool enabled;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    final connected = status.isConnected;
    return SizedBox(
      width: double.infinity,
      child: FilledButton.icon(
        onPressed: enabled ? onPressed : null,
        icon: Icon(connected ? Icons.link_off : Icons.link),
        label: Text(connected ? '断开连接' : '连接'),
        style: FilledButton.styleFrom(
          minimumSize: const Size.fromHeight(54),
          backgroundColor: connected
              ? Theme.of(context).colorScheme.error
              : null,
        ),
      ),
    );
  }
}
