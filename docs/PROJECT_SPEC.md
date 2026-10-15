# 项目开发任务：LanSocks

你是一名具有 Flutter、C/C++、Windows 网络驱动、TUN、SOCKS5 和跨平台开发经验的软件工程师。

请直接在当前工作区创建一个名为 `LanSocks` 的完整开源项目，并进行实际代码开发、构建和测试，而不是仅仅生成设计文档。

## 一、项目目标

开发一个轻量级、跨平台、带图形界面的 TUN2SOCKS5 客户端。

使用场景：

Android 手机已经运行完整的 VPN 客户端，并且已经开启 SOCKS5 局域网共享服务。

LanSocks 不负责连接 VPN 节点，不处理任何 VLESS、VMess、Trojan、Shadowsocks 等代理协议。

LanSocks 只需要接管当前设备的网络流量，通过 HEV 转换为 SOCKS5 请求，发送给手机的局域网 SOCKS5 服务端。

完整架构：

```text
Windows / macOS / Linux / Android / iOS
                  │
             Flutter GUI
                  │
            原生平台适配层
                  │
            TUN 虚拟网络接口
                  │
          HEV-SOCKS5-TUNNEL
                  │
         局域网 SOCKS5 连接
                  │
            Android 手机
                  │
           手机现有 VPN
                  │
              互联网
```

## 二、强制技术要求

### 1. TUN2SOCKS 引擎

必须使用官方项目：

https://github.com/heiher/hev-socks5-tunnel

要求：

- HEV 是唯一的数据转发引擎。
- 不允许自己重新开发 TCP/IP 协议栈。
- 不允许自己实现已有的 TUN2SOCKS 逻辑。
- 不允许引入 sing-box、mihomo、Xray、v2ray-core、Clash。
- 不允许引入其他第三方 tun2socks 实现作为替代。
- HEV 通过 Git Submodule 管理，并固定经过验证的版本。
- 尽量不修改 HEV 原始源码。
- 首选官方稳定 API、构建方式和配置格式。
- 不允许通过模拟命令执行结果来伪装功能已完成。

允许使用各平台必要的虚拟网卡驱动、系统 API、JNI、FFI、Network Extension 等基础组件，这些不视为额外代理内核。

### 2. GUI 技术

统一使用 Flutter + Dart。

要求：

- UI 代码尽可能跨平台复用。
- 使用 Material 3。
- 适配桌面窗口与移动端屏幕。
- 默认中文界面。
- 支持浅色和深色主题。
- 尽量减少第三方 Flutter 依赖。
- 不引入大型状态管理框架，除非有明确必要。
- GUI 与原生网络功能严格解耦。

### 3. 支持平台

目标平台：

1. Windows 10/11（优先完成）
2. Linux
3. macOS
4. Android
5. iOS

第一阶段必须完成 Windows 的可运行版本。

其他平台先创建标准 Flutter 工程和统一接口，不需要一次性实现全部平台。

不得将尚未完成的平台标记为已支持。

## 三、项目目录

采用下列目录结构，可在不增加不必要复杂度的前提下调整文件名称。

```text
LanSocks/
├── lib/
│   ├── main.dart
│   ├── app.dart
│   │
│   ├── models/
│   │   ├── socks5_config.dart
│   │   ├── tunnel_config.dart
│   │   └── tunnel_status.dart
│   │
│   ├── services/
│   │   ├── tunnel_manager.dart
│   │   ├── hev_config.dart
│   │   ├── socks5_checker.dart
│   │   ├── network_monitor.dart
│   │   └── settings_store.dart
│   │
│   ├── platform/
│   │   ├── tunnel_platform.dart
│   │   └── tunnel_bridge.dart
│   │
│   └── ui/
│       ├── pages/
│       │   ├── home_page.dart
│       │   ├── settings_page.dart
│       │   └── logs_page.dart
│       │
│       └── widgets/
│           ├── connect_button.dart
│           ├── status_card.dart
│           └── traffic_view.dart
│
├── native/
│   ├── hev-socks5-tunnel/
│   │
│   ├── windows/
│   │   ├── tunnel_helper.cpp
│   │   ├── route_manager.cpp
│   │   └── dns_manager.cpp
│   │
│   ├── linux/
│   └── apple/
│
├── windows/
├── linux/
├── macos/
├── android/
├── ios/
│
├── assets/
│   └── icons/
│
├── tool/
│   ├── build_windows.ps1
│   └── build_hev.sh
│
├── test/
├── pubspec.yaml
├── .gitmodules
├── .gitignore
└── README.md
```

Flutter 自动生成的平台工程应保持其标准结构，不要为了符合目录树而破坏 Flutter 官方构建机制。

## 四、GUI 功能要求

### 主界面

界面保持简洁，包含：

- SOCKS5 服务端 IP 地址
- SOCKS5 服务端端口
- 用户名（可选）
- 密码（可选）
- 显示/隐藏密码
- 测试 SOCKS5 连接
- 连接/断开按钮
- 当前连接状态
- 实时上传速度
- 实时下载速度
- 已上传/下载流量

