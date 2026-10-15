import 'package:flutter/material.dart';

import '../../models/tunnel_status.dart';

class StatusCard extends StatelessWidget {
  const StatusCard({required this.status, super.key});

  final TunnelStatus status;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final (label, icon, color) = switch (status.phase) {
      TunnelPhase.disconnected => ('未连接', Icons.cloud_off, colorScheme.outline),
      TunnelPhase.checking => ('检查中', Icons.search, colorScheme.tertiary),
      TunnelPhase.connecting => ('连接中', Icons.sync, colorScheme.tertiary),
      TunnelPhase.connected => ('已连接', Icons.verified, colorScheme.primary),
      TunnelPhase.disconnecting => ('正在断开', Icons.sync, colorScheme.tertiary),
      TunnelPhase.error => ('发生错误', Icons.error_outline, colorScheme.error),
    };
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Row(
          children: [
            Icon(icon, color: color, size: 30),
            const SizedBox(width: 14),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(label, style: Theme.of(context).textTheme.titleMedium),
                  if (status.message.isNotEmpty) ...[
                    const SizedBox(height: 4),
                    Text(status.message),
                  ],
                ],
              ),
            ),
            if (status.isBusy)
              const SizedBox.square(
                dimension: 20,
                child: CircularProgressIndicator(strokeWidth: 2),
              ),
          ],
        ),
      ),
    );
  }
}
