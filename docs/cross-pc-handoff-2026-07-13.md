# FifScreen 跨电脑项目交接说明（2026-07-13）

本文供在另一台电脑上接手本项目的 Codex 阅读。请先读取仓库根目录的 `AGENTS.md`，再按本文继续工作。

## 当前仓库基线

- 分支：`main`
- 当前提交：`5a15c6fb2d699d2a387bad560fcfd6ef13f52d99`
- 提交说明：`修复复制屏问题`
- 该提交已经位于 `origin/main`
- 编写本文前工作区为干净状态
- 当前版本仍为 `0.5.2`，本次没有发布新版本或创建新标签

不要覆盖与后续任务无关的用户改动。每次修改前必须先运行 Git 状态检查。

## 用户原始问题与处理顺序

用户报告两个问题，并明确要求按顺序处理：

1. 同一安装包在原 Windows 10 电脑上可以扩展屏幕，但在另一台 Windows 10 和一台 Windows 11 电脑上，Android 一直提示电脑没有提供扩展屏画面。
2. 选择“无线局域网”、输入四位 PIN 并应用后，Windows 日志仍显示 Host 以 USB 模式启动。

目前只处理并提交了第一个问题。第二个 LAN/USB 模式问题尚未整改，不要把第一个问题的结论误套到第二个问题上。

## 第一个问题的真实根因

最初从控制中心日志看到：

- 软件设备 Owner 正常运行；
- `SWD\FIFSCREENIDD\FIFIDDDRIVER` PnP 节点存在；
- 驱动服务为 `WUDFRd`；
- `problem_code=0`；
- Android 控制和视频连接最终仍等不到可捕获的扩展显示源。

进一步使用 Windows 的 `Microsoft-Windows-IndirectDisplays-ClassExtension-Events` ETW Provider 抓取 IddCx 跟踪后确认：

- `AdapterInitFinished` 成功；
- `MonitorCreate` 和 `MonitorArrival` 成功；
- 1920×1080 和 1280×720 模式有效；
- `AdapterCommit` 成功；
- `MonitorAssignSwapChain` 实际发生过。

因此根因不是驱动没有创建 Monitor，也不是 PnP 驱动启动失败。

失败电脑上的关键差异是 Windows 初始把 FifScreen 目标放入了“复制/克隆”拓扑：

- 内屏和 FifScreen 都映射到 `\\.\DISPLAY1`；
- 两个目标使用相同的 source mode 和 `(0,0)` 坐标；
- 内屏也被降到 1920×1080；
- FifScreen 没有成为独立、可供 GDI 抓取的扩展桌面源；
- 旧 Host 只通过 `EnumDisplayDevices` 查找名称中包含 FifScreen 的独立活动显示器，所以返回“显示器不存在”。

Windows 自带 `DisplaySwitch.exe /extend` 在该机器上没有可靠地修复拓扑。直接调用：

```text
SetDisplayConfig(0, nullptr, 0, nullptr,
                 SDC_APPLY | SDC_TOPOLOGY_EXTEND)
```

后，拓扑立即变为：

- 内屏：`3200×2000`，位置 `(0,0)`；
- FifScreen：`\\.\DISPLAY13`，`1920×1080`，位置 `(3200,0)`；
- Source 和 Target adapter path 都指向 `SWD#FifScreenIdd#FifIddDriver`；
- `EnumDisplayDevices` 随后也能看到 `FifScreen Indirect Display`。

这证明需要在控制中心启动链路中主动、可验证地应用扩展拓扑，而不能依赖不同电脑保存的显示拓扑状态。

## 已提交的整改方案

整改包含以下部分。

### 1. 设备启动器使用 DisplayConfig 识别拓扑

文件：`windows-driver/FifIddDeviceLauncher/src/main.cpp`

- 使用 `GetDisplayConfigBufferSizes`、`QueryDisplayConfig` 和 `DisplayConfigGetDeviceInfo`；
- 通过 `FifScreenIdd` / `FifIddDriver` 的 SWD adapter path 识别目标，而不是依赖本地化显示名称；
- `status` 新增以下机器可读字段：
  - `fifscreen_software_device_healthy`
  - `fifscreen_display_adapter_present`
  - `fifscreen_monitor_present`
  - `fifscreen_display_active`
  - `topology_active`
  - `topology_extended`
  - `topology_source_name`
  - Source/Target adapter path 和 monitor path
