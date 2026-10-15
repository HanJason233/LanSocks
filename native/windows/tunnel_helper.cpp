#include "lansocks_native.h"

#include <iphlpapi.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace lansocks {
namespace {

constexpr wchar_t kSessionMutexName[] = L"Local\\LanSocks.SessionLock";
constexpr wchar_t kStopEventName[] = L"Local\\LanSocks.StopEvent";
constexpr wchar_t kTunnelName[] = L"LanSocks";
constexpr wchar_t kRunnerName[] = L"lansocks.exe";
constexpr wchar_t kExpectedIpv4[] = L"198.18.0.1";
constexpr DWORD kTunnelRouteMetric = 5;
constexpr DWORD kBypassRouteMetric = 3;

std::wstring Trim(const std::wstring& value) {
  const auto first = value.find_first_not_of(L" \t");
  if (first == std::wstring::npos) return L"";
  const auto last = value.find_last_not_of(L" \t");
  return value.substr(first, last - first + 1);
}

bool ReadFileBytes(const std::wstring& path, std::string* bytes) {
  std::ifstream stream(std::filesystem::path(path), std::ios::binary);
  if (!stream) return false;
  bytes->assign(std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());
  return stream.good() || stream.eof();
}

std::wstring ReadHevLogTail(const std::wstring& path) {
  constexpr std::streamoff kMaxLogTailBytes = 4096;
  std::ifstream stream(std::filesystem::path(path), std::ios::binary);
  if (!stream) return L"";

  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  if (size <= 0) return L"";
  const std::streamoff start =
      size > kMaxLogTailBytes ? size - kMaxLogTailBytes : 0;
  stream.seekg(start, std::ios::beg);
  std::string tail(static_cast<size_t>(size - start), '\0');
  stream.read(tail.data(), static_cast<std::streamsize>(tail.size()));
  if (!stream && !stream.eof()) return L"";

  // A byte-limited tail can start in the middle of a UTF-8 line. Drop that
  // partial line so the diagnostic remains valid UTF-8.
  if (start > 0) {
    const size_t firstNewline = tail.find('\n');
    if (firstNewline == std::string::npos) return L"";
    tail.erase(0, firstNewline + 1);
  }
  return Utf8ToWide(tail);
}

bool ParseUnsigned(const std::wstring& text, ULONG maximum, ULONG* value) {
  if (text.empty()) return false;
  wchar_t* end = nullptr;
  const unsigned long parsed = wcstoul(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != L'\0' || parsed == 0 ||
      parsed > maximum) {
    return false;
  }
  *value = static_cast<ULONG>(parsed);
  return true;
}

bool IsSafeQuotedScalar(const std::wstring& value) {
  if (value.size() < 2 || value.front() != L'\'' || value.back() != L'\'') {
    return false;
  }
  for (size_t i = 1; i + 1 < value.size(); ++i) {
    const wchar_t ch = value[i];
    if (ch < 0x20 || ch == 0x7f) return false;
    if (ch == L'\'') {
      if (i + 2 >= value.size() || value[i + 1] != L'\'') return false;
      ++i;
    }
  }
  return true;
}

bool IsValidSecret(const std::wstring& value) {
  if (!IsSafeQuotedScalar(value)) return false;
  const int bytes = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, value.data() + 1,
      static_cast<int>(value.size() - 2), nullptr, 0, nullptr, nullptr);
  return bytes >= 0 && bytes <= 510;
}

bool SetWide(WCHAR* destination, size_t capacity, const std::wstring& value) {
  return wcscpy_s(destination, capacity, value.c_str()) == 0;
}

bool IsPathWithin(const std::wstring& path, const std::wstring& directory) {
  if (path.size() <= directory.size()) return false;
  if (_wcsnicmp(path.c_str(), directory.c_str(), directory.size()) != 0) {
    return false;
  }
  return path[directory.size()] == L'\\';
}

bool GetProcessImagePath(DWORD processId, std::wstring* imagePath) {
  HANDLE process =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
  if (process == nullptr) return false;
  std::array<wchar_t, 32768> buffer{};
  DWORD size = static_cast<DWORD>(buffer.size());
  const BOOL success =
      QueryFullProcessImageNameW(process, 0, buffer.data(), &size);
  CloseHandle(process);
  if (!success) return false;
  imagePath->assign(buffer.data(), size);
  return true;
}

std::wstring CurrentImagePath() {
  std::array<wchar_t, MAX_PATH> buffer{};
  const DWORD size =
      GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (size == 0 || size >= buffer.size()) return L"";
  return std::wstring(buffer.data(), size);
}

bool ValidateParentProcess(DWORD processId,
                           const AppPaths& paths,
                           HANDLE* process,
                           std::wstring* error) {
  HANDLE parent =
      OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                  processId);
  if (parent == nullptr) {
    if (error) *error = L"LanSocks 主界面已经关闭，取消隧道启动。";
    return false;
  }
  std::wstring parentPath;
  if (!GetProcessImagePath(processId, &parentPath)) {
    CloseHandle(parent);
    if (error) *error = L"无法确认 LanSocks 主界面进程。";
    return false;
  }
  const std::wstring expected =
      (std::filesystem::path(paths.dataDirectory).parent_path() /
       kRunnerName)
          .wstring();
  if (_wcsicmp(parentPath.c_str(), expected.c_str()) != 0) {
    CloseHandle(parent);
    if (error) *error = L"网络辅助程序只接受 LanSocks 主程序的请求。";
    return false;
  }
  *process = parent;
  return true;
}

