#include "tunnel_bridge.h"

#include "../../native/windows/lansocks_native.h"

#include <Shellapi.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using flutter::EncodableList;
using flutter::EncodableMap;
using flutter::EncodableValue;

std::wstring Utf8ToWide(const std::string& text) {
  if (text.empty()) return L"";
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                       text.data(), static_cast<int>(text.size()),
                                       nullptr, 0);
  if (size <= 0) return L"";
  std::wstring result(size, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                      static_cast<int>(text.size()), result.data(), size);
  return result;
}

std::string WideToUtf8(const std::wstring& text) {
  if (text.empty()) return "";
  const int size = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      nullptr, 0, nullptr, nullptr);
  if (size <= 0) return "";
  std::string result(size, '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                      static_cast<int>(text.size()), result.data(), size,
                      nullptr, nullptr);
  return result;
}

bool GetString(const EncodableMap& map,
               const char* key,
               std::wstring* result) {
  const auto it = map.find(EncodableValue(key));
  if (it == map.end()) return false;
  const auto* value = std::get_if<std::string>(&it->second);
  if (value == nullptr) return false;
  *result = Utf8ToWide(*value);
  return !result->empty();
}

bool GetInteger(const EncodableMap& map, const char* key, int64_t* result) {
  const auto it = map.find(EncodableValue(key));
  if (it == map.end()) return false;
  if (const auto* value = std::get_if<int32_t>(&it->second)) {
    *result = *value;
    return true;
  }
  if (const auto* value = std::get_if<int64_t>(&it->second)) {
    *result = *value;
    return true;
  }
  return false;
}

std::wstring QuoteArgument(const std::wstring& value) {
  std::wstring quoted = L"\"";
  for (wchar_t ch : value) {
    if (ch == L'\"') quoted.push_back(L'\\');
    quoted.push_back(ch);
  }
  quoted.push_back(L'\"');
  return quoted;
}

bool GetPaths(lansocks::AppPaths* paths, std::wstring* error) {
  return lansocks::GetAppPaths(paths, error);
}

void ReadLastErrorOrDefault(const lansocks::AppPaths& paths,
                            std::wstring* error,
                            const wchar_t* fallback) {
  *error = lansocks::ReadLastError(paths);
  if (error->empty()) *error = fallback;
}

bool LaunchHelper(const lansocks::AppPaths& paths,
                  const std::wstring& parameters,
                  HANDLE* process,
                  std::wstring* error) {
  SHELLEXECUTEINFOW execute{};
  execute.cbSize = sizeof(execute);
  execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
  execute.lpVerb = L"runas";
  execute.lpFile = paths.helperPath.c_str();
  execute.lpParameters = parameters.c_str();
  execute.nShow = SW_HIDE;
  if (!ShellExecuteExW(&execute)) {
    const DWORD code = GetLastError();
    if (code == ERROR_CANCELLED) {
      *error = L"已取消管理员授权，网络配置没有更改。";
    } else {
      *error = L"无法启动 Windows 网络辅助程序（Windows 错误 " +
               std::to_wstring(code) + L"）。";
    }
    return false;
  }
  *process = execute.hProcess;
  return true;
}

bool StartHelper(const std::wstring& configPath,
                 const std::wstring& socksAddress,
                 int64_t socksPort,
                 HANDLE stopEvent,
                 std::wstring* error) {
  lansocks::AppPaths paths{};
  if (!GetPaths(&paths, error)) return false;
  if (stopEvent != nullptr &&
      GetFileAttributesW(paths.statePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
    ResetEvent(stopEvent);
  }
  const std::wstring parameters =
      L"--start " + QuoteArgument(configPath) + L" " +
      QuoteArgument(socksAddress) + L" " + std::to_wstring(socksPort) + L" " +
      std::to_wstring(GetCurrentProcessId());
  HANDLE process = nullptr;
  if (!LaunchHelper(paths, parameters, &process, error)) return false;

  const DWORD helperProcessId = GetProcessId(process);
  const DWORD start = GetTickCount();
  while (GetTickCount() - start < 30000) {
    lansocks::SessionState state{};
    if (lansocks::ReadSession(paths, &state) &&
        state.phase == lansocks::SessionPhase::Active &&
        state.helperPid == helperProcessId &&
        state.parentPid == GetCurrentProcessId() &&
        lansocks::IsProcessAlive(state.hevPid)) {
      CloseHandle(process);
      return true;
    }
    if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
      CloseHandle(process);
      ReadLastErrorOrDefault(paths, error,
                             L"Windows 网络辅助程序未能启动 HEV。");
      return false;
    }
    Sleep(100);
  }
  CloseHandle(process);
  *error = L"等待 HEV 和 Wintun 初始化超时。";
  return false;
}