连接按钮必须对应真实的 TUN 启动与停止操作。

不能只修改界面状态而没有真正启动 HEV。

### 设置页面

包含：

- 开机自动启动（可以后续实现）
- 连接断开后的重连策略
- SOCKS5 UDP 模式
- DNS 配置
- TUN MTU
- 日志等级
- IPv6 支持状态

不要加入订阅管理、节点列表、代理协议选择、复杂分流规则等无关功能。

### 日志页面

至少显示：

- HEV 启动和退出信息
- SOCKS5 连接错误
- TUN 创建错误
- 系统路由配置错误
- DNS 修改与恢复结果
- 网络断开与恢复事件

日志不得泄露 SOCKS5 密码。

## 五、Windows 端实现

这是第一阶段最重要的开发任务。

### 1. HEV 集成

先阅读官方 HEV README、Windows 构建说明、配置格式和源码接口。

根据官方支持方式编译 HEV Windows 可执行文件。

Windows 第一版优先采用独立 HEV 进程，而不是将 HEV 强行编译到 Flutter 进程内部。

由原生平台层负责：

- HEV 进程启动
- HEV 进程停止
- 配置文件生成
- 工作状态监测
- 异常退出处理
- 运行日志读取
- 流量统计

优先使用 HEV 本身已支持的功能。

不得假设 HEV 必须由 GUI 另外调用 Wintun API 创建接口；先核对官方 Windows 实现与其 TUN 驱动依赖，避免重复创建虚拟网卡。

### 2. SOCKS5 配置

通过 GUI 生成 HEV YAML 配置。

例如：

```yaml
tunnel:
  name: LanSocks
  mtu: 1500
  ipv4: 198.18.0.1

socks5:
  address: 192.168.1.50
  port: 1080
  udp: 'udp'

misc:
  log-level: info
```

这是示例配置，必须根据官方 HEV 当前稳定版本核实字段，并允许实际 IP、端口、认证参数动态变化。

SOCKS5 用户名和密码使用官方支持的字段。

不能把示例地址写死。

SOCKS5 连接测试必须真正执行握手，并验证代理服务器能处理 CONNECT，而不是只检查 TCP 端口是否开放。

### 3. Windows TUN 路由

必须实现：

- 检测当前默认物理网络接口
- 获取当前物理网关
- 保存需要修改的原始路由
- 为手机 SOCKS5 地址设置物理网络绕行路由
- 启动 HEV
- 配置 TUN 流量接管
- 避免手机 SOCKS5 连接重新进入 TUN
- 停止时删除本程序创建的路由
- 恢复被修改的原始配置

严禁在运行过程中直接删除所有系统路由。

严禁通过重置整个 Windows 网络配置来解决问题。

要考虑有线网卡、Wi-Fi、网关变化和多网卡场景。

对于默认网关和 SOCKS5 服务地址处于同一子网的情况，也需要正确处理。

所有操作必须支持失败回滚。

### 4. DNS

DNS 必须有明确的处理方案。

要求：

- 保存原始 DNS 配置。
- 使用经过验证的 DNS 转发/隧道方案。
- 避免启用 TUN 后 DNS 请求形成死循环。
- 避免 DNS 请求意外从物理网卡直接泄漏。
- 停止或异常退出后恢复原始配置。
- 不允许用一个硬编码的公共 DNS 地址替代所有用户原有配置。

IPv6 需要单独处理。尚未实现完整 IPv6 接管时，必须明确提示 IPv6 应用流量可能绕过隧道，不得宣称已经实现全流量代理。为避免 DNS 泄漏，连接期间可临时将活动网卡的 IPv6 DNS 指向本机回环；必须在启动前保存并在断开、失败或恢复时还原。

### 5. UDP

使用 HEV 官方支持的 UDP 模式。

标准 SOCKS5 UDP 转发需要服务端支持 UDP ASSOCIATE。

如果手机端不支持，必须给出清晰错误提示，不能将 UDP 转发结果伪装为成功。

不要自行发明 SOCKS5 UDP 协议。

### 6. 权限管理

Windows 系统路由修改通常需要管理员权限。

要求：

- GUI 尽可能正常权限运行。
- 特权操作由专门的辅助程序完成。
- 辅助程序只能执行预定义的网络配置操作。
- 不提供任意命令执行接口。
- 对 IP、CIDR、端口、配置路径进行严格校验。
- 处理 UAC 提权失败场景。

## 六、连接生命周期

统一定义连接状态：

```text
disconnected
checking
connecting
connected
disconnecting
error
```

连接流程：

```text
用户点击连接
    ↓
检查配置
    ↓
检测手机 SOCKS5
    ↓
检查管理员权限
    ↓
保存网络环境
    ↓
创建 SOCKS5 绕行路由
    ↓
启动 HEV
    ↓
配置 TUN 路由和 DNS
    ↓
验证真实网络转发
    ↓
显示连接成功
```

断开流程：