- 健康状态要求设备已启动、无 PnP 问题码且已经绑定驱动；
- 新增 `extend` 命令，调用 `SetDisplayConfig(... SDC_APPLY | SDC_TOPOLOGY_EXTEND)`；
- 如果已经处于正确扩展拓扑，`extend` 会直接返回 `ALREADY_EXTENDED`；
- 应用拓扑后会继续轮询，直到确认 FifScreen 成为独立扩展源。

`windows-driver/FifIddDeviceLauncher/CMakeLists.txt` 同时补充了 Windows 10 API 目标宏。

### 2. 控制中心先确保画面就绪，再启动 Host 和 Android

文件：`scripts/fifscreen-control.ps1`

- `Get-LauncherStatus` 解析新的健康和拓扑字段；
- 新增 `Wait-ExtendedDisplayReady`；
- 启动顺序现在是：
  1. 创建或复用软件设备；
  2. 等待 PnP 节点和驱动健康；
  3. 确认 FifScreen Monitor 已出现；
  4. 调用 Launcher 的 `extend`；
  5. 确认 FifScreen 已成为独立扩展源；
  6. 然后才启动 Windows Host；
  7. 最后配置/启动 Android。
- 初始化失败会显示具体驱动或拓扑诊断，不再错误地继续启动手机端；
- 状态栏区分：`画面已就绪`、`正在初始化`、`驱动异常`、`不存在`；
- 软件设备创建等待时间从约 10 秒提高到约 30 秒，兼容慢速 PnP 初始化和旧节点释放。

### 3. Host 使用现代拓扑定位 FifScreen

文件：`windows-host/src/screen_capture.cpp`

- Host 先用 DisplayConfig 查找 Target adapter path 属于 FifScreen 的活动路径；
- 只有 Source adapter 也属于 FifScreen、且存在独立 GDI source name 时，才认为它是可捕获的扩展桌面；
- 取得 `\\.\DISPLAYx` 后，再与 `EnumDisplayDevices` 的活动显示器记录匹配；
- 保留旧名称识别作为兼容路径；
- 克隆拓扑下不会把物理主屏误当作 FifScreen。

### 4. 用户说明

`README.md` 和控制中心教程现在说明：点击“启动扩展屏”会等待虚拟显示器并自动切换到扩展桌面。

## 已完成验证

在问题 Windows 10 电脑上完成了以下验证：

- Visual Studio Community 2026、MSVC、CMake、Ninja、DriverKit 和 Spectre 组件已安装并可识别；
- Windows Host 与 Device Launcher 使用 MSVC Release 成功编译和链接；
- Launcher `selftest` 四个 Owner/Device 状态组合全部通过；
- 新 Launcher 在真实设备上输出：
  - `fifscreen_software_device_healthy=true`
  - `fifscreen_monitor_present=true`
  - `fifscreen_display_active=true`
  - `topology_extended=true`
  - `topology_source_name=\\.\DISPLAY13`
- `extend` 在已扩展状态下返回 `ALREADY_EXTENDED`；
- 新 Host 实机输出并选中：

```text
displayconfig FifScreen target source=\\.\DISPLAY13 ... extended=true
display candidate name=\\.\DISPLAY13 string=FifScreen Indirect Display ...
selected capture display name=\\.\DISPLAY13 ... pos=3200,0 size=1920x1080
```

- PowerShell 脚本语法检查通过；
- 控制中心状态烟雾测试显示 `扩展显示：画面已就绪`；
- `fif-protocol-test` 通过；
- `fif-pairing-crypto-test` 通过；
- `git diff --check` 通过。

## 尚未完成或受限的验证

另一台电脑的 Codex 接手后应继续完成这些验证：

1. `fif-touch-injector-test` 在本次 Codex 受限桌面中返回 `InjectTouchInput error=5`。这是桌面注入权限问题，需要在真实交互式 Windows 会话中复跑。
2. 本机仓库没有恢复 `windows-driver/packages`，所以 WDK 驱动包没有重新编译。注意：本次最终提交没有修改 `FifIddDriver/Driver.cpp`，主修复不要求更换驱动 DLL。
3. 缺少可用 JDK、项目 Android SDK 和 Debug APK，所以 Android 单元测试、Lint 和 APK 构建未完成。
4. 缺少 Inno Setup，完整安装包构建未完成。
5. 未把新构建覆盖安装到 `D:\software\FifScreen`；只在源码构建目录和真实显示拓扑上验证。
6. 当时手机未保持 ADB 连接，因此没有完成“手机收到新 Host 视频”的最终端到端确认。

