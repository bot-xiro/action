# 设备画像: 有道词典笔 (RK3562 平台)

探测日期: 通过 ADB 只读探测 (`platform-tools/adb.exe`)

```yaml
profile_id: youdao-rk3562-melon
model: Rockchip RK3562 MELON LP4 V10 Board
hostname: YoudaoDictionaryPen-215
firmware:
  os: Buildroot 2021.11 (Linux 5.10.160, 2024-12-12 编译)
  miniapp_runtime: "4.3.5"   # /etc/miniapp/resources/local_packages.json version
abi:
  machine: aarch64
  cpu: 4x Cortex-A53 (0xd03)
  libc: glibc (aarch64 buildroot)
screen:
  panel: DSI-1 480x960 @59.88Hz (原生竖屏)
  logical: { width: 960, height: 266 }
  direction: 270
  touch: { tp_direction: 270, tp_xoffset: 113, tp_yoffset: 0, xoffset: 0, yoffset: 107 }
  fb: /dev/fb0
  来源: /etc/miniapp/resources/cfg.json
input_method:
  类型: 系统 mini-app (IME), appid 8001666679481944, Category IM_PANEL_DICT
  调用方式: import globalModule from 'global'; new globalModule.Global()
  API: startTextEdit(JSONString) -> 同步返回 UUID; closeTextEdit(uuid);
       textEditFinished.on(handler) 信号; clearTextEditContent
  证据: strings /etc/miniapp/jsapis/libjsapi_export.so 中确认上述符号
  global_text_edit: 待真机验证
package:
  appid: "8001812345678901"   # 已在设备 /userdisk 出现过的自有 appid
  version: "0.1.0"
  start_page: index
安装:
  install: adb push x.amr /tmp/x.amr && adb shell miniapp_cli install /tmp/x.amr
  start: adb shell miniapp_cli start <appid> --<page>
构建: 本地不可构建, 通过 GitHub Actions (ubuntu-latest, node 18, pnpm, aiot-vue-cli)
```

## 已安装 appid 参考
- `/userdisk` 下存在 `8001792669600001.0_2_6.amr`、`8001812345678901.0_0_3.amr`、
  `bilibili_test.amr`、`bilibili_test2.amr`、`libjsapi_gstplayer.so`

## 已验证补充 (v0.1.2 / v0.1.3, 2026-08-28 真机)
- textEditFinished 回调: 参数[0] 为去横线的会话 UUID 裸串, 参数[1] 为
  {"text":...,"cursorIndex":N,"editConfirmed":bool}; 代码按全参数扫描处理
- $falcon.jsapi.http.request: 原生日志 tag debug_httpApi; resolve 值为响应体本身
  (无 statusCode 包装); headers 只认字符串数组 ["Key: value"], 对象形式被丢弃
- timeout 单位为秒 (http.md)
## 未验证项
- bilibili 搜索风控 (-412) 在设备网络上的触发概率
- `<image>` 网络图 (i0.hdslb.com 防盗链, 已带 Referer 但 image 组件无法设 header, 可能裂图)

## 已验证补充 (v0.6.x, 2026-08-29 真机/探测, gst 1.22.0)
- 逻辑→物理映射锚点来自触控 (references/ADB手册附录B):
  `touchX = displayY + 107`, `touchY = 959 - displayX` → 面板 480x960, direction=270
  → video KMS rect: px = ly + 107, py = 959 - (lx + lw), pw = lh, ph = lw
- gst-1.x videoflip method 枚举: 0 identity / 1 90r / 2 180 / 3 90l / 4 horiz / 5 vert;
  "4 = clockwise" 是错记忆 (4 是横向镜像). 竖屏面板+direction=270 该用 3 (90l).
- KMS: plane 54 = Primary Esmart0-win0 (zpos=0, UI), plane 76 = Esmart1-win0 (zpos=2, 视频默认在上),
  90/104 更高; 所有 plane rotation 只支持 rotate-0/reflect-y.
- kmssink 本固件 (gst-launch 实测): plane-properties="props,zpos=(guint)0" 可解析;
  分层验证必须启动 App 界面后再播 (裸 gst-launch 无 UI 参考, 结论无效).
- ALSA: /etc/asound.conf 的 default pcm = speaker -> spk_softvol -> resample_spk -> hw:1,0
  (aw883xx 智能功放主喇叭); card0 rk817 是耳机/麦克风路径; alsasink 用默认设备即可.
- 音频链: `qtdemux ! aacparse ! faad ! audioconvert ! audioresample ! alsasink` 本地可解 AAC
  (faad rank 128 存在), 但 decodebin 选 pad 不区分 use-first.
- gstplayer 宿主进程教训:
  * stopDaemon 不能在持锁态 join 会向 JS 派发的读线程 (互等死锁 → 看门狗重启)
  * 宿主必须 SIGPIPE=SIG_IGN, 否则 daemon 先退出后向 PIPE 写 QUERY 会杀宿主
- miniapp_cli capture 只截 UI 帧缓冲, 截不到 KMS video plane.

