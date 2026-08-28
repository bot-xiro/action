# 实验性云端部署仓库

⚠️ **警告：这是一个实验性云端部署仓库**

## ⚠️ 警告

本仓库仅用于**实验性云端部署测试**，可能包含以下内容：

- 🧪 **测试性功能** - 尚未稳定，可能随时变更或移除
- 🔬 **实验性 API** - 正在验证中的新接口
- 🛠️ **调试代码** - 包含大量调试日志和测试代码
- 🚧 **未完成功能** - 部分功能尚未完善，不建议用于生产环境

## 🚫 使用限制

- **严禁用于生产环境**
- 功能可能随时变更、破坏或移除
- 数据可能随时丢失或重置
- API 接口可能不兼容或随时变更

## 📋 当前测试内容

- ✅ 系统键盘测试 (`global.startTextEdit()`)
- ✅ 输入法测试 (`ime-test`)
- ✅ 搜索功能
- ✅ 视频播放 (`gstplayer`)
- 🔧 输入法回调处理
- 🔧 剪贴板监听
- 🔧 存储检查与轮询

## 🛠️ 技术栈

- **运行环境**: 有道词典笔 Falcon mini-app
- **构建工具**: aiot-vue-cli
- **原生模块**: gstplayer, global
- **构建方式**: GitHub Actions 云端构建

## ⚡ 快速开始

```bash
# 克隆仓库
git clone https://github.com/bot-xiro/action.git

# 安装依赖
npm install

# 本地构建 (需要 aiot-vue-cli)
npm run build

# 云端构建: 推送到 main 分支自动触发 GitHub Actions
git push origin main
```

## 📱 部署到设备

```bash
# 下载云端构建产物
gh run download <run-id> -n bilibili.amr -D .tmp/art

# 推送到设备
adb push .tmp/art/8001812345678901.0_0_3.amr /userdisk/bilibili_test.amr

# 安装
adb shell miniapp_cli uninstall 8001812345678901
adb shell miniapp_cli install /userdisk/bilibili_test.amr
adb shell miniapp_cli start 8001812345678901
```

## ⚠️ 免责声明

**本仓库代码仅供学习和实验参考，不对任何直接或间接损失负责。使用前请充分测试，确认符合您的需求后再决定是否采用。**

---

**最后更新**: 2026-08-23  
**维护者**: 实验性项目组