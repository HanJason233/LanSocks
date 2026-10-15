# LanSocks

LanSocks 是一个 Windows 桌面客户端，可把电脑的 IPv4 流量经由局域网中的 Android 手机 SOCKS5 服务转发。手机先连接自己的 VPN 并开启 SOCKS5 局域网共享，电脑再通过 LanSocks 接入；LanSocks 本身不连接 VPN 服务商，也不实现新的代理协议。

```text
Windows 应用 → HEV / Wintun 隧道 → 手机 SOCKS5 → 手机 VPN → 目标网络
```

Windows 隧道数据面使用官方 [HEV SOCKS5 Tunnel](https://github.com/heiher/hev-socks5-tunnel)，版本固定为 2.19.0；虚拟网卡使用 Wintun。项目没有引入 sing-box、mihomo、Xray 或其他代理内核。

## Windows 便携版

构建产物目录：`build/windows/x64/runner/Release/`。其中 `lansocks.exe` 和同目录运行文件需要一起保留。构建脚本会将运行依赖及第三方声明复制到该目录，并附带 HEV 构建所需的 MSYS2 源码归档。将整个 `Release` 目录打包或复制到目标电脑即可运行，不需要安装 Flutter。

启动 `lansocks.exe` 后，填入手机在当前局域网可访问的 IPv4 地址、SOCKS5 端口和可选账号密码。连接时 Windows 会请求管理员权限，以配置隧道路由和 DNS。首次使用请先用“测试 SOCKS5”确认电脑能访问手机服务。

也可以在连接页展开“从 URI 导入”，粘贴 `socks://` 或 `socks5://` 节点链接。例如：

```text
socks://dXNlcjpwYXNz@192.168.1.50:1080#Phone-VPN
```

URI 用户信息支持 Base64、Base64URL 编码或明文 `用户名:密码`；`#` 后面的名称会作为节点名称导入。导入后请核对地址和凭据，再连接。

### 使用条件

- Windows 10/11 x64。
- Android 手机已连接 VPN，并开启 SOCKS5 局域网入站服务。
- 电脑可以通过局域网直接访问手机 SOCKS5 地址和端口。
- 首次建立隧道时允许程序以管理员权限配置网络。

### DNS 与流量范围

Windows 使用 HEV MapDNS：TUN 地址为 `198.18.0.1`，MapDNS 地址为 `198.18.0.2`，映射地址池为 `100.64.0.0/10`。手机 SOCKS5 地址只作为物理网络上的代理端点，不会被设为 DNS 服务器。连接期间 LanSocks 会检查活动网卡 DNS、MapDNS 路由、映射地址路由、SOCKS5 绕行路由及 Windows 实际域名解析；断开、启动失败或异常恢复时会恢复保存的 DNS 和路由状态。

当前 Windows 隧道只转发 IPv4 应用流量。IPv6 应用流量不会经过该隧道，可能直接使用本机网络；连接期间会临时调整活动网卡的 IPv6 DNS 设置以避免 DNS 请求绕过隧道，并在退出隧道时恢复原设置。

## 从源码构建

Windows 构建需要 Flutter 3.47.7、Git、CMake、Ninja，以及安装了“使用 C++ 的桌面开发”工作负载的 Visual Studio 2022。详细工具链要求见 [Flutter Windows 配置文档](https://docs.flutter.dev/platform-integration/windows/setup)。

克隆仓库时要包含 HEV 子模块，然后在项目目录的 PowerShell 中执行：

```powershell
git clone --recurse-submodules <GitHub 仓库地址>
cd LanSocks
Set-ExecutionPolicy -Scope Process Bypass
.\tool\build_windows.ps1 -Configuration Release
```

构建脚本会校验官方 HEV 2.19.0 和 Wintun 0.14.1 文件的 SHA-256，执行 `flutter pub get`、静态分析和单元测试，再生成 Windows Release 目录。可用 `-UseSourceHev` 从固定版本源码构建 HEV；这需要 MSYS2 工具链，具体见 `tool/build_hev.sh`。

Windows DNS 和路由集成场景需要在管理员 PowerShell 中运行，并提供手机 SOCKS5 地址：

```powershell
.\tool\test_windows_tun_dns.ps1 -SocksAddress 192.168.1.50 -SocksPort 1080
```

该脚本会检查失败启动恢复、HEV MapDNS 查询、Windows 域名解析、TUN 与 SOCKS5 绕行路由、物理网卡 DNS 数据包、正常断开，以及异常退出后的恢复。

## 平台支持

- **Windows：** HEV + Wintun IPv4 隧道、MapDNS、DNS 泄漏检查、路由验证和异常恢复。
- **macOS、Linux、Android、iOS：** 目前仅包含 Flutter 界面与设置；尚未实现这些平台的原生 TUN 隧道。
- **IPv6：** IPv6 应用流量当前不通过隧道。

## 项目目录

- `lib/`：Flutter 界面、SOCKS5 检查、HEV 配置和隧道生命周期。
- `native/windows/`：提权后的路由配置、监控与清理辅助程序。
- `native/hev-socks5-tunnel/`：固定版本的 HEV 源码子模块。
- `tool/build_windows.ps1`：Windows 构建与打包脚本。
- `tool/test_windows_tun_dns.ps1`：Windows DNS 和路由集成检查。
- `test/`：HEV 配置和 SOCKS5 协议单元测试。
- `THIRD_PARTY_NOTICES.md`：第三方组件与许可说明。
