import 'package:flutter/material.dart';

import 'models/tunnel_settings.dart';
import 'services/tunnel_manager.dart';
import 'ui/pages/home_page.dart';
import 'ui/pages/logs_page.dart';
import 'ui/pages/settings_page.dart';

class LanSocksShell extends StatefulWidget {
  const LanSocksShell({
    required this.manager,
    required this.settings,
    required this.onSettingsChanged,
    super.key,
  });

  final TunnelManager manager;
  final TunnelSettings settings;
  final ValueChanged<TunnelSettings> onSettingsChanged;

  @override
  State<LanSocksShell> createState() => _LanSocksShellState();
}

class _LanSocksShellState extends State<LanSocksShell> {
  int _selected = 0;

  @override
  Widget build(BuildContext context) {
    final pages = [
      HomePage(manager: widget.manager, settings: widget.settings),
      SettingsPage(
        settings: widget.settings,
        onChanged: widget.onSettingsChanged,
      ),
      LogsPage(logs: widget.manager.logs),
    ];
    final wide = MediaQuery.sizeOf(context).width >= 820;
    return Scaffold(
      appBar: AppBar(
        title: const Text('LanSocks'),
        actions: [
          Padding(
            padding: const EdgeInsets.only(right: 20),
            child: Center(
              child: Text(
                widget.manager.isSupported ? 'Windows' : '仅界面预览',
                style: Theme.of(context).textTheme.labelMedium,
              ),
            ),
          ),
        ],
      ),
      body: Row(
        children: [
          if (wide)
            NavigationRail(
              selectedIndex: _selected,
              onDestinationSelected: (index) =>
                  setState(() => _selected = index),
              labelType: NavigationRailLabelType.all,
              destinations: const [
                NavigationRailDestination(
                  icon: Icon(Icons.vpn_key_outlined),
                  selectedIcon: Icon(Icons.vpn_key),
                  label: Text('连接'),
                ),
                NavigationRailDestination(
                  icon: Icon(Icons.tune_outlined),
                  selectedIcon: Icon(Icons.tune),
                  label: Text('设置'),
                ),
                NavigationRailDestination(
                  icon: Icon(Icons.article_outlined),
                  selectedIcon: Icon(Icons.article),
                  label: Text('日志'),
                ),
              ],
            ),
          Expanded(
            child: IndexedStack(index: _selected, children: pages),
          ),
        ],
      ),
      bottomNavigationBar: wide
          ? null
          : NavigationBar(
              selectedIndex: _selected,
              onDestinationSelected: (index) =>
                  setState(() => _selected = index),
              destinations: const [
                NavigationDestination(
                  icon: Icon(Icons.vpn_key_outlined),
                  selectedIcon: Icon(Icons.vpn_key),
                  label: '连接',
                ),
                NavigationDestination(
                  icon: Icon(Icons.tune_outlined),
                  selectedIcon: Icon(Icons.tune),
                  label: '设置',
                ),
                NavigationDestination(
                  icon: Icon(Icons.article_outlined),
                  selectedIcon: Icon(Icons.article),
                  label: '日志',
                ),
              ],
            ),
    );
  }
}