bool ConfigField(const std::map<std::wstring, std::wstring>& section,
                 const wchar_t* key,
                 const std::wstring& expected) {
  const auto it = section.find(key);
  return it != section.end() && it->second == expected;
}

bool ParseSingleQuoted(const std::wstring& value, std::wstring* result) {
  if (!IsSafeQuotedScalar(value)) return false;
  result->clear();
  for (size_t i = 1; i + 1 < value.size(); ++i) {
    if (value[i] == L'\'' && value[i + 1] == L'\'') ++i;
    result->push_back(value[i]);
  }
  return true;
}

bool SaveSessionAtomically(const std::wstring& path,
                           const SessionState& state) {
  const std::wstring temporary =
      path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const BOOL wrote = WriteFile(file, &state, sizeof(state), &written, nullptr);
  const BOOL flushed = wrote && written == sizeof(state) && FlushFileBuffers(file);
  CloseHandle(file);
  if (!flushed) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  if (!MoveFileExW(temporary.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  return true;
}

bool RemoveSessionAtomically(const std::wstring& path) {
  if (DeleteFileW(path.c_str())) return true;
  return GetLastError() == ERROR_FILE_NOT_FOUND;
}

bool MakeRouteOwnedAndAdd(SessionState* state,
                          const AppPaths& paths,
                          RouteRecord route,
                          std::wstring* error) {
  if (state->routeCount >= kMaxRoutes) {
    if (error) *error = L"DNS 服务器过多，无法安全记录本次路由。";
    return false;
  }
  const bool existed = RouteExists(route);
  route.owned = existed ? 0 : 1;
  const DWORD index = state->routeCount++;
  state->routes[index] = route;
  if (!SaveSession(paths, *state)) {
    if (error) *error = L"无法写入网络恢复记录。";
    return false;
  }

  bool owned = false;
  if (!AddRoute(state->routes[index], &owned, error)) return false;
  state->routes[index].owned = owned ? 1 : 0;
  if (!SaveSession(paths, *state)) {
    if (error) *error = L"路由已创建，但无法保存恢复记录。";
    return false;
  }
  return true;
}

bool StopProcessIfExpected(DWORD processId, const std::wstring& expectedPath) {
  if (processId == 0) return true;
  HANDLE process = OpenProcess(
      PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
      FALSE, processId);
  if (process == nullptr) return GetLastError() == ERROR_INVALID_PARAMETER;

  std::array<wchar_t, 32768> buffer{};
  DWORD size = static_cast<DWORD>(buffer.size());
  const BOOL pathRead =
      QueryFullProcessImageNameW(process, 0, buffer.data(), &size);
  if (!pathRead || _wcsicmp(buffer.data(), expectedPath.c_str()) != 0) {
    CloseHandle(process);
    return false;
  }

  if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0) {
    if (!TerminateProcess(process, 1)) {
      const DWORD error = GetLastError();
      if (error != ERROR_ACCESS_DENIED) {
        CloseHandle(process);
        return false;
      }
    }
    WaitForSingleObject(process, 5000);
  }
  CloseHandle(process);
  return true;
}

bool CleanupSession(const AppPaths& paths,
                    const SessionState& state,
                    std::wstring* error) {
  bool routesRestored = true;
  std::wstring message;
  for (DWORD i = state.routeCount; i > 0; --i) {
    std::wstring routeError;
    if (!DeleteRoute(state.routes[i - 1], &routeError)) {
      routesRestored = false;
      message = routeError;
    }
  }
  if (!routesRestored) {
    if (error) *error = message;
    WriteLastError(paths, message);
    return false;
  }

  if (!StopProcessIfExpected(state.hevPid, state.hevPath)) {
    message = L"无法安全停止已记录的 HEV 进程。";
    if (error) *error = message;
    WriteLastError(paths, message);
    return false;
  }

  std::wstring dnsError;
  if (!RestoreDnsSnapshot(paths, &dnsError)) {
    message = L"DNS 配置恢复失败：" + dnsError;
    if (error) *error = message;
    WriteLastError(paths, message);
    return false;
  }

  DeleteFileW(state.configPath);
  if (!RemoveDnsSnapshot(paths)) {
    message = L"网络已恢复，但无法删除 DNS 恢复快照。";
    if (error) *error = message;
    WriteLastError(paths, message);
    return false;
  }
  DeleteFileW(paths.errorPath.c_str());
  if (!RemoveSession(paths)) {
    if (error) *error = L"网络已恢复，但无法删除网络恢复记录。";
    return false;
  }
  return true;
}

bool IsUnicastIpv4(const IN_ADDR& address) {
  const ULONG host = ntohl(address.S_un.S_addr);
  const ULONG first = host >> 24;
  return host != 0 && host != 0xffffffff && first != 127 && first < 224;
}

void AddRouteOrThrow(SessionState* state,
                     const AppPaths& paths,
                     const RouteRecord& route) {
  std::wstring error;
  if (!MakeRouteOwnedAndAdd(state, paths, route, &error)) {
    throw std::runtime_error(WideToUtf8(error));
  }
}

bool NetworkPathStillUsable(const AppPaths& paths,
                            const SessionState& state,
                            std::wstring* error) {
  MIB_IF_ROW2 adapter{};
  adapter.InterfaceIndex = state.physicalIndex;
  if (GetIfEntry2(&adapter) != NO_ERROR ||
      adapter.OperStatus != IfOperStatusUp) {
    return false;
  }
  IN_ADDR currentGateway{};
  std::wstring ignored;
  if (!ResolveDefaultGateway(state.physicalIndex, &currentGateway, &ignored) ||
      currentGateway.S_un.S_addr != state.physicalGateway.S_un.S_addr) {
    return false;
  }
  NET_IFINDEX currentIndex = 0;
  IN_ADDR currentHop{};
  if (!ResolvePhysicalRoute(state.socksAddress, &currentIndex, &currentHop,
                            &ignored)) {
    return false;
  }
  if (currentIndex != state.physicalIndex ||
      currentHop.S_un.S_addr !=
          state.routes[0].nextHop.Ipv4.sin_addr.S_un.S_addr) {
    if (error) *error = L"SOCKS5 物理网卡或网关发生变化。";
    return false;
  }

  IN_ADDR mapDnsAddress{};
  IN_ADDR mappedNetwork{};
  InetPtonW(AF_INET, kMapDnsAddress, &mapDnsAddress);
  InetPtonW(AF_INET, kMapDnsNetwork, &mappedNetwork);
  if (!BestRouteUsesInterface(state.socksAddress, state.physicalIndex) ||
      !BestRouteUsesInterface(mapDnsAddress, state.tunnelIndex) ||
      !BestRouteUsesInterface(mappedNetwork, state.tunnelIndex)) {
    if (error) *error = L"MapDNS、映射地址网段或 SOCKS5 绕行路由失效。";
    return false;
  }

  bool dnsChanged = false;
  if (!EnsureMapDnsSettings(paths, &dnsChanged, error) ||
      !VerifyMapDnsSettings(paths, error)) {
    return false;
  }
  return !dnsChanged || VerifyMapDnsQuery(error);
}

void MonitorUntilStop(const AppPaths& paths,
                      const SessionState& state,
                      HANDLE parent,
                      HANDLE hev,
                      HANDLE stopEvent) {
  HANDLE handles[] = {parent, hev, stopEvent};
  bool parentClosed = false;
  bool hevExited = false;
  bool requested = false;
  std::wstring healthError;
  for (;;) {
    const DWORD result = WaitForMultipleObjects(3, handles, FALSE, 2000);
    if (result == WAIT_OBJECT_0) {
      parentClosed = true;
      break;
    }
    if (result == WAIT_OBJECT_0 + 1) {
      hevExited = true;
      break;
    }
    if (result == WAIT_OBJECT_0 + 2) {
      requested = true;
      break;
    }
    if (result != WAIT_TIMEOUT ||
        !NetworkPathStillUsable(paths, state, &healthError)) {
      break;
    }
  }

  if (!healthError.empty()) {
    AppendHelperLog(paths, L"DNS 或路由健康检查失败：" + healthError);
  }
  if (parentClosed) {
    AppendHelperLog(paths, L"LanSocks 界面已关闭，正在清理隧道路由。");
  } else if (hevExited) {
    AppendHelperLog(paths, L"HEV 进程已退出，正在清理隧道路由。");
  } else if (requested) {
    AppendHelperLog(paths, L"收到断开请求，正在恢复原始路由。");
  } else {
    AppendHelperLog(paths, L"物理网络接口或网关发生变化，正在清理隧道路由。");
  }

  std::wstring error;
  if (!CleanupSession(paths, state, &error)) {
    AppendHelperLog(paths, L"网络恢复失败：" + error);
  } else {
    AppendHelperLog(paths, L"隧道路由已清理，网络恢复完成。");
  }
}

bool StartHev(SessionState* state,
              const AppPaths& paths,
              HANDLE* process,
              std::wstring* error) {
  HANDLE logFile = CreateFileW(
      paths.logPath.c_str(), FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (logFile == INVALID_HANDLE_VALUE) {
    if (error) *error = L"无法打开 HEV 日志文件。";
    return false;
  }

  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  HANDLE inheritableLog = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), logFile, GetCurrentProcess(),
                       &inheritableLog, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
    CloseHandle(logFile);
    if (error) *error = L"无法准备 HEV 日志句柄。";
    return false;
  }
  CloseHandle(logFile);

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = inheritableLog;
  startup.hStdError = inheritableLog;
  PROCESS_INFORMATION processInfo{};
  std::wstring commandLine =
      L"\"" + std::wstring(state->hevPath) + L"\" \"" +
      std::wstring(state->configPath) + L"\"";
  const BOOL created = CreateProcessW(
      state->hevPath, commandLine.data(), nullptr, nullptr, TRUE,
      CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, paths.dataDirectory.c_str(),
      &startup, &processInfo);
  CloseHandle(inheritableLog);
  if (!created) {
    if (error) {
      *error = L"HEV 启动失败（Windows 错误 " +
               std::to_wstring(GetLastError()) + L"）。";
    }
    return false;
  }

  state->hevPid = processInfo.dwProcessId;
  if (!SaveSession(paths, *state)) {
    TerminateProcess(processInfo.hProcess, 1);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    if (error) *error = L"无法记录 HEV 进程，已取消启动。";
    return false;
  }

  if (ResumeThread(processInfo.hThread) == static_cast<DWORD>(-1)) {
    TerminateProcess(processInfo.hProcess, 1);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    if (error) *error = L"无法启动 HEV 进程。";
    return false;
  }
  CloseHandle(processInfo.hThread);
  *process = processInfo.hProcess;
  return true;
}

