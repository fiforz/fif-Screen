# FifScreen Windows 打包说明

`scripts/build-installer.ps1` 用于生成单文件、带版本号的 x64 Windows 安装包。

安装包包含：

- 静态链接的 Windows 主机程序和软件设备启动器；
- FifScreen 间接显示驱动；
- Android Platform Tools ADB 运行环境；
- 与产品版本一致的 Android APK；
- 中文控制中心和维护脚本；
- GitHub Releases 自动更新配置；
- 完整卸载清理流程。

目标电脑不需要安装 Visual Studio、WDK、CMake、Gradle、JDK、Android SDK 或 VC++ 运行库。

## 构建开发版

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-installer.ps1
```

输出位于 `artifacts\installer`。安装包内默认写入 FifScreen 官方 GitHub Releases API 地址。首次点击“启动扩展屏”时，控制中心会向已授权 USB 调试的 Android 设备安装随附 APK；后续 Windows 版本升级也会升级手机上的旧版 APK。

开发版包含测试签名显示驱动。安装程序需要用户确认开发驱动警告；静默安装时必须添加 `/ALLOWTESTDRIVER=1`。安装程序只导入与驱动目录匹配的公开测试证书，不会修改 BCD、安全启动或重启设置。

## 正式版构建门槛

正式版必须提供：

1. Microsoft 签名的 FifScreen 驱动包；
2. 使用发布密钥签名的 Android APK；
3. 用于安装程序和 Windows 二进制的 Authenticode 代码签名证书；
4. HTTPS GitHub Releases API 或兼容更新端点。

当驱动目录、APK、更新端点或签名输入不符合正式版要求时，构建脚本会直接拒绝打包。

每个版本都使用同一个 Inno Setup `AppId`，因此新版本会在原目录覆盖升级，并在系统中保留唯一卸载项。

## 更新与卸载约定

已安装的控制中心启动后读取 `update.json`，通过 GitHub 最新 Release 检查版本。更新器验证产品版本、x64 安装包名称、HTTPS 下载域、SHA-256，以及正式通道要求的 Authenticode 签名。

运行更新安装包之前，更新器会关闭 FifScreen 主机、设备启动器、控制中心和安装目录内的 ADB。安装程序还会再次执行同一关闭脚本，保证覆盖升级时文件没有被占用。

卸载程序会停止运行时、删除 FifScreen 软件设备和驱动包、删除只由安装程序导入的证书，并清理日志。它还会从所有当前已连接且已授权的 Android 设备中删除 APK 和 ADB 反向端口。电脑卸载时没有连接的手机无法被远程清理，需要在手机端手动卸载 APK。

随安装包分发的 Inno Setup 简体中文翻译采用 MIT 许可证，许可证文本位于 `packaging\licenses`。
