#include "lansocks_native.h"

#include <ipifcons.h>
#include <iphlpapi.h>
#include <objbase.h>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace lansocks {
namespace {

bool SameAddress(const SOCKADDR_INET& left,
                 const SOCKADDR_INET& right,
                 ADDRESS_FAMILY family) {
  if (family == AF_INET) {
    return left.Ipv4.sin_addr.S_un.S_addr == right.Ipv4.sin_addr.S_un.S_addr;
  }
  return false;
}

MIB_IPFORWARD_ROW2 ToIpForwardRow(const RouteRecord& route) {
  MIB_IPFORWARD_ROW2 row{};
  InitializeIpForwardEntry(&row);
  row.InterfaceIndex = route.interfaceIndex;
  row.DestinationPrefix.Prefix = route.prefix;
  row.DestinationPrefix.PrefixLength = route.prefixLength;
  row.NextHop = route.nextHop;
  row.Metric = route.metric;
  row.Protocol = MIB_IPPROTO_NETMGMT;
  row.Origin = NlroManual;
  row.Loopback = FALSE;
  row.AutoconfigureAddress = FALSE;
  row.Publish = FALSE;
  row.Immortal = FALSE;
  return row;
}

std::wstring Win32Error(const wchar_t* operation, DWORD code) {
  std::wstringstream message;
  message << operation << L"（Windows 错误 " << code << L"）。";
  return message.str();
}

}  // namespace

bool ParseIpv4(const std::wstring& text, IN_ADDR* address) {
  if (text.empty() || address == nullptr) return false;
  return InetPtonW(AF_INET, text.c_str(), address) == 1;
}

bool ResolvePhysicalRoute(const IN_ADDR& destination,
                          NET_IFINDEX* interfaceIndex,
                          IN_ADDR* nextHop,
                          std::wstring* error) {
  SOCKADDR_INET remote{};
  remote.Ipv4.sin_family = AF_INET;
  remote.Ipv4.sin_addr = destination;
  MIB_IPFORWARD_ROW2 route{};
  SOCKADDR_INET source{};
  const DWORD result =
      GetBestRoute2(nullptr, 0, nullptr, &remote, 0, &route, &source);
  if (result != NO_ERROR) {
    if (error) *error = Win32Error(L"无法找到 SOCKS5 服务的路由", result);
    return false;
  }

  MIB_IF_ROW2 adapter{};
  adapter.InterfaceIndex = route.InterfaceIndex;
  const DWORD adapterResult = GetIfEntry2(&adapter);
  if (adapterResult != NO_ERROR ||
      adapter.OperStatus != IfOperStatusUp ||
      (adapter.Type != IF_TYPE_ETHERNET_CSMACD &&
       adapter.Type != IF_TYPE_IEEE80211)) {
    if (error) {
      *error = L"SOCKS5 服务当前不经由已连接的以太网或 Wi-Fi 接口。";
    }
    return false;
  }

  *interfaceIndex = route.InterfaceIndex;
  *nextHop = route.NextHop.Ipv4.sin_addr;
  return true;
}

bool ResolveDefaultGateway(NET_IFINDEX interfaceIndex,
                           IN_ADDR* gateway,
                           std::wstring* error) {
  MIB_IF_ROW2 adapter{};
  adapter.InterfaceIndex = interfaceIndex;
  if (GetIfEntry2(&adapter) != NO_ERROR ||
      adapter.OperStatus != IfOperStatusUp) {
    if (error) *error = L"物理网络接口当前未连接。";
    return false;
  }

  PMIB_IPFORWARD_TABLE2 table = nullptr;
  const DWORD result = GetIpForwardTable2(AF_INET, &table);
  if (result != NO_ERROR || table == nullptr) {
    if (error) *error = Win32Error(L"无法读取默认网关", result);
    return false;
  }
  const MIB_IPFORWARD_ROW2* selected = nullptr;
  for (ULONG i = 0; i < table->NumEntries; ++i) {
    const auto& row = table->Table[i];
    if (row.InterfaceIndex != interfaceIndex ||
        row.DestinationPrefix.PrefixLength != 0 ||
        row.NextHop.Ipv4.sin_addr.S_un.S_addr == 0) {
      continue;
    }
    if (selected == nullptr || row.Metric < selected->Metric) {
      selected = &row;
    }
  }
  if (selected == nullptr) {
    FreeMibTable(table);
    if (error) *error = L"物理网络接口没有可用的 IPv4 默认网关。";
    return false;
  }
  *gateway = selected->NextHop.Ipv4.sin_addr;
  FreeMibTable(table);
  return true;
}

