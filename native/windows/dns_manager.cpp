#include "lansocks_native.h"

#include <iphlpapi.h>

#include <algorithm>
#include <vector>

namespace lansocks {

bool EnumerateIpv4DnsServers(std::vector<IN_ADDR>* servers,
                             std::wstring* error) {
  ULONG size = 16 * 1024;
  std::vector<BYTE> storage(size);
  auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
  ULONG result = GetAdaptersAddresses(
      AF_UNSPEC,
      GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
          GAA_FLAG_SKIP_UNICAST,
      nullptr,
      adapters,
      &size);
  if (result == ERROR_BUFFER_OVERFLOW) {
    storage.resize(size);
    adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    result = GetAdaptersAddresses(
        AF_UNSPEC,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
            GAA_FLAG_SKIP_UNICAST,
        nullptr,
        adapters,
        &size);
  }
  if (result != NO_ERROR) {
    if (error) {
      *error = L"无法读取 Windows IPv4 DNS 服务器配置（Windows 错误 " +
               std::to_wstring(result) + L"）。";
    }
    return false;
  }

  servers->clear();
  bool loopbackDns = false;
  for (auto* adapter = adapters; adapter != nullptr;
       adapter = adapter->Next) {
    if (adapter->OperStatus != IfOperStatusUp) continue;
    for (auto* dns = adapter->FirstDnsServerAddress; dns != nullptr;
         dns = dns->Next) {
      if (dns->Address.lpSockaddr == nullptr ||
          dns->Address.lpSockaddr->sa_family != AF_INET) {
        continue;
      }
      const auto* address =
          reinterpret_cast<const sockaddr_in*>(dns->Address.lpSockaddr);
      const ULONG hostAddress = ntohl(address->sin_addr.S_un.S_addr);
      if ((hostAddress >> 24) == 127) {
        loopbackDns = true;
        continue;
      }
      if (hostAddress == 0) continue;
      const bool duplicate = std::any_of(
          servers->begin(), servers->end(), [&](const IN_ADDR& existing) {
            return existing.S_un.S_addr == address->sin_addr.S_un.S_addr;
          });
      if (!duplicate) servers->push_back(address->sin_addr);
    }
  }
  if (loopbackDns) {
    if (error) {
      *error = L"系统正在使用环回地址作为 DNS 服务器，无法保证 DNS 请求进入 HEV。";
    }
    return false;
  }
  return true;
}

}  // namespace lansocks
