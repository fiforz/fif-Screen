# FifScreen

FifScreen 可以把 Android 手机或平板变成 Windows 的真实扩展显示器。Windows 会识别出一块独立显示屏，用户可以像使用普通外接显示器一样拖动窗口、播放视频和调整显示布局。

项目当前使用 USB 数据线传输画面：Windows 端通过 IddCx/UMDF 2 创建间接显示设备，采集扩展桌面后使用 H.264 低延迟编码，再通过 `adb reverse` 发送到 Android 端硬件解码显示。

> 当前发布的是开发预览版。显示驱动使用测试签名，安装前 Windows 必须已允许测试签名驱动，并且安全启动不能阻止该驱动。安装程序不会自行修改 BCD、安全启动或重启设置。

## 下载

请从 [GitHub Releases](https://github.com/fiforz/fif-Screen/releases) 下载最新的 Windows 一键安装包：

- `FifScreen-Setup-<版本>-dev-x64.exe`：Windows 开发预览版安装包；
- 同名 `.sha256` 文件：用于校验安装包完整性；
- Android APK 已封装在 Windows 安装包内，不需要单独下载。

安装包自带 Windows 主机程序、显示驱动、软件设备启动器、ADB 环境、Android APK、中文控制中心、自动更新和完整卸载脚本。目标电脑不需要预装 Visual Studio、WDK、CMake、JDK、Gradle、Android SDK 或 VC++ 运行库。

## 当前能力

- 在 Windows 10/11 x64 上创建真实扩展显示器；
- USB 调试连接 Android 手机或平板；
- 1920 × 1080、目标 50 FPS 的 H.264 低延迟画面；
- Windows 硬件 H.264 编码和 Android 硬件解码；
- 最新帧优先的有界管线，避免网络或解码变慢后持续累积延迟；
- 捕获 Windows 系统鼠标并显示在扩展屏画面中；
- 自动安装或升级安装包内的 Android APK；
- USB 断开后手动重新连接；
- 中文 Windows 控制中心；
- 启动时自动检查 GitHub Release，显示版本号、发布时间和更新日志；
- 下载更新后执行 SHA-256 校验，关闭相关进程并启动安装程序；
- 完整卸载驱动、软件设备、运行文件、日志和已连接手机上的 APK。

## 快速使用

### 1. 开启 Android 开发者模式

1. 打开手机或平板的“设置 → 关于手机/关于平板”。
2. 连续点击“版本号”或“内部版本号”约 7 次。
3. 输入锁屏密码，等待系统提示已经进入开发者模式。
4. 返回设置，进入“开发者选项”，打开“USB 调试”。

不同品牌的入口略有差异：

- 华为/荣耀：`系统和更新 → 开发人员选项`；
- 小米/红米：`更多设置 → 开发者选项`；
- 三星：`关于手机 → 软件信息 → 编译编号`，开启后在设置底部进入开发者选项；
- 找不到入口时，可在系统设置中搜索“版本号”“编译编号”或“USB 调试”。

### 2. 连接设备

1. 使用支持数据传输的 USB 线连接电脑和 Android 设备。
2. 手机上出现 USB 调试授权时，勾选“始终允许使用这台计算机进行调试”，然后点击“允许”。
3. 如果手机只显示充电，可把 USB 用途切换为“文件传输”后重新授权。

### 3. 启动扩展屏

1. 安装并打开 `FifScreen 控制中心`。
2. 点击“启动扩展屏”。首次启动会把安装包内的 APK 安装到已授权的 Android 设备。
3. 在 Windows 的“设置 → 系统 → 显示”中调整扩展屏位置、方向和缩放。
4. USB 重新插拔或更换设备后，点击“重新连接手机”。
5. 不再使用时点击“停止扩展屏”。

控制中心的“关于 → 使用教程”中也提供了完整中文操作说明。

## 自动更新

控制中心启动后，会在独立后台进程中读取：

`https://api.github.com/repos/fiforz/fif-Screen/releases/latest`

发现新版本时会显示当前版本、最新版本、发布时间和 Release 更新日志。用户确认后，更新器会：

1. 从 GitHub Release 下载对应的 x64 安装包；
2. 使用 Release 资产提供的 SHA-256 校验安装包；
3. 正常关闭 FifScreen 主机、软件设备启动器、控制中心和安装目录内的 ADB；
4. 以管理员权限启动新版本安装包，执行原目录覆盖升级。

更新器优先读取 GitHub Release API；API 超时或暂时不可用时，会回退到仓库内的静态更新清单。启动时两条路径都失败不会打扰用户，程序会在后台按退避间隔静默重试。用户主动点击“关于 → 检查更新”时会自动重试三次，全部失败后才提示检查网络环境。

Windows 控制中心必须以管理员身份运行。非管理员启动时只显示权限提示，点击确定后程序立即退出，不会继续创建显示设备或启动后台服务。

## 系统结构

| 模块 | 目录 | 作用 |
| --- | --- | --- |
| Android 客户端 | `android-client/` | 接收协议数据、MediaCodec H.264 硬解并显示画面 |
| Windows 主机 | `windows-host/` | 捕获扩展桌面、Media Foundation H.264 编码、USB 网络传输 |
| 间接显示驱动 | `windows-driver/FifIddDriver/` | 使用 IddCx/UMDF 2 向 Windows 提供扩展显示器 |
| 软件设备启动器 | `windows-driver/FifIddDeviceLauncher/` | 创建、查询和关闭 FifScreen 软件设备 |
| 传输协议 | `protocol/` | 控制通道、视频通道和有界数据包格式 |
| Windows 控制中心 | `scripts/fifscreen-control.ps1` | 中文启动、停止、重连、状态和更新界面 |
| 安装与维护 | `packaging/` | Inno Setup、驱动安装、自动更新和卸载 |

默认端口：

- `27183`：控制通道；
- `27184`：视频通道。

两个端口只监听 `127.0.0.1`，通过 ADB 反向端口映射与 USB 设备通信。

## 从源码构建

开发环境需要：

- Windows 10/11 x64；
- Visual Studio 2022，包含 MSVC C++ 工具链；
- Windows Driver Kit；
- CMake 和 Ninja；
- JDK 17；
- Android SDK；
- Inno Setup 6。

构建 Android 客户端：

```powershell
cd android-client
.\gradlew.bat clean assembleDebug testDebugUnitTest
```

构建完整开发版安装包：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-installer.ps1
```

输出位于 `artifacts/installer/`。安装器构建脚本会检查 Windows 二进制、驱动、APK、版本号、文件哈希和打包清单是否一致。

## 版本发布

项目使用语义化版本号，统一维护在 `VERSION` 和 `VERSION_CODE`。每次发布都应：

1. 更新版本号和 [CHANGELOG.md](CHANGELOG.md)；
2. 完成 Windows、Android、协议和安装包验证，并确认 `updates/latest-<通道>.json` 与安装包哈希一致；
3. 提交并通过 SSH 推送 `main`；
4. 创建 `v<版本>` Git 标签；
5. 在 GitHub Release 上传安装包和同名 `.sha256` 文件。

仓库内的 `scripts/publish-release.ps1` 可以在已登录 GitHub CLI 的环境中完成推送、打标签和上传 Release 资产。

## 已知限制

- 当前只支持一台 Android 设备和一块 FifScreen 扩展屏；
- 当前安装包和驱动仍是开发签名，不能替代正式的 Microsoft 驱动签名与 Authenticode 签名；
- 不同 Android 厂商的硬件解码缓冲策略不同，实际延迟会随设备变化；
- 电脑卸载时无法移除一台当前未连接手机上的 APK，需要在手机端手动卸载。

## 项目地址

- GitHub：<https://github.com/fiforz/fif-Screen>
- 问题反馈：<https://github.com/fiforz/fif-Screen/issues>
- 版本下载：<https://github.com/fiforz/fif-Screen/releases>

当前仓库尚未添加明确的开源许可证。在许可证确定之前，源码复用与再分发需要获得项目维护者许可。
