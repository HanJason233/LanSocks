import 'dart:async';

import 'package:flutter/material.dart';

import 'app.dart';
import 'models/tunnel_settings.dart';
import 'platform/tunnel_bridge.dart';
import 'services/settings_store.dart';
import 'services/tunnel_manager.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const LanSocksApp());
}

class LanSocksApp extends StatefulWidget {
  const LanSocksApp({super.key});

  @override
  State<LanSocksApp> createState() => _LanSocksAppState();
}

class _LanSocksAppState extends State<LanSocksApp> {
  final _settingsStore = SettingsStore();
  final _settings = ValueNotifier(const TunnelSettings());
  late final TunnelManager _tunnelManager = TunnelManager(
    platform: TunnelBridge(),
  );
  bool _ready = false;

  @override
  void initState() {
    super.initState();
    unawaited(_initialize());
  }

  Future<void> _initialize() async {
    _settings.value = await _settingsStore.load();
    await _tunnelManager.prepare();
    if (mounted) setState(() => _ready = true);
  }

  Future<void> _saveSettings(TunnelSettings settings) async {
    _settings.value = settings;
    await _settingsStore.save(settings);
  }

  @override
  void dispose() {
    _settings.dispose();
    _tunnelManager.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder<TunnelSettings>(
      valueListenable: _settings,
      builder: (context, settings, _) {
        final themeMode = switch (settings.theme) {
          AppTheme.system => ThemeMode.system,
          AppTheme.light => ThemeMode.light,
          AppTheme.dark => ThemeMode.dark,
        };
        return MaterialApp(
          title: 'LanSocks',
          debugShowCheckedModeBanner: false,
          themeMode: themeMode,
          theme: ThemeData(
            useMaterial3: true,
            colorSchemeSeed: const Color(0xff286f68),
            brightness: Brightness.light,
          ),
          darkTheme: ThemeData(
            useMaterial3: true,
            colorSchemeSeed: const Color(0xff63c8b7),
            brightness: Brightness.dark,
          ),
          home: !_ready
              ? const _StartupPage()
              : LanSocksShell(
                  manager: _tunnelManager,
                  settings: settings,
                  onSettingsChanged: _saveSettings,
                ),
        );
      },
    );
  }
}

class _StartupPage extends StatelessWidget {
  const _StartupPage();

  @override
  Widget build(BuildContext context) => const Scaffold(
    body: Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          CircularProgressIndicator(),
          SizedBox(height: 16),
          Text('正在检查本机网络恢复记录…'),
        ],
      ),
    ),
  );
}
