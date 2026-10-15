import 'package:flutter/material.dart';

import '../../services/log_store.dart';

class LogsPage extends StatelessWidget {
  const LogsPage({required this.logs, super.key});

  final LogStore logs;

  @override
  Widget build(BuildContext context) =>
      ValueListenableBuilder<List<AppLogEntry>>(
        valueListenable: logs,
        builder: (context, entries, _) => Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 980),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Padding(
                  padding: const EdgeInsets.fromLTRB(20, 20, 20, 8),
                  child: Text(
                    '运行日志',
                    style: Theme.of(context).textTheme.headlineSmall,
                  ),
                ),
                const Padding(
                  padding: EdgeInsets.symmetric(horizontal: 20),
                  child: Text('SOCKS5 密码不会写入应用日志。'),
                ),
                const SizedBox(height: 8),
                Expanded(
                  child: entries.isEmpty
                      ? const Center(child: Text('暂无日志'))
                      : ListView.separated(
                          reverse: true,
                          padding: const EdgeInsets.all(20),
                          itemCount: entries.length,
                          separatorBuilder: (_, _) => const Divider(height: 1),
                          itemBuilder: (context, index) {
                            final entry = entries[entries.length - 1 - index];
                            final color = entry.level == 'error'
                                ? Theme.of(context).colorScheme.error
                                : null;
                            return ListTile(
                              dense: true,
                              contentPadding: EdgeInsets.zero,
                              leading: Text(
                                _time(entry.time),
                                style: Theme.of(context).textTheme.labelSmall,
                              ),
                              title: Text(
                                entry.message,
                                style: TextStyle(color: color),
                              ),
                            );
                          },
                        ),
                ),
              ],
            ),
          ),
        ),
      );

  String _time(DateTime time) =>
      '${time.hour.toString().padLeft(2, '0')}:${time.minute.toString().padLeft(2, '0')}:${time.second.toString().padLeft(2, '0')}';
}
