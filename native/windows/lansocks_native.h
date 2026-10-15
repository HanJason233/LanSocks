#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <Windows.h>
#include <Ws2tcpip.h>
#include <iphlpapi.h>

#include <string>

namespace lansocks {

constexpr DWORD kStateMagic = 0x4c534b53;  // LSKS
constexpr DWORD kStateVersion = 1;
constexpr DWORD kMaxRoutes = 48;
constexpr wchar_t kTunnelGuid[] = L"6f06c4a2-6f95-4f25-98f0-cf4608d50b41";
constexpr wchar_t kMapDnsAddress[] = L"198.18.0.2";
constexpr wchar_t kMapDnsNetwork[] = L"100.64.0.0";
constexpr wchar_t kMapDnsNetmask[] = L"255.192.0.0";
constexpr wchar_t kMapDnsIpv6Block[] = L"::1";

enum class SessionPhase : DWORD {
  Starting = 1,
  Active = 2,
};

struct RouteRecord {
  ADDRESS_FAMILY family;
  UCHAR prefixLength;
  UCHAR owned;
  USHORT reserved;
  NET_IFINDEX interfaceIndex;
  ULONG metric;
  SOCKADDR_INET prefix;
  SOCKADDR_INET nextHop;
};

struct SessionState {
  DWORD magic;
  DWORD version;
  SessionPhase phase;
  DWORD routeCount;
  DWORD parentPid;
  DWORD helperPid;
  DWORD hevPid;
  NET_IFINDEX physicalIndex;
  NET_IFINDEX tunnelIndex;
  IN_ADDR socksAddress;
  IN_ADDR physicalGateway;
  USHORT socksPort;
  USHORT reserved;
  WCHAR configPath[MAX_PATH];
  WCHAR hevPath[MAX_PATH];
  WCHAR logPath[MAX_PATH];
  WCHAR statePath[MAX_PATH];
  RouteRecord routes[kMaxRoutes];
};

struct AppPaths {
  std::wstring localAppData;
  std::wstring runtimeDirectory;
  std::wstring recoveryDirectory;
  std::wstring logDirectory;
  std::wstring configPath;
  std::wstring statePath;
  std::wstring dnsStatePath;
  std::wstring errorPath;
  std::wstring hevPath;
  std::wstring helperPath;
  std::wstring dataDirectory;
  std::wstring logPath;
};

bool GetAppPaths(AppPaths* paths, std::wstring* error);
bool ParseIpv4(const std::wstring& text, IN_ADDR* address);
bool ResolvePhysicalRoute(const IN_ADDR& destination,
                          NET_IFINDEX* interfaceIndex,
                          IN_ADDR* nextHop,
                          std::wstring* error);
bool ResolveDefaultGateway(NET_IFINDEX interfaceIndex,
                           IN_ADDR* gateway,
                           std::wstring* error);
bool CaptureDnsSnapshot(const AppPaths& paths, std::wstring* error);
bool EnsureMapDnsSettings(const AppPaths& paths,
                          bool* changed,
                          std::wstring* error);
bool VerifyMapDnsSettings(const AppPaths& paths, std::wstring* error);
bool VerifyMapDnsQuery(std::wstring* error);
bool RestoreDnsSnapshot(const AppPaths& paths, std::wstring* error);
bool RemoveDnsSnapshot(const AppPaths& paths);
bool HasDnsSnapshot(const AppPaths& paths);
bool GetTunnelInterfaceIndex(NET_IFINDEX* index);
bool WaitForTunnelInterface(NET_IFINDEX* index,
                            HANDLE hevProcess,
                            DWORD timeoutMilliseconds);
RouteRecord MakeIpv4Route(const IN_ADDR& prefix,
                          UCHAR prefixLength,
                          const IN_ADDR& nextHop,
                          NET_IFINDEX interfaceIndex,
                          ULONG metric);
bool RouteExists(const RouteRecord& route);
bool AddRoute(const RouteRecord& route, bool* owned, std::wstring* error);
bool DeleteRoute(const RouteRecord& route, std::wstring* error);
bool BestRouteUsesInterface(const IN_ADDR& destination,
                            NET_IFINDEX interfaceIndex);
bool ReadSession(const AppPaths& paths, SessionState* state);
bool SaveSession(const AppPaths& paths, const SessionState& state);
bool RemoveSession(const AppPaths& paths);
bool IsProcessAlive(DWORD processId);
bool IsElevated();
void WriteLastError(const AppPaths& paths, const std::wstring& message);
std::wstring ReadLastError(const AppPaths& paths);
void AppendHelperLog(const AppPaths& paths, const std::wstring& message);
std::wstring Utf8ToWide(const std::string& text);
std::string WideToUtf8(const std::wstring& text);
bool ValidateHevConfig(const AppPaths& paths,
                       const IN_ADDR& socksAddress,
                       USHORT socksPort,
                       std::wstring* error);
int RunHelper(int argc, wchar_t** argv);

}  // namespace lansocks
