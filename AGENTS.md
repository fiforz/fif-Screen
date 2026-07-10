# FifScreen 项目协作规则

1. 每次修改前先读取当前 Git 状态，保留与任务无关的用户改动。
2. 用户可见功能完成后必须运行对应的 Windows、Android、协议和打包验证，不能只提交未验证源码。
3. 发布版本时统一更新 `VERSION`、`VERSION_CODE`、Windows 版本资源、驱动 INF 和 `CHANGELOG.md`。
4. 版本提交完成后，通过 SSH 远端 `git@github.com:fiforz/fif-Screen.git` 推送 `main` 和 `v<版本>` 标签。
5. 每个版本都在 GitHub Releases 发布版本说明、Windows 安装包和同名 `.sha256` 文件。
6. 不向仓库提交私钥、令牌、测试证书私钥、构建缓存或本机凭据。
7. 自动更新必须从 HTTPS GitHub Release 读取，并在运行安装包前验证 SHA-256。
