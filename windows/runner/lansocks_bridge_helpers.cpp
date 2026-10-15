#include "../../native/windows/lansocks_native.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iphlpapi.h>
#include <ws2tcpip.h>

namespace lansocks {

bool GetAppPaths(AppPaths* paths, std::wstring* error) {
  std::array<wchar_t, MAX_PATH> local{};
  const DWORD localLength =
      GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), MAX_PATH);
  if (localLength == 0 || localLength >= MAX_PATH) {
    if (error) *error = L"Unable to resolve the current user's local app data directory.";
    return false;
  }

  std::array<wchar_t, 32768> module{};
  const DWORD moduleLength =
      GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
  if (moduleLength == 0 || moduleLength >= module.size()) {
    if (error) *error = L"Unable to resolve the LanSocks application directory.";
    return false;
  }

  const std::filesystem::path localPath(local.data());
  const std::filesystem::path applicationPath(module.data());
  const std::filesystem::path dataPath = applicationPath.parent_path() / L"data";
  paths->localAppData = localPath.wstring();
  paths->runtimeDirectory =
      (localPath / L"LanSocks" / L"runtime").wstring();
  paths->recoveryDirectory =
      (localPath / L"LanSocks" / L"recovery").wstring();
  paths->logDirectory = (localPath / L"LanSocks" / L"logs").wstring();
  paths->configPath =
      (localPath / L"LanSocks" / L"runtime" / L"hev.yml").wstring();
  paths->statePath =
      (localPath / L"LanSocks" / L"recovery" / L"session.bin").wstring();
  paths->errorPath =
      (localPath / L"LanSocks" / L"recovery" / L"last-error.txt").wstring();
  paths->logPath = (localPath / L"LanSocks" / L"logs" / L"hev.log").wstring();
  paths->dataDirectory = dataPath.wstring();
  paths->helperPath = (dataPath / L"lansocks-helper.exe").wstring();
  paths->hevPath = (dataPath / L"hev-socks5-tunnel.exe").wstring();

  std::error_code filesystemError;
  std::filesystem::create_directories(paths->runtimeDirectory, filesystemError);
  if (filesystemError) {
    if (error) *error = L"Unable to create the LanSocks runtime directory.";
    return false;
  }
  std::filesystem::create_directories(paths->recoveryDirectory, filesystemError);
  if (filesystemError) {
    if (error) *error = L"Unable to create the network recovery directory.";
    return false;
  }
  std::filesystem::create_directories(paths->logDirectory, filesystemError);
  if (filesystemError) {
    if (error) *error = L"Unable to create the HEV log directory.";
    return false;
  }
  return true;
}

bool ReadSession(const AppPaths& paths, SessionState* state) {
  HANDLE file = CreateFileW(
      paths.statePath.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
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

bool IsProcessAlive(DWORD processId) {
  if (processId == 0) return false;
  HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId);
  if (process == nullptr) return false;
  const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
  CloseHandle(process);
  return alive;
}

std::wstring ReadLastError(const AppPaths& paths) {
  std::ifstream stream(std::filesystem::path(paths.errorPath), std::ios::binary);
  if (!stream) return L"";
  const std::string bytes(std::istreambuf_iterator<char>(stream), {});
  if (bytes.empty()) return L"";
  const int size = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
      static_cast<int>(bytes.size()), nullptr, 0);
  if (size <= 0) return L"";
  std::wstring message(size, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                      static_cast<int>(bytes.size()), message.data(), size);
  return message;
}

bool ParseIpv4(const std::wstring& text, IN_ADDR* address) {
  if (text.empty() || address == nullptr) return false;
  return InetPtonW(AF_INET, text.c_str(), address) == 1;
}

bool BestRouteUsesInterface(const IN_ADDR& destination,
                            NET_IFINDEX interfaceIndex) {
  SOCKADDR_INET remote{};
  remote.Ipv4.sin_family = AF_INET;
  remote.Ipv4.sin_addr = destination;
  MIB_IPFORWARD_ROW2 route{};
  SOCKADDR_INET source{};
  if (GetBestRoute2(nullptr, 0, nullptr, &remote, 0, &route, &source) !=
      NO_ERROR) {
    return false;
  }
  return route.InterfaceIndex == interfaceIndex;
}

}  // namespace lansocks
