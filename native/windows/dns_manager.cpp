#include "lansocks_native.h"

#include <iphlpapi.h>
#include <netioapi.h>
#include <windns.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace lansocks {
namespace {

constexpr DWORD kDnsSnapshotMagic = 0x4c53444e;  // LSDN
constexpr DWORD kDnsSnapshotVersion = 1;
constexpr DWORD kMaxDnsInterfaces = 256;
constexpr DWORD kMaxDnsStringChars = 32767;

struct DnsSnapshotHeader {
  DWORD magic;
  DWORD version;
  DWORD count;
};

struct DnsSnapshotRecordHeader {
  GUID interfaceGuid;
  DWORD ipv4NameServerChars;
  DWORD ipv4ProfileNameServerChars;
  DWORD ipv6NameServerChars;
  DWORD ipv6ProfileNameServerChars;
};

struct DnsSnapshotRecord {
  GUID interfaceGuid{};
  std::wstring ipv4NameServer;
  std::wstring ipv4ProfileNameServer;
  std::wstring ipv6NameServer;
  std::wstring ipv6ProfileNameServer;
};

bool WriteAll(HANDLE file, const void* data, DWORD size) {
  const auto* cursor = static_cast<const BYTE*>(data);
  while (size > 0) {
    DWORD written = 0;
    if (!WriteFile(file, cursor, size, &written, nullptr) || written == 0) {
      return false;
    }
    cursor += written;
    size -= written;
  }
  return true;
}

bool ReadAll(HANDLE file, void* data, DWORD size) {
  auto* cursor = static_cast<BYTE*>(data);
  while (size > 0) {
    DWORD read = 0;
    if (!ReadFile(file, cursor, size, &read, nullptr) || read == 0) {
      return false;
    }
    cursor += read;
    size -= read;
  }
  return true;
}

bool WriteString(HANDLE file, const std::wstring& value) {
  if (value.size() > kMaxDnsStringChars) return false;
  const DWORD bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
  return bytes == 0 || WriteAll(file, value.data(), bytes);
}

bool ReadString(HANDLE file, DWORD characters, std::wstring* value) {
  if (characters > kMaxDnsStringChars) return false;
  value->assign(characters, L'\0');
  const DWORD bytes = characters * sizeof(wchar_t);
  return bytes == 0 || ReadAll(file, value->data(), bytes);
}

bool SaveSnapshot(const AppPaths& paths,
                  const std::vector<DnsSnapshotRecord>& records) {
  const std::wstring temporary =
      paths.dnsStatePath + L"." + std::to_wstring(GetCurrentProcessId()) +
      L".tmp";
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;

  const DnsSnapshotHeader header{kDnsSnapshotMagic, kDnsSnapshotVersion,
                                 static_cast<DWORD>(records.size())};
  bool success = records.size() <= kMaxDnsInterfaces &&
                 WriteAll(file, &header, sizeof(header));
  for (const DnsSnapshotRecord& record : records) {
    const DnsSnapshotRecordHeader recordHeader{
        record.interfaceGuid,
        static_cast<DWORD>(record.ipv4NameServer.size()),
        static_cast<DWORD>(record.ipv4ProfileNameServer.size()),
        static_cast<DWORD>(record.ipv6NameServer.size()),
        static_cast<DWORD>(record.ipv6ProfileNameServer.size())};
    success = success && WriteAll(file, &recordHeader, sizeof(recordHeader)) &&
              WriteString(file, record.ipv4NameServer) &&
              WriteString(file, record.ipv4ProfileNameServer) &&
              WriteString(file, record.ipv6NameServer) &&
              WriteString(file, record.ipv6ProfileNameServer);
    if (!success) break;
  }
  success = success && FlushFileBuffers(file);
  CloseHandle(file);
  if (!success) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  if (!MoveFileExW(temporary.c_str(), paths.dnsStatePath.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  return true;
}

bool LoadSnapshot(const AppPaths& paths,
                  std::vector<DnsSnapshotRecord>* records,
                  std::wstring* error) {
  HANDLE file = CreateFileW(paths.dnsStatePath.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE |
                                FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    if (error) *error = L"DNS 恢复快照不存在或无法读取。";
    return false;
  }
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart < sizeof(DnsSnapshotHeader) ||
      size.QuadPart > 4 * 1024 * 1024) {
    CloseHandle(file);
    if (error) *error = L"DNS 恢复快照大小无效。";
    return false;
  }

  DnsSnapshotHeader header{};
  bool success = ReadAll(file, &header, sizeof(header)) &&
                 header.magic == kDnsSnapshotMagic &&
                 header.version == kDnsSnapshotVersion &&
                 header.count > 0 && header.count <= kMaxDnsInterfaces;
  std::vector<DnsSnapshotRecord> loaded;
  if (success) loaded.reserve(header.count);
  for (DWORD i = 0; success && i < header.count; ++i) {
    DnsSnapshotRecordHeader recordHeader{};
    success = ReadAll(file, &recordHeader, sizeof(recordHeader)) &&
              recordHeader.ipv4NameServerChars <= kMaxDnsStringChars &&
              recordHeader.ipv4ProfileNameServerChars <= kMaxDnsStringChars &&
              recordHeader.ipv6NameServerChars <= kMaxDnsStringChars &&
              recordHeader.ipv6ProfileNameServerChars <= kMaxDnsStringChars;
    if (!success) break;

    DnsSnapshotRecord record{};
    record.interfaceGuid = recordHeader.interfaceGuid;
    success = ReadString(file, recordHeader.ipv4NameServerChars,
                         &record.ipv4NameServer) &&
              ReadString(file, recordHeader.ipv4ProfileNameServerChars,
                         &record.ipv4ProfileNameServer) &&
              ReadString(file, recordHeader.ipv6NameServerChars,
                         &record.ipv6NameServer) &&
              ReadString(file, recordHeader.ipv6ProfileNameServerChars,
                         &record.ipv6ProfileNameServer);
    if (success) loaded.push_back(std::move(record));
  }
  LARGE_INTEGER finalPosition{};
  const bool gotPosition = SetFilePointerEx(file, {}, &finalPosition, FILE_CURRENT);
  CloseHandle(file);
  success = success && gotPosition && finalPosition.QuadPart == size.QuadPart;
  if (!success) {
    if (error) *error = L"DNS 恢复快照损坏，未修改 DNS 配置。";
    return false;
  }
  *records = std::move(loaded);
  return true;
}

bool EnumerateActiveDnsInterfaces(std::vector<GUID>* interfaces,
                                  std::wstring* error) {
  ULONG size = 16 * 1024;
  std::vector<BYTE> storage(size);
  auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
  ULONG result = GetAdaptersAddresses(
      AF_UNSPEC,
      GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
          GAA_FLAG_SKIP_UNICAST,
      nullptr, adapters, &size);
  if (result == ERROR_BUFFER_OVERFLOW) {
    storage.resize(size);
    adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    result = GetAdaptersAddresses(
        AF_UNSPEC,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
            GAA_FLAG_SKIP_UNICAST,
        nullptr, adapters, &size);
  }
  if (result != NO_ERROR) {
    if (error) {
      *error = L"无法枚举 Windows 网络接口（错误 " +
               std::to_wstring(result) + L"）。";
    }
    return false;
  }

  interfaces->clear();
  for (auto* adapter = adapters; adapter != nullptr; adapter = adapter->Next) {
    if (adapter->OperStatus != IfOperStatusUp ||
        adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
      continue;
    }
    GUID interfaceGuid{};
    if (ConvertInterfaceLuidToGuid(&adapter->Luid, &interfaceGuid) != NO_ERROR) {
      if (error) *error = L"无法读取活动网卡的接口 GUID。";
      return false;
    }
    const bool duplicate = std::any_of(
        interfaces->begin(), interfaces->end(), [&](const GUID& existing) {
          return IsEqualGUID(existing, interfaceGuid) != FALSE;
        });
    if (!duplicate) interfaces->push_back(interfaceGuid);
  }
  if (interfaces->empty()) {
    if (error) *error = L"没有可配置 DNS 的活动网络接口。";
    return false;
  }
  return true;
}

std::wstring ReadSettingString(PWSTR value) {
  return value == nullptr ? L"" : std::wstring(value);
}

bool ReadDnsFamily(const GUID& interfaceGuid,
                   bool ipv6,
                   std::wstring* nameServer,
                   std::wstring* profileNameServer,
                   std::wstring* error) {
  DNS_INTERFACE_SETTINGS settings{};
  settings.Version = DNS_INTERFACE_SETTINGS_VERSION1;
  // Windows 10 returns the selected IP-family settings when this selector is
  // supplied. IPv4 uses the documented zero-flags form.
  settings.Flags = ipv6 ? DNS_SETTING_IPV6 : 0;
  const NETIO_STATUS result = GetInterfaceDnsSettings(interfaceGuid, &settings);
  if (result != NO_ERROR) {
    if (error) {
      *error = L"无法读取网卡 DNS 配置（错误 " + std::to_wstring(result) +
               L"）。";
    }
    return false;
  }
  *nameServer = ReadSettingString(settings.NameServer);
  *profileNameServer = ReadSettingString(settings.ProfileNameServer);
  FreeInterfaceDnsSettings(&settings);
  return true;
}

bool SetDnsFamily(const GUID& interfaceGuid,
                  bool ipv6,
                  const std::wstring& nameServer,
                  const std::wstring& profileNameServer,
                  std::wstring* error) {
  DNS_INTERFACE_SETTINGS settings{};
  settings.Version = DNS_INTERFACE_SETTINGS_VERSION1;
  settings.Flags = DNS_SETTING_NAMESERVER | DNS_SETTING_PROFILE_NAMESERVER |
                   (ipv6 ? DNS_SETTING_IPV6 : 0);
  settings.NameServer = const_cast<PWSTR>(nameServer.c_str());
  settings.ProfileNameServer = const_cast<PWSTR>(profileNameServer.c_str());
  const NETIO_STATUS result = SetInterfaceDnsSettings(interfaceGuid, &settings);
  if (result != NO_ERROR) {
    if (error) {
      *error = L"无法设置网卡 DNS 配置（错误 " + std::to_wstring(result) +
               L"）。";
    }
    return false;
  }
  return true;
}

bool ReadRecord(const GUID& interfaceGuid,
                DnsSnapshotRecord* record,
                std::wstring* error) {
  record->interfaceGuid = interfaceGuid;
  return ReadDnsFamily(interfaceGuid, false, &record->ipv4NameServer,
                       &record->ipv4ProfileNameServer, error) &&
         ReadDnsFamily(interfaceGuid, true, &record->ipv6NameServer,
                       &record->ipv6ProfileNameServer, error);
}

bool FindRecord(const std::vector<DnsSnapshotRecord>& records,
                const GUID& interfaceGuid,
                size_t* index) {
  for (size_t i = 0; i < records.size(); ++i) {
    if (IsEqualGUID(records[i].interfaceGuid, interfaceGuid)) {
      *index = i;
      return true;
    }
  }
  return false;
}

bool InterfaceStillExists(const GUID& interfaceGuid) {
  NET_LUID luid{};
  if (ConvertInterfaceGuidToLuid(&interfaceGuid, &luid) != NO_ERROR) return false;
  NET_IFINDEX index = 0;
  if (ConvertInterfaceLuidToIndex(&luid, &index) != NO_ERROR) return false;
  MIB_IF_ROW2 row{};
  row.InterfaceIndex = index;
  return GetIfEntry2(&row) == NO_ERROR;
}

bool VerifyEffectiveDnsServers(std::wstring* error) {
  ULONG size = 16 * 1024;
  std::vector<BYTE> storage(size);
  auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
  ULONG result = GetAdaptersAddresses(
      AF_UNSPEC,
      GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
          GAA_FLAG_SKIP_UNICAST,
      nullptr, adapters, &size);
  if (result == ERROR_BUFFER_OVERFLOW) {
    storage.resize(size);
    adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    result = GetAdaptersAddresses(
        AF_UNSPEC,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
            GAA_FLAG_SKIP_UNICAST,
        nullptr, adapters, &size);
  }
  if (result != NO_ERROR) {
    if (error) {
      *error = L"无法验证 Windows 当前生效的 DNS 服务器（错误 " +
               std::to_wstring(result) + L"）。";
    }
    return false;
  }

  IN_ADDR expectedIpv4{};
  InetPtonW(AF_INET, kMapDnsAddress, &expectedIpv4);
  for (auto* adapter = adapters; adapter != nullptr; adapter = adapter->Next) {
    if (adapter->OperStatus != IfOperStatusUp ||
        adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
      continue;
    }
    DWORD ipv4Count = 0;
    DWORD ipv6Count = 0;
    bool unexpectedIpv4 = false;
    bool unexpectedIpv6 = false;
    for (auto* dns = adapter->FirstDnsServerAddress; dns != nullptr;
         dns = dns->Next) {
      if (dns->Address.lpSockaddr == nullptr) continue;
      if (dns->Address.lpSockaddr->sa_family == AF_INET) {
        const auto* address =
            reinterpret_cast<const sockaddr_in*>(dns->Address.lpSockaddr);
        ++ipv4Count;
        unexpectedIpv4 |=
            address->sin_addr.S_un.S_addr != expectedIpv4.S_un.S_addr;
      } else if (dns->Address.lpSockaddr->sa_family == AF_INET6) {
        const auto* address =
            reinterpret_cast<const sockaddr_in6*>(dns->Address.lpSockaddr);
        ++ipv6Count;
        unexpectedIpv6 |= !IN6_IS_ADDR_LOOPBACK(&address->sin6_addr);
      }
    }
    if (ipv4Count == 0 || unexpectedIpv4) {
      if (error) *error = L"活动网卡仍配置了 MapDNS 以外的 IPv4 DNS 服务器。";
      return false;
    }
    if (ipv6Count == 0 || unexpectedIpv6) {
      if (error) *error = L"活动网卡仍配置了可向物理网络发送的 IPv6 DNS 服务器。";
      return false;
    }
  }
  return true;
}

}  // namespace

bool CaptureDnsSnapshot(const AppPaths& paths, std::wstring* error) {
  if (HasDnsSnapshot(paths)) {
    if (error) *error = L"发现上次 DNS 恢复快照，必须先恢复后再启动。";
    return false;
  }
  std::vector<GUID> interfaces;
  if (!EnumerateActiveDnsInterfaces(&interfaces, error)) return false;
  std::vector<DnsSnapshotRecord> records;
  records.reserve(interfaces.size());
  for (const GUID& interfaceGuid : interfaces) {
    DnsSnapshotRecord record{};
    if (!ReadRecord(interfaceGuid, &record, error)) return false;
    records.push_back(std::move(record));
  }
  if (!SaveSnapshot(paths, records)) {
    if (error) *error = L"无法保存原始 DNS 恢复快照。";
    return false;
  }
  return true;
}

bool EnsureMapDnsSettings(const AppPaths& paths,
                          bool* changed,
                          std::wstring* error) {
  if (changed) *changed = false;
  std::vector<DnsSnapshotRecord> records;
  if (!LoadSnapshot(paths, &records, error)) return false;

  std::vector<GUID> interfaces;
  if (!EnumerateActiveDnsInterfaces(&interfaces, error)) return false;
  bool configurationChanged = false;
  for (const GUID& interfaceGuid : interfaces) {
    size_t index = 0;
    if (!FindRecord(records, interfaceGuid, &index)) {
      DnsSnapshotRecord record{};
      if (!ReadRecord(interfaceGuid, &record, error)) return false;
      records.push_back(std::move(record));
      if (!SaveSnapshot(paths, records)) {
        if (error) *error = L"无法记录新网卡的原始 DNS 配置。";
        return false;
      }
    }

    std::wstring ipv4NameServer;
    std::wstring ipv4ProfileNameServer;
    std::wstring ipv6NameServer;
    std::wstring ipv6ProfileNameServer;
    if (!ReadDnsFamily(interfaceGuid, false, &ipv4NameServer,
                       &ipv4ProfileNameServer, error) ||
        !ReadDnsFamily(interfaceGuid, true, &ipv6NameServer,
                       &ipv6ProfileNameServer, error)) {
      return false;
    }
    const bool ipv4Ready = ipv4NameServer == kMapDnsAddress &&
                           ipv4ProfileNameServer == kMapDnsAddress;
    const bool ipv6Ready = ipv6NameServer == kMapDnsIpv6Block &&
                           ipv6ProfileNameServer == kMapDnsIpv6Block;
    if (ipv4Ready && ipv6Ready) continue;

    if (!SetDnsFamily(interfaceGuid, false, kMapDnsAddress, kMapDnsAddress, error) ||
        !SetDnsFamily(interfaceGuid, true, kMapDnsIpv6Block, kMapDnsIpv6Block, error)) {
      return false;
    }
    configurationChanged = true;
  }

  if (changed) *changed = configurationChanged;
  return true;
}

bool VerifyMapDnsSettings(const AppPaths& paths, std::wstring* error) {
  std::vector<DnsSnapshotRecord> records;
  if (!LoadSnapshot(paths, &records, error)) return false;
  std::vector<GUID> interfaces;
  if (!EnumerateActiveDnsInterfaces(&interfaces, error)) return false;
  for (const GUID& interfaceGuid : interfaces) {
    std::wstring nameServer;
    std::wstring profileNameServer;
    if (!ReadDnsFamily(interfaceGuid, false, &nameServer, &profileNameServer,
                       error)) return false;
    if (nameServer != kMapDnsAddress || profileNameServer != kMapDnsAddress) {
      if (error) *error = L"活动网卡的 IPv4 DNS 未全部指向 HEV MapDNS。";
      return false;
    }
    if (!ReadDnsFamily(interfaceGuid, true, &nameServer, &profileNameServer,
                       error)) return false;
    if (nameServer != kMapDnsIpv6Block || profileNameServer != kMapDnsIpv6Block) {
      if (error) *error = L"活动网卡的 IPv6 DNS 未被阻止，可能绕过 IPv4 TUN。";
      return false;
    }
  }
  return VerifyEffectiveDnsServers(error);
}

bool VerifyMapDnsQuery(std::wstring* error) {
  PDNS_RECORDW records = nullptr;
  const DNS_STATUS result = DnsQuery_W(
      L"example.com.", DNS_TYPE_A,
      DNS_QUERY_BYPASS_CACHE | DNS_QUERY_TREAT_AS_FQDN, nullptr, &records,
      nullptr);
  if (result != 0) {
    if (records != nullptr) DnsRecordListFree(records, DnsFreeRecordList);
    if (error) {
      *error = L"Windows 实际 DNS 查询未通过 HEV MapDNS（错误 " +
               std::to_wstring(result) + L"）。";
    }
    return false;
  }

  bool mappedAddressFound = false;
  for (PDNS_RECORDW record = records; record != nullptr; record = record->pNext) {
    if (record->wType != DNS_TYPE_A) continue;
    const ULONG address = ntohl(record->Data.A.IpAddress);
    if ((address & 0xffc00000UL) == 0x64400000UL) {
      mappedAddressFound = true;
      break;
    }
  }
  DnsRecordListFree(records, DnsFreeRecordList);
  if (!mappedAddressFound) {
    if (error) {
      *error = L"Windows DNS 查询成功，但未返回 HEV MapDNS 的 100.64.0.0/10 映射地址。";
    }
    return false;
  }
  return true;
}

bool RestoreDnsSnapshot(const AppPaths& paths, std::wstring* error) {
  if (!HasDnsSnapshot(paths)) return true;
  std::vector<DnsSnapshotRecord> records;
  if (!LoadSnapshot(paths, &records, error)) return false;

  bool success = true;
  std::wstring lastError;
  for (const DnsSnapshotRecord& record : records) {
    if (!InterfaceStillExists(record.interfaceGuid)) continue;
    std::wstring currentError;
    if (!SetDnsFamily(record.interfaceGuid, false, record.ipv4NameServer,
                      record.ipv4ProfileNameServer, &currentError)) {
      success = false;
      lastError = currentError;
    }
    currentError.clear();
    if (!SetDnsFamily(record.interfaceGuid, true, record.ipv6NameServer,
                      record.ipv6ProfileNameServer, &currentError)) {
      success = false;
      lastError = currentError;
    }
  }
  if (!success && error) *error = lastError;
  return success;
}

bool RemoveDnsSnapshot(const AppPaths& paths) {
  if (DeleteFileW(paths.dnsStatePath.c_str())) return true;
  return GetLastError() == ERROR_FILE_NOT_FOUND;
}

bool HasDnsSnapshot(const AppPaths& paths) {
  return GetFileAttributesW(paths.dnsStatePath.c_str()) !=
         INVALID_FILE_ATTRIBUTES;
}

}  // namespace lansocks
