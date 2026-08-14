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
- [x] 首次装机验证（13:35）：`miniapp_cli install` ret:0 → 启动成功 → **首页热门列表加载 20 条**，封面图从 hdslb.com 正常拉取；加载的 .so 为本次 Action 产物（pkg libs/libjsapi_gstplayer_386948451.so）
- [x] **发现 appid 冲突**：8002000000000001 实为系统媒体播放器 appid（/userdisk/8002000000000001.0_0_1.amr 为 7-26 系统文件，早于本项目 8-06 起步；商店检查返回 rollingBack:407 有回滚压力）→ **已更换为 8001812345678901**（v0.0.2）
- [x] **发现构建缺陷**：build 未加 -q → amr 内是 .js 而非 .js.bin，设置页模块报 `settings.js.bin open failed`（框架按 .js.bin 查找）→ build 脚本改为 `aiot-cli build -q -p`

---

## 2026-08-14（装机验证 + 全功能真机通过）

### 功能：步骤 2/3/4 核心验证

- **构建**：Action 构建 ✅（1m37s）；本地构建被沙箱 EPERM 拦截（spawn 限制）→ 按提示词统一走 Action。
- **首次装机（T1）**：install ret:0 → 首页热门 20 条加载 ✅ → 发现 2 个问题（见上 [x] 两项）。
- **重装（T2，用户真机操作 + 视觉确认，13:42）**：
  - ✅ 首页热门（20 条，触底加载分页逻辑在）
  - ✅ 详情页（playUrl 预取缓存命中，720P H.264 codecid=7）
  - ✅ **播放页：open ret:true → zpos=3 → play OK → stateChanged playing，视频画面正常显示**（自研 gstplayer：souphttpsrc+mountaintoys.cn CDN 直连绕 403 → qtdemux → h264parse → mppvideodec 硬解 → waylandsink）
  - ✅ 搜索页（内置软键盘 + WBI 签名 + httpGet 带 UA/Referer）
  - ✅ 设置页（播放器模式切换 + storage 持久化）
  - ✅ 详情页评论（WBI 评论接口 + 置顶/热评合并）
  - ✅ 全日志 0 FATAL / 0 ERROR
- **工程改进**：原生日志改走 local7 设施（进 YD_PEN_APP.log，便于崩溃排查，PORTING_NOTES C3）；清理 app.js 探测残留代码 → v0.0.3 构建中。

### 结果
- **Bilibili MiniApp 在 X6PRO 上全功能可用**（热门/详情/播放/搜索/设置/评论/搜索历史）。
- 交付物：deving.md、docs/（X6PRO_ENV、STUDY_NOTES、PROJECT_SUMMARY、VERIFY_FLOW、SCREEN_WAKE、PORTING_NOTES、DEV_LOG、X6PRO_TEST_LOG/）、bilibili.amr（构建产物）。

### 问题 / 待办
- [ ] v0.0.3（原生日志 local7）装机回归（T3）
- [ ] 首页为"热门"而非个性化推荐（登录态 rcmd 依赖 cookie，暂不引入；决策记录于 PROJECT_SUMMARY）
- [ ] 登录（QR）为预留实现：cookie 已可存取，但 httpGet 尚未带 cookie，真正登录后历史/收藏需 native 支持 cookie
- [ ] 首次装机时系统曾自动清理 pkg（会话开始 13:03 观测到）——新 appid 是否被系统定期清理需长期观察

---

## 2026-08-14（播放器重构：全屏 + 悬浮控制栏 + 多轮真机修复）

### 功能：用户需求"视频全屏铺满(不变比例) + 控制栏悬浮在视频上方"

- **架构**：视频链 videoscale→capsfilter(等比内容尺寸)→videobox(黑边补画布)→gdkpixbufoverlay
  (悬浮控制栏)→videoconvert→kmssink；控制栏由原生 cairo 渲染 ARGB 条带(76×960)合入视频帧，
  JS 只做触摸命中（触摸输入与 plane 无关）。
- **用户已确认**：横屏视频比例正常、控制栏方向/文字/时间/颜色正确、拖动方向正常。

### 多轮修复（真机实证定位，详见 docs/PORTING_NOTES.md C5-C7）
- C5 闪退：cairo/gdk-pixbuf 符号懒解析 → dlopen+RTLD_GLOBAL 预热（libdrm→cairo→gdk-pixbuf 三连）。
- C6 掉帧：全画布合成→条带化 + 隐藏时 pixbuf=NULL（GValue）→ 零合成。
- C7 多项：rk videoscale 无 force-aspect-ratio（gst-inspect 实证）→ videobox 补边；
  ARGB32(BGRA)↔gdk-pixbuf(RGBA) → R/B 交换；文字 rotate→反射矩阵；快进/快退三角方向；
  gdkpixbufoverlay 行序反转（红蓝测试条"左蓝右红"实证）→ 缓冲行倒置。
- 时长：原生 getDuration 恒 0 → B 站 API timelength；字体：注册设备字体到 FcConfig。