bool AcquireMutex(HANDLE* mutex, std::wstring* error) {
  *mutex = CreateMutexW(nullptr, FALSE, kSessionMutexName);
  if (*mutex == nullptr) {
    if (error) *error = L"无法锁定 LanSocks 网络恢复记录。";
    return false;
  }
  const DWORD wait = WaitForSingleObject(*mutex, 0);
  if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
    CloseHandle(*mutex);
    *mutex = nullptr;
    if (error) *error = L"LanSocks 隧道操作正在进行。";
    return false;
  }
  return true;
}

bool SignalRunningHelper(const AppPaths& paths, std::wstring* error) {
  SessionState state{};
  const bool stateExists =
      GetFileAttributesW(paths.statePath.c_str()) != INVALID_FILE_ATTRIBUTES;
  if (!stateExists) {
    if (!HasDnsSnapshot(paths)) return true;
    HANDLE mutex = nullptr;
    if (!AcquireMutex(&mutex, error)) return false;
    std::wstring restoreError;
    const bool restored = RestoreDnsSnapshot(paths, &restoreError);
    const bool removed = restored && RemoveDnsSnapshot(paths);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    if (!restored || !removed) {
      if (error) {
        *error = restored ? L"无法删除 DNS 恢复快照。" : restoreError;
      }
      return false;
    }
    return true;
  }
  if (!ReadSession(paths, &state)) {
    if (error) *error = L"The network recovery record is unreadable.";
    return false;
  }

  if (IsProcessAlive(state.helperPid)) {
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, kStopEventName);
    if (event != nullptr) {
      const BOOL signaled = SetEvent(event);
      CloseHandle(event);
      if (!signaled) {
        if (error) *error = L"Unable to signal the running route helper.";
        return false;
      }
      const DWORD start = GetTickCount();
      while (GetTickCount() - start < 30000) {
        const bool stillPresent =
            GetFileAttributesW(paths.statePath.c_str()) !=
            INVALID_FILE_ATTRIBUTES;
        const bool helperAlive = IsProcessAlive(state.helperPid);
        if (!stillPresent && !helperAlive) return true;
        if (stillPresent && !helperAlive) break;
        Sleep(150);
      }
      if (IsProcessAlive(state.helperPid)) {
        if (error) *error = L"Timed out while restoring the previous routes.";
        return false;
      }
    }
  }

  HANDLE mutex = nullptr;
  if (!AcquireMutex(&mutex, error)) return false;
  SessionState current{};
  const bool currentExists =
      GetFileAttributesW(paths.statePath.c_str()) != INVALID_FILE_ATTRIBUTES;
  if (!currentExists) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return true;
  }
  if (!ReadSession(paths, &current)) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    if (error) *error = L"The network recovery record is unreadable.";
    return false;
  }
  const bool cleaned = CleanupSession(paths, current, error);
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return cleaned;
}