bool StopHelper(bool recover, HANDLE stopEvent, std::wstring* error) {
  lansocks::AppPaths paths{};
  if (!GetPaths(&paths, error)) return false;
  lansocks::SessionState state{};
  const bool stateExists =
      GetFileAttributesW(paths.statePath.c_str()) != INVALID_FILE_ATTRIBUTES;
  const bool stateReadable = lansocks::ReadSession(paths, &state);
  if (!stateExists) {
    if (stopEvent != nullptr) ResetEvent(stopEvent);
    return true;
  }
  if (stopEvent != nullptr && stateReadable &&
      lansocks::IsProcessAlive(state.helperPid)) {
    if (!SetEvent(stopEvent)) {
      *error = L"Unable to signal the running route helper.";
      return false;
    }
    const DWORD start = GetTickCount();
    while (GetTickCount() - start < 30000) {
      const bool stillPresent =
          GetFileAttributesW(paths.statePath.c_str()) != INVALID_FILE_ATTRIBUTES;
      if (!stillPresent && !lansocks::IsProcessAlive(state.helperPid)) {
        ResetEvent(stopEvent);
        return true;
      }
      Sleep(100);
    }
    ReadLastErrorOrDefault(paths, error,
                           L"Timed out while restoring the previous routes.");
    return false;
  }

  HANDLE process = nullptr;
  if (!LaunchHelper(paths, recover ? L"--recover" : L"--stop", &process,
                    error)) {
    return false;
  }
  const DWORD wait = WaitForSingleObject(process, 35000);
  DWORD exitCode = 0;
  GetExitCodeProcess(process, &exitCode);
  CloseHandle(process);
  if (wait != WAIT_OBJECT_0 || exitCode != 0) {
    ReadLastErrorOrDefault(paths, error,
                           L"Windows 网络恢复失败。请重试“恢复网络配置”。");
    return false;
  }
  return true;
}

void HandleStart(const EncodableMap& args,
                 HANDLE stopEvent,
                 std::unique_ptr<flutter::MethodResult<EncodableValue>> result) {
  std::wstring configPath;
  std::wstring socksAddress;
  int64_t socksPort = 0;
  if (!GetString(args, "configPath", &configPath) ||
      !GetString(args, "socksAddress", &socksAddress) ||
      !GetInteger(args, "socksPort", &socksPort) ||
      socksPort < 1 || socksPort > 65535) {
    result->Error("invalid_arguments", "隧道配置参数无效。");
    return;
  }
  std::thread([configPath = std::move(configPath),
               socksAddress = std::move(socksAddress), socksPort, stopEvent,
               result = std::move(result)]() mutable {
    std::wstring error;
    if (StartHelper(configPath, socksAddress, socksPort, stopEvent, &error)) {
      result->Success();
    } else {
      result->Error("start_failed", WideToUtf8(error));
    }
  }).detach();
}

void HandleStop(bool recover,
                HANDLE stopEvent,
                std::unique_ptr<flutter::MethodResult<EncodableValue>> result) {
  std::thread([recover, stopEvent, result = std::move(result)]() mutable {
    std::wstring error;
    if (StopHelper(recover, stopEvent, &error)) {
      result->Success();
    } else {
      result->Error(recover ? "recovery_failed" : "stop_failed",
                    WideToUtf8(error));
    }
  }).detach();
}

