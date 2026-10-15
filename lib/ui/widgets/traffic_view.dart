import 'dart:async';

import 'package:flutter/material.dart';

import '../../models/tunnel_status.dart';

class TrafficView extends StatefulWidget {
  const TrafficView({required this.status, super.key});

  final TunnelStatus status;

  @override
  State<TrafficView> createState() => _TrafficViewState();
}

class _TrafficViewState extends State<TrafficView> {
  Timer? _timer;
  DateTime _sampledAt = DateTime.now();
  int _lastUpload = 0;
  int _lastDownload = 0;
  int _uploadRate = 0;
  int _downloadRate = 0;

  @override
  void initState() {
    super.initState();
    _timer = Timer.periodic(const Duration(seconds: 1), (_) => _sample());
  }

  @override
  void didUpdateWidget(covariant TrafficView oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (widget.status.uploadBytes < _lastUpload ||
        widget.status.downloadBytes < _lastDownload) {
      _lastUpload = widget.status.uploadBytes;
      _lastDownload = widget.status.downloadBytes;
      _uploadRate = 0;
      _downloadRate = 0;
    }
  }

  void _sample() {
    final now = DateTime.now();
    final seconds =
        now.difference(_sampledAt).inMilliseconds.clamp(1, 5000) / 1000;
    setState(() {
      _uploadRate = ((widget.status.uploadBytes - _lastUpload) / seconds)
          .round();
      _downloadRate = ((widget.status.downloadBytes - _lastDownload) / seconds)
          .round();
      _lastUpload = widget.status.uploadBytes;
      _lastDownload = widget.status.downloadBytes;
      _sampledAt = now;
    });
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Card(
    child: Padding(
      padding: const EdgeInsets.all(20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('流量统计', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 16),
          Row(
            children: [
              Expanded(
                child: _TrafficMetric(
                  icon: Icons.arrow_upward,
                  title: '上传',
                  rate: _formatRate(_uploadRate),
                  total: _formatBytes(widget.status.uploadBytes),
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: _TrafficMetric(
                  icon: Icons.arrow_downward,
                  title: '下载',
                  rate: _formatRate(_downloadRate),
                  total: _formatBytes(widget.status.downloadBytes),
                ),
              ),
            ],
          ),
        ],
      ),
    ),
  );

  String _formatRate(int bytes) => '${_formatBytes(bytes)}/s';

  String _formatBytes(int bytes) {
    if (bytes < 1024) return '$bytes B';
    const units = ['KB', 'MB', 'GB', 'TB'];
    var value = bytes / 1024;
    var unit = 0;
    while (value >= 1024 && unit < units.length - 1) {
      value /= 1024;
      unit++;
    }
    return '${value.toStringAsFixed(1)} ${units[unit]}';
  }
}

class _TrafficMetric extends StatelessWidget {
  const _TrafficMetric({
    required this.icon,
    required this.title,
    required this.rate,
    required this.total,
  });

  final IconData icon;
  final String title;
  final String rate;
  final String total;

  @override
  Widget build(BuildContext context) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      Row(
        children: [Icon(icon, size: 18), const SizedBox(width: 6), Text(title)],
      ),
      const SizedBox(height: 6),
      Text(rate, style: Theme.of(context).textTheme.titleLarge),
      Text('累计 $total', style: Theme.of(context).textTheme.bodySmall),
    ],
  );
}
