import 'package:flutter/foundation.dart';

class AppLogEntry {
  const AppLogEntry(this.time, this.level, this.message);

  final DateTime time;
  final String level;
  final String message;
}

class LogStore extends ValueNotifier<List<AppLogEntry>> {
  LogStore() : super(const []);

  void add(String message, {String level = 'info'}) {
    value = [
      AppLogEntry(DateTime.now(), level, message),
      ...value,
    ].take(500).toList(growable: false);
  }

  void replaceHevLines(Iterable<String> lines) {
    final now = DateTime.now();
    final hev = lines
        .where((line) => line.trim().isNotEmpty)
        .map((line) => AppLogEntry(now, 'hev', line))
        .toList(growable: false);
    final local = value.where((entry) => entry.level != 'hev');
    value = [...hev, ...local].take(500).toList(growable: false);
  }
}