```text
用户点击断开
    ↓
停止流量接管
    ↓
恢复路由、DNS
    ↓
停止 HEV 并清理资源
    ↓
显示断开
```

如果任意步骤失败，必须按照实际已完成的修改执行回滚。

考虑软件强制关闭、HEV 崩溃、电脑休眠唤醒、Wi-Fi 切换、手机 SOCKS5 服务停止等情况。

建立持久化的恢复记录，防止程序崩溃后残留错误路由。

不得将程序启动成功等同于代理已经连通。

## 七、跨平台接口

Flutter 与平台实现之间必须有一个统一抽象层。

可以采用 MethodChannel 或 FFI，但需要避免不必要的复杂度。

建议接口：

```dart
abstract class TunnelPlatform {
  Future<void> prepare();

  Future<void> start(Socks5Config config);

  Future<void> stop();

  Future<TunnelStatus> getStatus();

  Stream<TunnelStatus> watchStatus();
}
```

Windows 实现真实功能。

其他平台先预留接口和目录，但未实现时应明确返回 UnsupportedError，不允许写空方法假装功能已经完成。

未来平台可以直接复用：

- Flutter GUI
- SOCKS5 配置模型
- HEV 配置生成逻辑
- 状态管理
- 错误处理
- 流量统计展示

而 TUN、系统路由、DNS、权限等使用各平台原生实现。

## 八、代码质量要求

- 代码尽量简洁。
- 避免过度抽象。
- 不创建不必要的 Manager、Provider 或 Repository。
- 不使用大型服务框架。
- 避免重复代码。
- 核心模块保持职责明确。
- 异常必须有明确日志。
- 网络恢复逻辑必须可测试。
- 不把用户名和密码直接写入普通日志。
- 不能依赖第三方在线服务。
- 不增加遥测、广告、账号登录或云端后台。
- 不编造 HEV API 或 Windows 网络调用。
- 不将模拟数据作为真实连接状态展示。

## 九、测试与验收

必须至少验证：

1. Flutter Windows 应用可以构建并启动。
2. 可以通过 GUI 输入手机 SOCKS5 地址。
3. SOCKS5 握手与 CONNECT 检测正常。
4. HEV 能正确启动与停止。
5. TUN 能成功创建。
6. Windows 应用流量可以经 SOCKS5 转发。
7. SOCKS5 服务端连接不会发生 TUN 回环。
8. 断开后路由和 DNS 可以恢复。
9. 重复启动、停止不会残留网络配置。
10. HEV 启动失败能够回滚。
11. SOCKS5 服务不可用时 UI 能正确反馈。
12. UDP 和 IPv6 的实际支持情况有明确测试结果。

编写单元测试和能够运行的集成测试。

如果开发环境缺少 Windows 权限、驱动或真实手机 SOCKS5 服务，不能虚报端到端测试通过。

此时完成所有可执行测试，并提供实际 Windows 测试步骤和结果记录。

## 十、开发顺序

严格按照以下顺序执行：

**阶段一：工程初始化**

创建 Flutter 项目和必要的目录结构，建立 HEV Git 子模块，配置 Windows 构建脚本。

**阶段二：HEV Windows 集成**

编译或集成 HEV，实现真实进程启动停止，生成正确配置文件。

**阶段三：Windows 网络接管**

实现 TUN、路由绕行、DNS 管理、权限处理和错误回滚。

**阶段四：Flutter GUI**

创建主页、设置页、日志页，将真实的原生接口接入按钮和状态显示。

**阶段五：测试与修复**

执行构建、单元测试、连接测试、断开恢复测试，修复实际发现的问题。

**阶段六：跨平台结构**

确认统一接口可复用，补充 Linux、macOS、Android、iOS 的构建说明和待实现任务，不要在 Windows 完成前大规模开发其他系统。

## 十一、输出要求

不要停留在设计阶段，直接修改工作区文件。

开始开发前先检查当前目录，避免覆盖已有项目。

遇到问题时先阅读对应源码及官方文档，不要猜测接口。

每完成一个阶段执行相应的构建或测试。

最终提供：

- 实际创建的目录结构
- 已完成的功能
- 关键源码位置
- 构建及启动命令
- 已通过的测试
- 未通过的测试和原因
- 当前未实现的功能
- 下一阶段的工作清单

## 十二、最高优先级约束

这是一个**轻量级 SOCKS5 TUN 客户端**，不是完整代理软件。

不要因为实现方便而引入 sing-box、mihomo、Xray 或任何其他代理内核。

不要增加手机端的 VPN 逻辑，手机端已经完成。

不要开发 SOCKS5 服务端，手机已提供服务。

不要实现订阅、节点管理或多协议转换。

不允许重新实现 HEV 已经拥有的功能。

**最终目标：用户打开 Windows GUI，填写手机 IP 和 SOCKS5 端口，点击连接，即可通过 HEV 把 Windows 系统流量转发给手机的 SOCKS5 服务。点击断开后，Windows 原有网络配置必须安全恢复。**

现在开始检查工作区、初始化工程并实施第一阶段，随后继续推进 Windows 可运行版本。
