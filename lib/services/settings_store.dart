import 'dart:convert';
import 'dart:io';

import '../models/tunnel_settings.dart';

class SettingsStore {
  Future<TunnelSettings> load() async {
    final file = _settingsFile();
    try {
      if (!await file.exists()) return const TunnelSettings();
      final raw = await file.readAsString();
      final decoded = jsonDecode(raw);
      if (decoded is Map<String, dynamic>) {
        final settings = TunnelSettings.fromMap(decoded);
        return settings.validate() == null ? settings : const TunnelSettings();
      }
    } on FormatException {
      // Ignore damaged preferences and return safe defaults.
    } on FileSystemException {
      // Ignore damaged preferences and return safe defaults.
    }
    return const TunnelSettings();
  }

  Future<void> save(TunnelSettings settings) async {
    final file = _settingsFile();
    await file.parent.create(recursive: true);
    await file.writeAsString(jsonEncode(settings.toMap()), flush: true);
  }

  File _settingsFile() {
    final localAppData = Platform.environment['LOCALAPPDATA'];
    if (Platform.isWindows && localAppData != null && localAppData.isNotEmpty) {
      return File(
        '$localAppData${Platform.pathSeparator}LanSocks'
        '${Platform.pathSeparator}settings.json',
      );
    }

    final home = Platform.environment['HOME'] ?? Directory.systemTemp.path;
    final configHome = Platform.environment['XDG_CONFIG_HOME'];
    final linuxConfig = configHome == null || configHome.isEmpty
        ? '$home${Platform.pathSeparator}.config'
        : configHome;
    final directory = Platform.isMacOS
        ? '$home${Platform.pathSeparator}Library${Platform.pathSeparator}'
              'Application Support${Platform.pathSeparator}LanSocks'
        : Platform.isLinux
        ? '$linuxConfig${Platform.pathSeparator}LanSocks'
        : '$home${Platform.pathSeparator}.lansocks';
    return File('$directory${Platform.pathSeparator}settings.json');
  }
}
