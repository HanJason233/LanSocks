# LanSocks

LanSocks is a small Flutter desktop client that sends this computer's IPv4 traffic through an Android phone's SOCKS5 server. The phone must already be connected to its VPN and expose a SOCKS5 service on the local network. LanSocks does not connect to VPN providers or implement proxy protocols.

The data plane is the official [HEV SOCKS5 Tunnel](https://github.com/heiher/hev-socks5-tunnel), pinned as a Git submodule at 2.19.0. The Windows adapter and route management live in a small native helper; the Flutter UI handles configuration, status, diagnostics, and settings.

## Current platform support

- **Windows:** HEV + Wintun IPv4 tunnel, HEV MapDNS, DNS leak checks, route verification, crash cleanup, and recovery.
- **macOS, Linux, Android, iOS:** Flutter interface and settings only. Native TUN support is not implemented on these platforms.
- **IPv6:** IPv6 application traffic is not routed through the tunnel and may bypass it. During a tunnel session, DNS servers on active adapters are temporarily set to `::1` to prevent IPv6 DNS from escaping; the original settings are restored on disconnect or recovery.

## Build on Windows

Install Flutter 3.47.7, Git, CMake, Ninja, and Visual Studio 2022 with the **Desktop development with C++** workload. Flutter requires the Visual Studio C++ toolchain for Windows desktop builds: [Flutter Windows setup](https://docs.flutter.dev/platform-integration/windows/setup).

Clone the repository with submodules, then run PowerShell from the project directory:

```powershell
git clone --recurse-submodules <repository-url>
cd LanSocks
Set-ExecutionPolicy -Scope Process Bypass
.\tool\build_windows.ps1 -Configuration Release
```

The script fetches the official HEV 2.19.0 Windows release and Wintun 0.14.1 package, verifies their pinned SHA-256 hashes, runs `flutter pub get`, `flutter analyze`, `flutter test`, builds the Windows app, and copies the runtime files and license notices into the bundle. It also puts the verified MSYS2 runtime source archive in `third_party_sources/` beside the app bundle. Include that directory when sharing the Windows build. The script does not download an alternate proxy engine. The resulting executable is `build\windows\x64\runner\Release\lansocks.exe`.

To build HEV from its pinned source instead of using the official Windows release, install the MSYS2 toolchain and run `tool/build_hev.sh` from an MSYS2 MSYS shell. Then run the PowerShell build with `-UseSourceHev`.

Run the Windows DNS integration scenarios from an elevated PowerShell after building. Supply the phone's reachable SOCKS5 endpoint and credentials when required:

```powershell
.\tool\test_windows_tun_dns.ps1 -SocksAddress 192.168.1.50 -SocksPort 1080
```

The script checks failed startup, a live HEV MapDNS A query, Windows system DNS resolution, TUN and SOCKS bypass routes, physical-NIC DNS packets with Pktmon, normal disconnect, and recovery after terminating the helper.

## Use

Enter the phone's reachable IPv4 address, SOCKS5 port, and optional username and password. The **Test SOCKS5** action performs the SOCKS5 negotiation and a CONNECT request to the selected test target. Connect then requests Windows administrator approval to install routes. The app checks that the SOCKS server stays reachable through the physical network and that the test target is routed through the HEV adapter.

You can also expand **Import from URI** on the connection page and paste a `socks://` or `socks5://` URI. The URI can contain Base64 or Base64URL encoded `username:password` credentials, or plain `username:password` user info. A fragment such as `#Chimera-LAN` is shown as the imported name.

Windows uses HEV MapDNS at `198.18.0.2`, with the TUN at `198.18.0.1` and mapped addresses in `100.64.0.0/10`. It verifies the DNS route, mapped-address route, physical SOCKS5 bypass, all active adapters' DNS settings, and an actual Windows DNS query before reporting the tunnel as connected. DNS settings are snapshotted before changes; failures, disconnects, and recovery restore them. The phone's SOCKS5 address remains a physical-network endpoint and is never configured as DNS. On disconnect, routes created by LanSocks are removed while pre-existing routes are retained. Use the recovery action if Windows reports that a previous session left recovery state behind.

## Project files

- `docs/PROJECT_SPEC.md` — the original implementation brief.
- `native/hev-socks5-tunnel/` — pinned HEV source submodule.
- `native/windows/` — elevated helper for route setup, monitoring, and cleanup.
- `lib/` — Flutter UI, SOCKS5 checks, HEV configuration, and tunnel lifecycle.
- `tool/build_windows.ps1` — verified Windows build and packaging script.
- `tool/test_windows_tun_dns.ps1` — elevated Windows DNS and route integration scenarios.
- `test/` — HEV configuration and SOCKS5 protocol tests.

Third-party notices are in `THIRD_PARTY_NOTICES.md` and in the licenses shipped with their respective source or runtime packages.
