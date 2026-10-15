# LanSocks

LanSocks is a small Flutter desktop client that sends this computer's IPv4 traffic through an Android phone's SOCKS5 server. The phone must already be connected to its VPN and expose a SOCKS5 service on the local network. LanSocks does not connect to VPN providers or implement proxy protocols.

The data plane is the official [HEV SOCKS5 Tunnel](https://github.com/heiher/hev-socks5-tunnel), pinned as a Git submodule at 2.19.0. The Windows adapter and route management live in a small native helper; the Flutter UI handles configuration, status, diagnostics, and settings.

## Current platform support

- **Windows:** HEV + Wintun IPv4 tunnel, DNS route steering, route verification, crash cleanup, and recovery.
- **macOS, Linux, Android, iOS:** Flutter interface and settings only. Native TUN support is not implemented on these platforms.
- **IPv6:** unchanged by the Windows tunnel. IPv6 traffic may bypass it.

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

## Use

Enter the phone's reachable IPv4 address, SOCKS5 port, and optional username and password. The **Test SOCKS5** action performs the SOCKS5 negotiation and a CONNECT request to the selected test target. Connect then requests Windows administrator approval to install routes. The app checks that the SOCKS server stays reachable through the physical network and that the test target is routed through the HEV adapter.

Windows adds a specific physical route for the SOCKS5 server, IPv4 default routes through the tunnel, and routes for configured IPv4 DNS servers. On disconnect, helper exit, or detected HEV failure, routes created by LanSocks are removed. System DNS addresses and IPv6 settings are not changed. Use the recovery action if Windows reports that a previous session left recovery state behind.

## Project files

- `docs/PROJECT_SPEC.md` — the original implementation brief.
- `native/hev-socks5-tunnel/` — pinned HEV source submodule.
- `native/windows/` — elevated helper for route setup, monitoring, and cleanup.
- `lib/` — Flutter UI, SOCKS5 checks, HEV configuration, and tunnel lifecycle.
- `tool/build_windows.ps1` — verified Windows build and packaging script.
- `test/` — HEV configuration and SOCKS5 protocol tests.

Third-party notices are in `THIRD_PARTY_NOTICES.md` and in the licenses shipped with their respective source or runtime packages.
