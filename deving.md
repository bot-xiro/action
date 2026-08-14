# deving.md — 开发日志（Bilibili MiniApp for X6PRO）

约定：每完成一个功能模块追加一条记录（日期 / 功能 / 结果 / 问题）。代码提交前同步本文件与 `docs/`。

---

## 2026-08-14（会话恢复日）

### 功能：环境恢复 + 项目接管（0a-0d + 步骤 1）

- **0a 环境**：找到 `platform-tools/adb.exe`、`git`（C:\Program Files\Git）、`gh`（GitHub CLI，登录 soarnext，repo+workflow 权限）；沙箱禁止写用户级 .gitconfig，改为 `tools/env.ps1` 用 `GIT_CONFIG_COUNT` 环境变量注入 openssl TLS 后端 + socks5://127.0.0.1:10808 代理；**严格使用 $ADB_CMD/$GIT_CMD/$GH_CMD 引用工具**。
- **0b 摸底**：`docs/X6PRO_ENV.txt` 完整记录。设备 = YoudaoDictionaryPen-215，Linux 5.10.160 aarch64，**RK3562**（4×Cortex-A53），1GB RAM，屏幕 **960×266**（direction=270）；GStreamer 1.22（rockchipmpp mppvideodec 硬解、waylandsink/kms/flv/hls/rtmp/soup 插件，**无软解 avdec**）、ffmpeg 4.4.1、miniapp_cli、unzip/zip/tar 均在；`aiot-vue-cli` 设备上无（主机侧工具）。
- **0c 研究**：克隆 7 个参考仓库到 `references/`（haasui-docs、miniapp、miniapp-template、aiot-vue-cli、WPE4YDPv2(dev)、bilibili-api-collect-mirror、youdao-pen-loli）；研读 HaaS UI 文档、JQuick 原生模块机制、.amr 打包格式（ZIP+manifest.json+cert）、系统 JSAPI 清单（libjsapi_export.so：videoPlayer/soundPlayer/volume/wifi…，**无 http/sqlite**，但框架提供 `$jsapi/http`）；`docs/STUDY_NOTES.md` 全文记录。
- **0d 仓库**：发现 `bot-xiro/action` 已有前期完整工程（src/ 五页面 + native/gstplayer 自研播放器 + .github/workflows/build-miniapp.yml，appid 8002000000000001）。**决定不覆盖**：把仓库克隆内容合并为本地工作区根目录（保留 git 历史），继续开发。补齐 .gitignore（references/.tmp/tools/ 本地不推送）、README、deving.md、docs/。
- **步骤 1**：adb kill-server 后设备上线（序列号 9E11700007500215），`echo "device connected"` ✅。
- **本地构建链验证**：aiot-vue-cli@1.0.32 已全局安装（win32 qjsc 自带），`aiot-cli -V` = 1.0.32 ✅。

### 结果
- 环境、文档、仓库、设备连接全部就绪；项目代码完整接管（src/、native/、.github/）。
- 设备当前未安装本 app（pkg/8002000000000001 已不在），需重新构建安装。

### 问题 / 待办
- [ ] 本地/CI 构建 bilibili.amr 并装机验证（步骤 2）
- [ ] 播放页真机验证（步骤 3）：自研 gstplayer 在 X6PRO 上的实际表现
- [ ] 步骤 4 功能逐项回归：首页热门 → 详情 → 播放 → 搜索 → 设置/历史
- [ ] 前期 docs/（PROJECT_SUMMARY/VERIFY_FLOW/SCREEN_WAKE/DEV_LOG）随会话丢失，本次重建
- [ ] AGENTS.md 中日志路径为 /usrdata/applog/，实测为 /data/applog/（已记录到 VERIFY_FLOW.md）