本次尝试安装 NuGet/JDK/Inno Setup 时，后续系统审批因工具额度限制被拒绝。不要用绕过审批的方式安装；在新电脑上应通过正常授权恢复工具链。

## 新电脑建议执行步骤

### 1. 获取并确认代码

```powershell
git status --short --branch
git log -3 --oneline --decorate
git rev-parse HEAD
```

确认至少包含提交 `5a15c6f`。保留所有与任务无关的用户修改。

### 2. 恢复开发环境

参考：

- `docs/dev-machine.md`
- `docs/driver-toolchain.md`
- `docs/android-build.md`
- `windows-driver/packages.config`

至少需要：

- Visual Studio Community 2026，MSVC 14.51；
- CMake、Ninja、MSBuild；
- Visual Studio DriverKit 与 Spectre libraries；
- NuGet，并恢复 `windows-driver/packages.config`；
- JDK、Android SDK/build-tools/platform-tools；
- Inno Setup 6。

不要提交工具目录、构建缓存、证书私钥或本机凭据。

### 3. 复跑 Windows 与协议验证

至少构建：

- `fif-host`
- `fif-idd-device-launcher`
- `fif-protocol-test`
- `fif-pairing-crypto-test`
- `fif-touch-injector-test`

然后运行 CTest，并在交互式桌面验证触控测试。

### 4. 验证真实扩展拓扑

启动软件设备后运行：

```powershell
fif-idd-device-launcher.exe status
fif-idd-device-launcher.exe extend
fif-idd-device-launcher.exe status
```

必须看到：

- 软件设备健康；
- Monitor 存在；
- `topology_extended=true`；
- Source adapter 和 Target adapter 均属于 FifScreen SWD；
- Source name 是独立的 `\\.\DISPLAYx`，不能与物理主屏相同；
- `EnumDisplaySettingsEx` 能取得非零分辨率和独立桌面坐标。

再启动 Host，确认日志出现 `selected capture display`，并在 Android 上确认真实画面和触控。

### 5. 完整项目验证

按照 `AGENTS.md` 要求完成：

- Windows 构建与测试；
- Android 单元测试、Lint、APK 构建；
- 协议与 PIN 加密测试；
- 驱动/Launcher 验证；
- Development 安装包构建和安装验证。

如果准备发布新版本，必须统一更新 `VERSION`、`VERSION_CODE`、Windows 版本资源、驱动 INF 和 `CHANGELOG.md`，并遵守标签、推送、Release、SHA-256 和更新清单规则。本次交接本身不代表已经发布新版本。

## 第二个问题的接手提示

第二个问题的症状是：在控制中心选择 LAN、输入四位 PIN 并点击“应用 PIN 并等待连接”后，日志仍显示 Host 以 USB 模式启动。

接手时优先检查以下链路，但不要在没有证据时直接改代码：

1. GUI 中 `lanModeRadio.Checked` 是否在按钮事件执行时为真；
2. `Get-RequestedConnectionSettings` 返回的 `Mode`；
3. `Start-FifScreen` 传给 `Ensure-Host` 的 `Mode`；
4. `Ensure-Host` 检测现有 `fif-host` 命令行时，`Get-HostConnectionMode` 是否因 CIM 权限/查询失败而默认返回 USB；
5. `Start-Process` 最终参数是否确实为 `--transport lan`；
6. 旧 Host 进程是否未退出，或存在多个不同版本的 `fif-host.exe`；
7. Android 启动参数 `fifscreen_connection_mode` 是否为 `lan`；
8. LAN 模式下是否仍错误设置了 `FIF_ADB` / `FIF_ADB_SERIAL` 或执行了 ADB reverse。

建议先增加结构化日志，明确记录：请求模式、现有 Host PID/路径/完整命令行、停止结果、新 Host PID/完整参数和最终监听地址，再根据证据修复。

## 安全与清理注意事项

- 不要提交 ETL、安装日志、构建目录或临时诊断程序；这些应位于被忽略的 `artifacts/` 或 `build/`。
- 不要提交测试证书私钥、PFX、令牌或其他凭据。
- 不要用 `git reset --hard` 或 `git checkout --` 清理用户改动。
- 自动更新仍必须使用 HTTPS GitHub Release，并在运行安装包前验证 SHA-256。