bool ParseAndValidateConfig(const std::wstring& path,
                            const IN_ADDR& socksAddress,
                            USHORT socksPort,
                            std::wstring* error) {
  std::string bytes;
  if (!ReadFileBytes(path, &bytes) || bytes.empty() || bytes.size() > 64 * 1024) {
    if (error) *error = L"HEV 配置文件缺失或大小无效。";
    return false;
  }
  const std::wstring content = Utf8ToWide(bytes);
  if (content.empty()) {
    if (error) *error = L"HEV 配置文件不是有效的 UTF-8。";
    return false;
  }

  std::map<std::wstring, std::wstring> tunnel;
  std::map<std::wstring, std::wstring> socks5;
  std::map<std::wstring, std::wstring> mapdns;
  std::map<std::wstring, std::wstring> misc;
  std::map<std::wstring, std::wstring>* current = nullptr;
  int section = 0;
  std::wistringstream stream(content);
  std::wstring line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == L'\r') line.pop_back();
    if (line.empty()) continue;
    if (line == L"tunnel:" && section == 0) {
      current = &tunnel;
      section = 1;
      continue;
    }
    if (line == L"socks5:" && section == 1) {
      current = &socks5;
      section = 2;
      continue;
    }
    if (line == L"mapdns:" && section == 2) {
      current = &mapdns;
      section = 3;
      continue;
    }
    if (line == L"misc:" && section == 3) {
      current = &misc;
      section = 4;
      continue;
    }
    if (current == nullptr || line.size() < 4 ||
        line[0] != L' ' || line[1] != L' ' || line[2] == L' ') {
      if (error) *error = L"HEV 配置包含不允许的 YAML 结构。";
      return false;
    }
    const size_t separator = line.find(L": ", 2);
    if (separator == std::wstring::npos) {
      if (error) *error = L"HEV 配置字段格式无效。";
      return false;
    }
    const std::wstring key = line.substr(2, separator - 2);
    const std::wstring value = line.substr(separator + 2);
    if (!current->emplace(key, value).second) {
      if (error) *error = L"HEV 配置字段重复。";
      return false;
    }
  }

  if (section != 4 || tunnel.size() != 6 ||
      (socks5.size() != 3 && socks5.size() != 5) || mapdns.size() != 5 ||
      misc.size() != 2) {
    if (error) *error = L"HEV 配置字段不完整或含有未知字段。";
    return false;
  }
  if (!ConfigField(tunnel, L"name", kTunnelName) ||
      !ConfigField(tunnel, L"guid", kTunnelGuid) ||
      !ConfigField(tunnel, L"multi-queue", L"false") ||
      !ConfigField(tunnel, L"ipv4", kExpectedIpv4) ||
      !ConfigField(tunnel, L"icmp", L"'off'")) {
    if (error) *error = L"HEV TUN 配置不符合 LanSocks 的固定参数。";
    return false;
  }
  ULONG mtu = 0;
  if (!ParseUnsigned(tunnel[L"mtu"], 9000, &mtu) || mtu < 576) {
    if (error) *error = L"HEV TUN MTU 超出允许范围。";
    return false;
  }

  IN_ADDR configuredAddress{};
  ULONG configuredPort = 0;
  if (!ParseIpv4(socks5[L"address"], &configuredAddress) ||
      configuredAddress.S_un.S_addr != socksAddress.S_un.S_addr ||
      !ParseUnsigned(socks5[L"port"], 65535, &configuredPort) ||
      configuredPort != socksPort) {
    if (error) *error = L"HEV SOCKS5 地址或端口与本次请求不匹配。";
    return false;
  }
  std::wstring udp;
  if (!ParseSingleQuoted(socks5[L"udp"], &udp) ||
      (udp != L"udp" && udp != L"tcp")) {
    if (error) *error = L"HEV UDP 模式无效。";
    return false;
  }
  ULONG mapDnsPort = 0;
  ULONG mapDnsCacheSize = 0;
  IN_ADDR mapDnsAddress{};
  IN_ADDR mapDnsNetwork{};
  IN_ADDR mapDnsNetmask{};
  IN_ADDR expectedMapDns{};
  IN_ADDR expectedMapNetwork{};
  IN_ADDR expectedMapNetmask{};
  InetPtonW(AF_INET, kMapDnsAddress, &expectedMapDns);
  InetPtonW(AF_INET, kMapDnsNetwork, &expectedMapNetwork);
  InetPtonW(AF_INET, kMapDnsNetmask, &expectedMapNetmask);
  if (!ParseIpv4(mapdns[L"address"], &mapDnsAddress) ||
      !ParseIpv4(mapdns[L"network"], &mapDnsNetwork) ||
      !ParseIpv4(mapdns[L"netmask"], &mapDnsNetmask) ||
      !ParseUnsigned(mapdns[L"port"], 65535, &mapDnsPort) ||
      !ParseUnsigned(mapdns[L"cache-size"], 1000000, &mapDnsCacheSize) ||
      mapDnsAddress.S_un.S_addr != expectedMapDns.S_un.S_addr ||
      mapDnsNetwork.S_un.S_addr != expectedMapNetwork.S_un.S_addr ||
      mapDnsNetmask.S_un.S_addr != expectedMapNetmask.S_un.S_addr ||
      mapDnsPort != 53 || mapDnsCacheSize != 10000) {
    if (error) *error = L"HEV MapDNS 必须使用固定的 TUN DNS 与映射网段。";
    return false;
  }
  const bool hasUsername = socks5.find(L"username") != socks5.end();
  const bool hasPassword = socks5.find(L"password") != socks5.end();
  if (hasUsername != hasPassword) {
    if (error) *error = L"HEV 用户名和密码必须同时存在。";
    return false;
  }
  if (hasUsername &&
      (!IsValidSecret(socks5[L"username"]) ||
       !IsValidSecret(socks5[L"password"]))) {
    if (error) *error = L"HEV SOCKS5 认证字段无效。";
    return false;
  }
  if (!ConfigField(misc, L"log-file", L"stdout") ||
      (misc[L"log-level"] != L"debug" &&
       misc[L"log-level"] != L"info" &&
       misc[L"log-level"] != L"warn" &&
       misc[L"log-level"] != L"error")) {
    if (error) *error = L"HEV 日志配置无效。";
    return false;
  }
  return true;
}

