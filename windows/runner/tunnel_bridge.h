#ifndef RUNNER_TUNNEL_BRIDGE_H_
#define RUNNER_TUNNEL_BRIDGE_H_

#include <winsock2.h>
#include <Windows.h>

#include <flutter/binary_messenger.h>
#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>

#include <memory>

class TunnelBridge {
 public:
  explicit TunnelBridge(flutter::BinaryMessenger* messenger);
  ~TunnelBridge();

  TunnelBridge(const TunnelBridge&) = delete;
  TunnelBridge& operator=(const TunnelBridge&) = delete;

 private:
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  std::unique_ptr<
      flutter::MethodChannel<flutter::EncodableValue>> channel_;
  HANDLE stop_event_ = nullptr;
};

#endif  // RUNNER_TUNNEL_BRIDGE_H_
