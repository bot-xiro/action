# Bilibili MiniApp for X6PRO

在有道词典笔 X6PRO（RK3562，Buildroot Linux，非安卓，960×266 长条屏）上从零开发的 Bilibili MiniApp。
使用 Bilibili 网页端（Web）API 获取数据，使用 aiot-vue-cli 构建，页面代码手动编写（不使用 demo 模板）。

## 红线

- 严禁修改实机上的任何系统文件（/system、/vendor 等）！所有设备端操作仅限于 `/userdisk/`。
- 严禁卸载、删除或修改实机上已有的任何软件，包括 `/userdisk/` 下已有的 `.amr` 包。
- 电脑端所有操作、下载、编译产物仅限于项目根目录内。
- 所有 adb 命令使用 `platform-tools/` 中的 adb（`ADB_CMD` 变量），禁止设置 ANDROID_SDK_HOME。
- git/gh 必须通过绝对路径调用（`GIT_CMD` / `GH_CMD`）。
- 主要工作仓库为公开仓库 `https://github.com/bot-xiro/action`（协作者 soarnext）。
- 本地无 WSL/Docker/Linux 虚拟机，所有 Linux 任务（交叉编译等）必须通过 bot-xiro/action 的 GitHub Actions 远程执行。
- 设备日志在 `/data/applog`，只能 `cat` 只读查看，严禁修改/删除/pull。
- 测试包使用自定义文件名 `bilibili.amr`，不得覆盖设备原有内容。

## 文档

- 环境摸底: [docs/X6PRO_ENV.txt](docs/X6PRO_ENV.txt)
- 阅读笔记与骨架设计: [docs/STUDY_NOTES.md](docs/STUDY_NOTES.md)
- 适配差异与崩溃修复: [docs/PORTING_NOTES.md](docs/PORTING_NOTES.md)
- 亮屏方法汇总: [docs/SCREEN_WAKE.md](docs/SCREEN_WAKE.md)
- 测试日志: [docs/X6PRO_TEST_LOG/](docs/X6PRO_TEST_LOG/)