bool IsConfigPathAllowed(const std::wstring& path, const AppPaths& paths) {
  std::array<wchar_t, MAX_PATH> full{};
  const DWORD length = GetFullPathNameW(path.c_str(), MAX_PATH, full.data(), nullptr);
  if (length == 0 || length >= MAX_PATH) return false;
  return _wcsicmp(full.data(), paths.configPath.c_str()) == 0 &&
         IsPathWithin(full.data(), paths.runtimeDirectory);
}

void FillStatePaths(SessionState* state, const AppPaths& paths) {
  state->magic = kStateMagic;
  state->version = kStateVersion;
  state->phase = SessionPhase::Starting;
  SetWide(state->configPath, MAX_PATH, paths.configPath);
  SetWide(state->hevPath, MAX_PATH, paths.hevPath);
  SetWide(state->logPath, MAX_PATH, paths.logPath);
  SetWide(state->statePath, MAX_PATH, paths.statePath);
}

bool StartTunnel(const AppPaths& paths,
                 const std::wstring& requestedConfig,
                 const std::wstring& addressText,
                 USHORT port,
                 DWORD parentPid,
                 HANDLE stopEvent,
                 std::wstring* error) {
  if (!IsElevated()) {
    if (error) *error = L"网络配置需要管理员权限；UAC 提权没有生效。";
    return false;
  }
  if (!IsConfigPathAllowed(requestedConfig, paths)) {
    if (error) *error = L"只允许使用 LanSocks 运行目录中的 HEV 配置。";
    return false;
  }
  IN_ADDR socksAddress{};
  if (!ParseIpv4(addressText, &socksAddress) ||
      !IsUnicastIpv4(socksAddress) || port == 0) {
    if (error) *error = L"SOCKS5 地址或端口无效。";
    return false;
  }
  if (!ParseAndValidateConfig(paths.configPath, socksAddress, port, error)) {
    return false;
  }
  IN_ADDR mapDnsEndpoint{};
  InetPtonW(AF_INET, kMapDnsAddress, &mapDnsEndpoint);
  const ULONG socksHostAddress = ntohl(socksAddress.S_un.S_addr);
  if (socksAddress.S_un.S_addr == mapDnsEndpoint.S_un.S_addr ||
      (socksHostAddress & 0xffc00000UL) == 0x64400000UL) {
    if (error) {
      *error = L"SOCKS5 地址与 HEV MapDNS 地址或映射网段冲突，无法同时保证两条路由。";
    }
    return false;
  }

  HANDLE parent = nullptr;
  if (!ValidateParentProcess(parentPid, paths, &parent, error)) return false;
  if (GetFileAttributesW(paths.hevPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
    CloseHandle(parent);
    if (error) *error = L"未找到 HEV 可执行文件，请先运行 Windows 构建脚本。";
    return false;
  }
  if (GetFileAttributesW(
          (std::filesystem::path(paths.dataDirectory) / L"wintun.dll")
              .c_str()) == INVALID_FILE_ATTRIBUTES) {
    CloseHandle(parent);
    if (error) *error = L"未找到官方 Wintun DLL，请先运行 Windows 构建脚本。";
    return false;
  }

  SessionState state{};
  FillStatePaths(&state, paths);
  state.parentPid = parentPid;
  state.helperPid = GetCurrentProcessId();
  state.socksAddress = socksAddress;
  state.socksPort = port;

  IN_ADDR physicalNextHop{};
  if (!ResolvePhysicalRoute(socksAddress, &state.physicalIndex,
                            &physicalNextHop, error)) {
    CloseHandle(parent);
    return false;
  }
  if (!ResolveDefaultGateway(state.physicalIndex, &state.physicalGateway,
                             error)) {
    CloseHandle(parent);
    return false;
  }
  state.routes[0] = MakeIpv4Route(socksAddress, 32, physicalNextHop,
                                  state.physicalIndex, kBypassRouteMetric);
  state.routeCount = 0;
  if (!CaptureDnsSnapshot(paths, error)) {
    CloseHandle(parent);
    return false;
  }
  if (!SaveSession(paths, state)) {
    RemoveDnsSnapshot(paths);
    CloseHandle(parent);
    if (error) *error = L"无法创建网络恢复记录。";
    return false;
  }

  try {
    AddRouteOrThrow(&state, paths, state.routes[0]);
    HANDLE hevProcess = nullptr;
    if (!StartHev(&state, paths, &hevProcess, error)) {
      throw std::runtime_error("HEV process start failed");
    }

    NET_IFINDEX tunnelIndex = 0;
    if (!WaitForTunnelInterface(&tunnelIndex, hevProcess, 20000)) {
      DWORD hevExitCode = STILL_ACTIVE;
      const bool exitCodeAvailable =
          GetExitCodeProcess(hevProcess, &hevExitCode) != FALSE;
      const bool hevExited =
          exitCodeAvailable && hevExitCode != STILL_ACTIVE;
      if (error) {
        if (hevExited) {
          *error = L"HEV 在创建 Wintun 网卡前退出，退出码 " +
                   std::to_wstring(hevExitCode) + L"。";
        } else if (exitCodeAvailable) {
          *error = L"HEV 仍在运行，但 20 秒内没有发现预期的 Wintun 网卡（GUID " +
                   std::wstring(kTunnelGuid) + L"）。";
        } else {
          *error = L"无法读取 HEV 进程状态，且没有发现预期的 Wintun 网卡（GUID " +
                   std::wstring(kTunnelGuid) + L"）。";
        }
        const std::wstring logTail = ReadHevLogTail(paths.logPath);
        if (!logTail.empty()) {
          *error += L"\nHEV 启动日志尾部：\n" + logTail;
        }
      }
      if (!hevExited) TerminateProcess(hevProcess, 1);
      WaitForSingleObject(hevProcess, 5000);
      CloseHandle(hevProcess);
      throw std::runtime_error("Wintun interface was not ready");
    }
    state.tunnelIndex = tunnelIndex;

    IN_ADDR zero{};
    IN_ADDR upperHalf{};
    IN_ADDR mapDnsAddress{};
    IN_ADDR mappedNetwork{};
    upperHalf.S_un.S_addr = htonl(0x80000000UL);
    InetPtonW(AF_INET, kMapDnsAddress, &mapDnsAddress);
    InetPtonW(AF_INET, kMapDnsNetwork, &mappedNetwork);
    AddRouteOrThrow(
        &state, paths,
        MakeIpv4Route(zero, 1, zero, state.tunnelIndex, kTunnelRouteMetric));
    AddRouteOrThrow(
        &state, paths,
        MakeIpv4Route(upperHalf, 1, zero, state.tunnelIndex,
                      kTunnelRouteMetric));
    AddRouteOrThrow(
        &state, paths,
        MakeIpv4Route(mapDnsAddress, 32, zero, state.tunnelIndex,
                      kTunnelRouteMetric));
    AddRouteOrThrow(
        &state, paths,
        MakeIpv4Route(mappedNetwork, 10, zero, state.tunnelIndex,
                      kTunnelRouteMetric));

    if (!BestRouteUsesInterface(socksAddress, state.physicalIndex)) {
      if (error) *error = L"SOCKS5 绕行路由未生效，已回滚以避免 TUN 回环。";
      throw std::runtime_error("SOCKS5 bypass route verification failed");
    }
    IN_ADDR lowerProbe{};
    IN_ADDR upperProbe{};
    InetPtonW(AF_INET, L"1.1.1.1", &lowerProbe);
    InetPtonW(AF_INET, L"129.1.1.1", &upperProbe);
    if (!BestRouteUsesInterface(lowerProbe, state.tunnelIndex) ||
        !BestRouteUsesInterface(upperProbe, state.tunnelIndex)) {
      if (error) {
        *error = L"默认 IPv4 流量没有全部选择 HEV TUN 路由；已回滚。";
      }
      throw std::runtime_error("default TUN route verification failed");
    }
    if (!BestRouteUsesInterface(mapDnsAddress, state.tunnelIndex) ||
        !BestRouteUsesInterface(mappedNetwork, state.tunnelIndex)) {
      if (error) *error = L"MapDNS 或映射地址路由未通过 HEV TUN。";
      throw std::runtime_error("MapDNS TUN route verification failed");
    }

    bool dnsChanged = false;
    if (!EnsureMapDnsSettings(paths, &dnsChanged, error) ||
        !VerifyMapDnsSettings(paths, error) || !VerifyMapDnsQuery(error)) {
      throw std::runtime_error("Windows DNS MapDNS verification failed");
    }

    state.phase = SessionPhase::Active;
    if (!SaveSession(paths, state)) {
      CloseHandle(hevProcess);
      throw std::runtime_error("Could not persist active session state");
    }
    DeleteFileW(paths.errorPath.c_str());
    std::wstring startupMessage =
        L"HEV TUN 路由、SOCKS5 物理绕行、活动网卡 DNS 与 MapDNS 查询均已验证。";
    if (dnsChanged) startupMessage += L" Windows DNS 已切换到 MapDNS。";
    AppendHelperLog(paths, startupMessage);

    MonitorUntilStop(paths, state, parent, hevProcess, stopEvent);
    CloseHandle(hevProcess);
    CloseHandle(parent);
    return true;
  } catch (const std::exception& exception) {
    SessionState current{};
    if (ReadSession(paths, &current)) {
      std::wstring cleanupError;
      CleanupSession(paths, current, &cleanupError);
    }
    CloseHandle(parent);
    if (error && error->empty()) {
      *error = Utf8ToWide(exception.what());
    }
    if (error && error->empty()) {
      *error = ReadLastError(paths);
      if (error->empty()) *error = L"HEV 或 Windows 路由配置失败，已执行回滚。";
    }
    return false;
  }
}

}  // namespace

