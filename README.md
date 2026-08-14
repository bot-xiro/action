# Bilibili MiniApp for X6PRO

有道词典笔 X6PRO（RK3562, Buildroot Linux, 960×266 横屏）上的 Bilibili 小程序。

- 首页热门列表 → 视频详情 → 播放页（自研 GStreamer 原生播放器，RK MPP 硬解）→ 搜索 → 设置
- 原生模块 `gstplayer`（native/gstplayer，JQuick 绑定）经 GitHub Actions 交叉编译 aarch64
- 前端 src/ 由 aiot-vue-cli（qjsc QuickJS 字节码）打包为 .amr

## 构建

GitHub Actions（本仓库 push main / workflow_dispatch）自动构建：

- `libs/libjsapi_gstplayer.so`（aarch64 交叉编译）
- `bilibili.amr`（含 .so）

产物见 Actions artifacts：`bilibili.amr`、`libjsapi_gstplayer.so`。

本地构建（可选，Windows 可用）：`aiot-cli -q -p`（需全局 aiot-vue-cli@1.0.32）。

## 部署（真机）

见 docs/VERIFY_FLOW.md（本地文档，不上传）。要点：

1. push amr 到设备 `/userdisk/bilibili_test.amr`（自定义文件名，不覆盖原包）
2. `miniapp_cli install` → **kill miniapp 进程**（加载新 .so）→ `miniapp_cli start 8001812345678901`

## 目录

- `src/` — 前端（Vue 页面 + utils：WBI 签名 api、md5、storage、settings）
- `native/gstplayer/` — 自研 GStreamer 播放器原生模块（含 third_party/iot-miniapp-sdk）
- `.github/workflows/build-miniapp.yml` — CI 构建
- `docs/` — 本地文档（gitignore，不上传）：PROJECT_SUMMARY / VERIFY_FLOW / SCREEN_WAKE / DEV_LOG / X6PRO_ENV / STUDY_NOTES
- `deving.md` — 根目录开发日志

## 协作约定

见 AGENTS.md（真机验证流程、亮屏约定、部署流程）。