RouteRecord MakeIpv4Route(const IN_ADDR& prefix,
                          UCHAR prefixLength,
                          const IN_ADDR& nextHop,
                          NET_IFINDEX interfaceIndex,
                          ULONG metric) {
  RouteRecord route{};
  route.family = AF_INET;
  route.prefixLength = prefixLength;
  route.interfaceIndex = interfaceIndex;
  route.metric = metric;
  route.prefix.Ipv4.sin_family = AF_INET;
  route.prefix.Ipv4.sin_addr = prefix;
  route.nextHop.Ipv4.sin_family = AF_INET;
  route.nextHop.Ipv4.sin_addr = nextHop;
  return route;
}

bool RouteExists(const RouteRecord& route) {
  PMIB_IPFORWARD_TABLE2 table = nullptr;
  if (GetIpForwardTable2(route.family, &table) != NO_ERROR || table == nullptr) {
    return false;
  }

  bool exists = false;
  for (ULONG i = 0; i < table->NumEntries; ++i) {
    const auto& row = table->Table[i];
    if (row.InterfaceIndex == route.interfaceIndex &&
        row.DestinationPrefix.PrefixLength == route.prefixLength &&
        row.Metric == route.metric &&
        SameAddress(row.DestinationPrefix.Prefix, route.prefix, route.family) &&
        SameAddress(row.NextHop, route.nextHop, route.family)) {
      exists = true;
      break;
    }
  }
  FreeMibTable(table);
  return exists;
}

bool AddRoute(const RouteRecord& route, bool* owned, std::wstring* error) {
  if (RouteExists(route)) {
    *owned = false;
    return true;
  }

  MIB_IPFORWARD_ROW2 row = ToIpForwardRow(route);
  const DWORD result = CreateIpForwardEntry2(&row);
  if (result == NO_ERROR) {
    *owned = true;
    return true;
  }
  if (result == ERROR_OBJECT_ALREADY_EXISTS) {
    *owned = false;
    return true;
  }
  if (error) *error = Win32Error(L"创建路由失败", result);
  return false;
}

bool DeleteRoute(const RouteRecord& route, std::wstring* error) {
  if (!route.owned || !RouteExists(route)) return true;
  MIB_IPFORWARD_ROW2 row = ToIpForwardRow(route);
  const DWORD result = DeleteIpForwardEntry2(&row);
  if (result == NO_ERROR || result == ERROR_NOT_FOUND) return true;
  if (error) *error = Win32Error(L"删除本程序创建的路由失败", result);
  return false;
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

bool GetTunnelInterfaceIndex(NET_IFINDEX* index) {
  GUID guid{};
  const std::wstring guidText = std::wstring(L"{") + kTunnelGuid + L"}";
  if (CLSIDFromString(guidText.c_str(), &guid) != S_OK) return false;
  NET_LUID luid{};
  if (ConvertInterfaceGuidToLuid(&guid, &luid) != NO_ERROR) return false;
  return ConvertInterfaceLuidToIndex(&luid, index) == NO_ERROR;
}

bool WaitForTunnelInterface(NET_IFINDEX* index,
                            HANDLE hevProcess,
                            DWORD timeoutMilliseconds) {
  const DWORD start = GetTickCount();
  do {
    if (hevProcess != nullptr &&
        WaitForSingleObject(hevProcess, 0) == WAIT_OBJECT_0) {
      return false;
    }
    NET_IFINDEX found = 0;
    if (GetTunnelInterfaceIndex(&found)) {
      MIB_IF_ROW2 row{};
      row.InterfaceIndex = found;
      if (GetIfEntry2(&row) == NO_ERROR && row.OperStatus == IfOperStatusUp) {
        *index = found;
        return true;
      }
    }
    Sleep(250);
  } while (GetTickCount() - start < timeoutMilliseconds);
  return false;
}

}  // namespace lansocks