std::wstring Utf8ToWide(const std::string& text) {
  if (text.empty()) return L"";
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                       text.data(), static_cast<int>(text.size()),
                                       nullptr, 0);
  if (size <= 0) return L"";
  std::wstring value(size, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                      static_cast<int>(text.size()), value.data(), size);
  return value;
}

std::string WideToUtf8(const std::wstring& text) {
  if (text.empty()) return "";
  const int size = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      nullptr, 0, nullptr, nullptr);
  if (size <= 0) return "";
  std::string value(size, '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                      static_cast<int>(text.size()), value.data(), size, nullptr,
                      nullptr);
  return value;
}

bool GetAppPaths(AppPaths* paths, std::wstring* error) {
  std::array<wchar_t, MAX_PATH> local{};
  const DWORD localLength =
      GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), MAX_PATH);
  if (localLength == 0 || localLength >= MAX_PATH) {
    if (error) *error = L"无法确定当前用户的本地应用数据目录。";
    return false;
  }

  std::array<wchar_t, MAX_PATH> module{};
  const DWORD moduleLength =
      GetModuleFileNameW(nullptr, module.data(), MAX_PATH);
  if (moduleLength == 0 || moduleLength >= MAX_PATH) {
    if (error) *error = L"无法确定网络辅助程序所在目录。";
    return false;
  }

  const std::filesystem::path localPath(local.data());
  const std::filesystem::path helperPath(module.data());
  const std::filesystem::path dataPath = helperPath.parent_path();
  paths->localAppData = localPath.wstring();
  paths->runtimeDirectory =
      (localPath / L"LanSocks" / L"runtime").wstring();
  paths->recoveryDirectory =
      (localPath / L"LanSocks" / L"recovery").wstring();
  paths->logDirectory = (localPath / L"LanSocks" / L"logs").wstring();
  paths->configPath = (localPath / L"LanSocks" / L"runtime" / L"hev.yml").wstring();
  paths->statePath =
      (localPath / L"LanSocks" / L"recovery" / L"session.bin").wstring();
  paths->dnsStatePath =
      (localPath / L"LanSocks" / L"recovery" / L"dns.bin").wstring();
  paths->errorPath =
      (localPath / L"LanSocks" / L"recovery" / L"last-error.txt").wstring();
  paths->logPath = (localPath / L"LanSocks" / L"logs" / L"hev.log").wstring();
  paths->dataDirectory = dataPath.wstring();
  paths->helperPath = helperPath.wstring();
  paths->hevPath = (dataPath / L"hev-socks5-tunnel.exe").wstring();

  std::error_code filesystemError;
  std::filesystem::create_directories(paths->runtimeDirectory, filesystemError);
  if (filesystemError) {
    if (error) *error = L"无法创建 LanSocks 运行目录。";
    return false;
  }
  std::filesystem::create_directories(paths->recoveryDirectory, filesystemError);
  if (filesystemError) {
    if (error) *error = L"无法创建网络恢复记录目录。";
    return false;
  }
  std::filesystem::create_directories(paths->logDirectory, filesystemError);
  if (filesystemError) {
    if (error) *error = L"无法创建 HEV 日志目录。";
    return false;
  }
  return true;
}

