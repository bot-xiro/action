# AGENTS.md —— 项目约定（Bilibili MiniApp for X6PRO）

本文档为 AI 协作约定，供每次开发会话（本机与云端）在开始工作前读取并遵守。

## 项目简介

有道词典笔 X6PRO（RK3562, Buildroot Linux, 非安卓, 播放页视口 960×266）上的 Bilibili 小程序：
首页热门列表 → 详情页 → 播放页（自研 GStreamer 原生播放器 `gstplayer`）。

## 文档目录约定（核心）

**`docs/` 是项目的文档目录，任何代码工作都以其为准、并保持同步：**

1. **动手前必读** `docs/`：PROJECT_SUMMARY.md（项目总结/架构/踩坑）、VERIFY_FLOW.md（真机验证流程）、SCREEN_WAKE.md（亮屏约定）等，先了解现状与既定决策，再开始编码。
2. **每次提交代码（git commit）前必须同步更新 `docs/` 文档**，做到"代码与文档同行"：
   - 常规阶段开发 → 在 `docs/DEV_LOG.md` 追加当日记录（做了什么/验证结论/踩坑）。
   - 重大变更（架构、关键决策、接口、目录结构、环境信息）→ 同步更新 `docs/PROJECT_SUMMARY.md` 相应章节。
   - 涉及验证协作/亮屏/环境的变更 → 同步更新 `docs/VERIFY_FLOW.md`、`docs/SCREEN_WAKE.md`、`docs/X6PRO_ENV.txt`。
3. 若一次提交同时包含代码与 `docs/` 变更，提交信息中说明文档更新部分。

## 其他必须遵守的既有约定（详见 docs/）

- **真机验证**（VERIFY_FLOW.md）：设备端操作一律由用户手动执行，开发侧**不注入触摸/按键事件**，只通过 `adb shell "tail -200 /usrdata/applog/YD_PEN_APP.log"` 调日志验证；截图（`miniapp_cli capture`）仅辅助。
- **亮屏处理**（SCREEN_WAKE.md）：**禁止用指令/代码唤醒屏幕**（曾致事故）；屏幕熄了 → 对话框请用户按电源键。
- **docs/ 仅存本地**：`.gitignore` 已排除 `docs/`，该目录不上传云端（保持此约定，除非用户明确要求变更）。
- **部署流程**：CI 下载 amr → `adb push` `/userdisk/` → `miniapp_cli uninstall/install/start`；**install 后需 kill miniapp 进程才加载新 .so**。
- **播放页视区**：960×266（设备真实视口；hole 挖洞 + 控制栏 absolute 顶层 z-index 999，播放/暂停按钮屏幕中央）。
- 设备上有原有 amr 包（如 8001792669600001.0_2_6.amr、loli.amr），测试包必须自定义文件名，不得覆盖/删除原包。