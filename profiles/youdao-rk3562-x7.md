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

## 未验证项
- 输入法 confirm/cancel 回调具体数据形态 (代码已兼容 string / {value} / {text})
- bilibili 搜索风控 (-412) 在设备网络上的触发概率
- `<image>` 网络图 (i0.hdslb.com 防盗链, 已带 Referer 但 image 组件无法设 header, 可能裂图)