bool ReadSession(const AppPaths& paths, SessionState* state) {
  HANDLE file = CreateFileW(paths.statePath.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD read = 0;
  const BOOL success = ReadFile(file, state, sizeof(*state), &read, nullptr);
  LARGE_INTEGER size{};
  const BOOL gotSize = GetFileSizeEx(file, &size);
  CloseHandle(file);
  return success && gotSize && read == sizeof(*state) &&
         size.QuadPart == sizeof(*state) && state->magic == kStateMagic &&
         state->version == kStateVersion && state->routeCount <= kMaxRoutes;
}

bool SaveSession(const AppPaths& paths, const SessionState& state) {
  return SaveSessionAtomically(paths.statePath, state);
}

bool RemoveSession(const AppPaths& paths) {
  return RemoveSessionAtomically(paths.statePath);
}

bool IsProcessAlive(DWORD processId) {
  if (processId == 0) return false;
  HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId);
  if (process == nullptr) return false;
  const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
  CloseHandle(process);
  return alive;
}

bool IsElevated() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  TOKEN_ELEVATION elevation{};
  DWORD size = 0;
  const BOOL read = GetTokenInformation(
      token, TokenElevation, &elevation, sizeof(elevation), &size);
  CloseHandle(token);
  return read && elevation.TokenIsElevated != 0;
}