### 结果
- 播放器视觉与交互（比例/控制栏/时长/文字/拖动）已达标；**seek 与竖屏 9:16 仍在修复中**。

### 问题 / 待办（当前）
- [ ] **seek 失效（位置回退）**：FLUSH ret=1 但回退；非冲刷 ret=0。已排除 FLUSH 标志/动态
  caps/Accept-Ranges 头；疑 videobox/内容 caps 链影响 seek 事件传播。→ `native/testseek`
  设备端 4 变体实验定位中（当前卡启动 SIGILL：独立进程 gst_init 崩溃，全量 dlopen 预加载
  后仍 SIGILL，LD_BIND_NOW 无符号报错——排查中）
- [ ] **竖屏 9:16 视频**：重建机制已实现（pad-added 尺寸核对 → start() 预滚后重建；修复了
  teardown 清零画布尺寸的 bug）待真机验证（应显示 266×150 内容 + 上下大黑边，不拉伸）
- [ ] 视频左右镜像疑虑：pixbuf 路径镜像已实证并补偿；视频本体待带字幕视频复核
- [ ] testseek SIGILL → 完成后 4 变体 seek 结论 → 修复 seek → 全量回归

### 交付物
- 源码（native/ + src/ + .github/）、bilibili.amr（CI 产物）、testseek（CI 产物）、
  docs/（X6PRO_ENV、STUDY_NOTES、PROJECT_SUMMARY、PROGRESS_SUMMARY、VERIFY_FLOW、
  SCREEN_WAKE、PORTING_NOTES、DEV_LOG、X6PRO_TEST_LOG/）、deving.md

---

## 2026-08-14（顶部标题条 + 自动隐藏修复）

### 功能：用户需求"添加顶部显示标题" + "控制栏自动隐藏行为修正"

- **顶部标题条**：新增第二条 gdkpixbufoverlay（vtitleoverlay_）+ 独立 ControlBar 实例
  （kind="title"）：半透明黑底条带（用户空间 y 0~40）+ 左对齐标题文字（size 20）。
  - ControlBar::init 增加 kind 参数（"bar"/"title"），条带几何按 kind 计算
    （title：portrait 条带=画布 x∈[0,40] y 全幅；landscape 对称）。
  - 管线链：...→ voverlay_(控制栏) → **vtitleoverlay_(标题)** → vconvert2_ → sink。
  - refreshBar 双条带同频刷新：barTitle_ 为空时不挂 pixbuf（drawTitle 会画黑底，
    空标题必须移除叠加，避免顶部黑条）。
  - setBarState 新增 title 字段解析（JS_ToCStringLen 兼容中文 UTF-8）。
  - player.vue pushBarState 传 title: this.title（详情页 navTo 已带 title）。
- **自动隐藏修复**：根因——bar 区交互一律 cancelBarHide() 且空白点击不恢复
  scheduleBarHide() → 控制栏永不自动隐藏（用户反馈"控制栏不可以自动隐藏"）。
  修复：bar 区空白点击 / 轨道点击 / 轨道拖动均重置自动隐藏计时（6s）。

### 结果
- 待 Action 构建 + 真机验证：顶部标题显示、自动隐藏恢复、回归（seek/竖屏/横屏/拖动）。

### 问题 / 待办
- [ ] 真机验证顶部标题条（位置/文字/中文渲染）与自动隐藏恢复
- [ ] 回归：seek、竖屏 9:16、横屏比例、控制栏交互

---

## 2026-08-14（自动隐藏真根因 + 控制栏放大）

### 真机反馈：标题 ✅，但控制栏 6s 后"不动了"却没消失

- **根因（日志+代码实证）**：`bar hidden (overlay removed)` 已打印，但画面定格——
  gdkpixbufoverlay 的 pixbuf 属性类型是 **GDK_TYPE_PIXBUF**（GObject 子类），而移除代码
  `g_value_init(&gv, G_TYPE_OBJECT)`（父类）→ `g_object_set_property` 类型不兼容
  → **静默失败**（GLib critical 不进 local7 日志），旧 pixbuf 保留在叠加层。
  即自动隐藏的 JS 状态机一直正常，是原生移除从未生效（C6"零合成"结论未在隐藏路径
  真机验证过）。
- **修复**：新增 `clearOverlayPixbuf()`——用 `g_object_class_find_property` 取 pixbuf
  属性真实类型（G_PARAM_SPEC_VALUE_TYPE）初始化 GValue 再 set_property，三处
  （bar 隐藏 / title 隐藏 / title 变空）统一走此函数。

### 控制栏放大（用户需求"把按键和 seek 做大一点"）

- 布局（用户空间 960×266，native bargeom 与 JS BAR 常量同步）：
  - 进度轨道：y 202→196、高 14→22、圆角 7→10、圆点 8→11
  - 按钮行：y 236→226、高 26→36；图标 7→10、播放圆底 13→17
  - 时间/返回文字 16/14→18/16；命中区放宽（back 24-140、sbk 330-430、
    play 448-512、sfw 530-630）

### 结果
- 待 Action 构建 + 真机验证：自动隐藏真正消失、控制栏加大后布局/命中正确。

---