EncodableMap StatusMap() {
  EncodableMap response;
  lansocks::AppPaths paths{};
  std::wstring error;
  if (!GetPaths(&paths, &error)) {
    response[EncodableValue("active")] = EncodableValue(false);
    response[EncodableValue("recoveryAvailable")] = EncodableValue(false);
    response[EncodableValue("message")] = EncodableValue(WideToUtf8(error));
    response[EncodableValue("uploadBytes")] = EncodableValue(int64_t{0});
    response[EncodableValue("downloadBytes")] = EncodableValue(int64_t{0});
    return response;
  }

  lansocks::SessionState state{};
  const bool readable = lansocks::ReadSession(paths, &state);
  const bool stateExists =
      readable ||
      GetFileAttributesW(paths.statePath.c_str()) != INVALID_FILE_ATTRIBUTES;
  bool active =
      readable && state.phase == lansocks::SessionPhase::Active &&
      lansocks::IsProcessAlive(state.helperPid) &&
      lansocks::IsProcessAlive(state.parentPid) &&
      lansocks::IsProcessAlive(state.hevPid);
  uint64_t upload = 0;
  uint64_t download = 0;
  if (active) {
    MIB_IF_ROW2 row{};
    row.InterfaceIndex = state.tunnelIndex;
    if (GetIfEntry2(&row) == NO_ERROR && row.OperStatus == IfOperStatusUp) {
      upload = row.OutOctets;
      download = row.InOctets;
    } else {
      active = false;
    }
  }
  const bool recovery = stateExists && !active;
  response[EncodableValue("active")] = EncodableValue(active);
  response[EncodableValue("recoveryAvailable")] = EncodableValue(recovery);
  response[EncodableValue("message")] =
      EncodableValue(recovery ? "发现待恢复的网络配置。" : "");
  response[EncodableValue("uploadBytes")] =
      EncodableValue(static_cast<int64_t>(upload));
  response[EncodableValue("downloadBytes")] =
      EncodableValue(static_cast<int64_t>(download));
  return response;
}

EncodableList ReadLogLines() {
  EncodableList lines;
  lansocks::AppPaths paths{};
  std::wstring error;
  if (!GetPaths(&paths, &error)) return lines;
  std::ifstream stream(std::filesystem::path(paths.logPath), std::ios::binary);
  if (!stream) return lines;
  std::vector<std::string> tail;
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    tail.push_back(line);
    if (tail.size() > 400) tail.erase(tail.begin());
  }
  for (const auto& item : tail) {
    lines.emplace_back(EncodableValue(item));
  }
  return lines;
}

bool VerifyRoute(const std::string& address) {
  const std::wstring wide = Utf8ToWide(address);
  IN_ADDR destination{};
  if (!lansocks::ParseIpv4(wide, &destination)) return false;
  lansocks::AppPaths paths{};
  std::wstring error;
  lansocks::SessionState state{};
  return GetPaths(&paths, &error) &&
         lansocks::ReadSession(paths, &state) &&
         state.phase == lansocks::SessionPhase::Active &&
         lansocks::IsProcessAlive(state.helperPid) &&
         lansocks::BestRouteUsesInterface(destination, state.tunnelIndex);
}

}  // namespace

TunnelBridge::TunnelBridge(flutter::BinaryMessenger* messenger)
    : channel_(std::make_unique<flutter::MethodChannel<EncodableValue>>(
          messenger, "lansocks/tunnel",
          &flutter::StandardMethodCodec::GetInstance())) {
  stop_event_ = CreateEventW(nullptr, TRUE, FALSE, L"Local\\LanSocks.StopEvent");
  channel_->SetMethodCallHandler(
      [this](const auto& call, auto result) {
        HandleMethodCall(call, std::move(result));
      });
}

TunnelBridge::~TunnelBridge() {
  if (channel_) channel_->SetMethodCallHandler(nullptr);
  if (stop_event_ != nullptr) CloseHandle(stop_event_);
}

void TunnelBridge::HandleMethodCall(
    const flutter::MethodCall<EncodableValue>& call,
    std::unique_ptr<flutter::MethodResult<EncodableValue>> result) {
  const std::string& method = call.method_name();
  if (method == "prepare") {
    lansocks::AppPaths paths{};
    std::wstring error;
    if (GetPaths(&paths, &error)) {
      result->Success();
    } else {
      result->Error("prepare_failed", WideToUtf8(error));
    }
    return;
  }
  if (method == "start") {
    const auto* args = std::get_if<EncodableMap>(call.arguments());
    if (args == nullptr) {
      result->Error("invalid_arguments", "隧道配置参数无效。");
      return;
    }
    HandleStart(*args, stop_event_, std::move(result));
    return;
  }
  if (method == "stop" || method == "recover") {
    HandleStop(method == "recover", stop_event_, std::move(result));
    return;
  }
  if (method == "status") {
    result->Success(EncodableValue(StatusMap()));
    return;
  }
  if (method == "logs") {
    result->Success(EncodableValue(ReadLogLines()));
    return;
  }
  if (method == "verifyRoute") {
    const auto* args = std::get_if<EncodableMap>(call.arguments());
    std::wstring address;
    if (args == nullptr || !GetString(*args, "address", &address)) {
      result->Error("invalid_arguments", "验证地址无效。");
      return;
    }
    result->Success(EncodableValue(VerifyRoute(WideToUtf8(address))));
    return;
  }
  result->NotImplemented();
}