void WriteLastError(const AppPaths& paths, const std::wstring& message) {
  const std::string utf8 = WideToUtf8(message);
  HANDLE file = CreateFileW(paths.errorPath.c_str(), GENERIC_WRITE,
                            FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
  FlushFileBuffers(file);
  CloseHandle(file);
}

std::wstring ReadLastError(const AppPaths& paths) {
  std::string bytes;
  if (!ReadFileBytes(paths.errorPath, &bytes)) return L"";
  return Utf8ToWide(bytes);
}

void AppendHelperLog(const AppPaths& paths, const std::wstring& message) {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  std::wstringstream formatted;
  formatted << L"[" << now.wHour << L":" << now.wMinute << L":" << now.wSecond
            << L"] " << message << L"\r\n";
  const std::string utf8 = WideToUtf8(formatted.str());
  HANDLE file = CreateFileW(paths.logPath.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
  CloseHandle(file);
}

void WriteLastError(const AppPaths& paths, const wchar_t* message) {
  WriteLastError(paths, std::wstring(message));
}

bool ValidateHevConfig(const AppPaths& paths,
                       const IN_ADDR& socksAddress,
                       USHORT socksPort,
                       std::wstring* error) {
  return ParseAndValidateConfig(paths.configPath, socksAddress, socksPort, error);
}

int RunHelper(int argc, wchar_t** argv) {
  AppPaths paths{};
  std::wstring error;
  if (!GetAppPaths(&paths, &error)) {
    return 2;
  }
  DeleteFileW(paths.errorPath.c_str());
  if (argc >= 2 && _wcsicmp(argv[1], L"--stop") == 0) {
    if (!SignalRunningHelper(paths, &error)) {
      WriteLastError(paths, error);
      return 3;
    }
    return 0;
  }
  if (argc >= 2 && _wcsicmp(argv[1], L"--recover") == 0) {
    if (!SignalRunningHelper(paths, &error)) {
      WriteLastError(paths, error);
      return 4;
    }
    return 0;
  }
  if (argc != 6 || _wcsicmp(argv[1], L"--start") != 0) {
    WriteLastError(paths, L"无效的辅助程序请求。");
    return 5;
  }

  ULONG portValue = 0;
  ULONG parentValue = 0;
  if (!ParseUnsigned(argv[4], 65535, &portValue) ||
      !ParseUnsigned(argv[5], std::numeric_limits<ULONG>::max(),
                     &parentValue)) {
    WriteLastError(paths, L"无效的端口或主程序进程号。");
    return 6;
  }

  HANDLE mutex = nullptr;
  if (!AcquireMutex(&mutex, &error)) {
    WriteLastError(paths, error);
    return 7;
  }
  SessionState oldState{};
  if (ReadSession(paths, &oldState)) {
    std::wstring cleanupError;
    if (!CleanupSession(paths, oldState, &cleanupError)) {
      ReleaseMutex(mutex);
      CloseHandle(mutex);
      WriteLastError(paths, cleanupError);
      return 8;
    }
  }
  if (HasDnsSnapshot(paths)) {
    std::wstring restoreError;
    if (!RestoreDnsSnapshot(paths, &restoreError) ||
        !RemoveDnsSnapshot(paths)) {
      ReleaseMutex(mutex);
      CloseHandle(mutex);
      WriteLastError(paths, restoreError.empty()
                                 ? L"无法删除 DNS 恢复快照。"
                                 : restoreError);
      return 8;
    }
  }
  HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, kStopEventName);
  if (stopEvent == nullptr || !ResetEvent(stopEvent)) {
    if (stopEvent != nullptr) CloseHandle(stopEvent);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    WriteLastError(paths, L"无法创建隧道停止事件。");
    return 9;
  }

  if (!StartTunnel(paths, argv[2], argv[3],
                   static_cast<USHORT>(portValue),
                   static_cast<DWORD>(parentValue), stopEvent, &error)) {
    WriteLastError(paths, error);
    CloseHandle(stopEvent);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 10;
  }
  CloseHandle(stopEvent);
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return 0;
}

}  // namespace lansocks

int wmain(int argc, wchar_t** argv) {
  return lansocks::RunHelper(argc, argv);
}
