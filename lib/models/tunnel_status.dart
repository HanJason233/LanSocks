enum TunnelPhase {
  disconnected,
  checking,
  connecting,
  connected,
  disconnecting,
  error,
}

class TunnelStatus {
  const TunnelStatus({
    this.phase = TunnelPhase.disconnected,
    this.message = '',
    this.uploadBytes = 0,
    this.downloadBytes = 0,
    this.recoveryAvailable = false,
  });

  final TunnelPhase phase;
  final String message;
  final int uploadBytes;
  final int downloadBytes;
  final bool recoveryAvailable;

  bool get isBusy =>
      phase == TunnelPhase.checking ||
      phase == TunnelPhase.connecting ||
      phase == TunnelPhase.disconnecting;

  bool get isConnected => phase == TunnelPhase.connected;
}
