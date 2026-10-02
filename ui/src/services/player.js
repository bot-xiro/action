// 播放器原生适配层 v2 (全重写)
//
// 原生 gstplayer 模块 (native/gstplayer): 宿主进程只做 gstplayerd 守护进程
// 生命周期管理与行协议收发, gstreamer 全部在独立子进程中运行.
//
// 跨层契约 (pages/player/player.vue 只依赖本模块):
//   isSupported()                    固件是否具备播放能力
//   open(url)                        打开网络流 (UA/Referer 原生携带,
//                                    视频矩形由设备侧按分辨率自动适配 UI 带)
//   start() / pause() / resume() / close()
//   seek(ms)                         跳转, 毫秒
//   getPosition() / getDuration()    毫秒 number (失败 0)
//   getVideoSize()                   {width, height} 未知时 0
//   onState(handler) / offState(handler)
//                                    订阅原生状态串:
//                                    "opening"/"ready"/"play"/"pause"/
//                                    "eos"/"closed"/"error: xxx"

import { gstPlayer } from 'gstplayer'
import { bilinet } from 'bilinet'

const LOG = '[player] '

let stateSubscribed = []
let nativeSubscribed = false

export function isSupported() {
  return !!(gstPlayer && typeof gstPlayer.open === 'function')
}

// ---------------- 播放时防息屏 ----------------
//
// 息屏机制: 系统按输入事件空闲计时 (真机实测 ~10s 无触控即息屏), 播放中
// 用户不碰屏幕就会黑掉 (视频还在播). 用 bilinet.exec 定期注入一条无害的
// `send_event touch move` 输入事件重置计时器 —— move 不产生点击, 不干扰 UI.
// 命令在真机上验证后调整 (若 move 不重置计时器, 换 press/release 短按).

/** 注入一次防息屏输入事件 (同步, ~10ms). exec 缺失/失败静默.
 *  真机实测: send_event 支持 action=press/release/slip (无 move);
 *  `touch slip` 连续 28s 每 4s 一次可让屏幕常亮, 且不产生点击副作用. */
export function keepAwakeTick() {
  if (!bilinet || typeof bilinet.exec !== 'function') return
  try {
    bilinet.exec('send_event touch slip 240 479')
  } catch (e) {
    console.log(LOG + 'keepAwake exec failed: ' + (e && e.message ? e.message : e))
  }
}

/**
 * 点亮/保持屏幕 (系统 hal-screen, 手册《有道词典笔 ADB 控制手册》§10.4 实证).
 * 为什么不用 send_event 合成触摸: 合成触摸只在"刚黑"的浅睡下有效,
 * 深睡(黑几分钟)后完全唤不醒(实测), 而 hal-screen on 任何状态都能点亮.
 * 注意: 它内部会注入一次触摸激活面板, 所以调用方要还原控制条显隐状态.
 */
export function screenOn() {
  if (!bilinet || typeof bilinet.exec !== 'function') return false
  try {
    bilinet.exec('hal-screen on')
    return true
  } catch (e) {
    console.log(LOG + 'hal-screen on failed: ' + (e && e.message ? e.message : e))
    return false
  }
}

/** 查屏幕状态串: "SCREEN_ON" / "SCREEN_OFF" (失败返回空串) */
export function screenState() {
  if (!bilinet || typeof bilinet.exec !== 'function') return ''
  try {
    const s = bilinet.exec('hal-screen state')
    const t = String(s == null ? '' : s)
    const i = t.indexOf('SCREEN_')
    return i >= 0 ? t.substring(i, i + 10) : ''
  } catch (e) { return '' }
}

/** 是否具备防息屏能力 (exec 方法存在) */
export function keepAwakeSupported() {
  return !!(bilinet && typeof bilinet.exec === 'function')
}

/**
 * 合成一次屏幕点击 (等效用户点一下屏幕, 真机实测点击后视频/UI 层级恢复).
 * 坐标 (240,479) = 显示坐标 (480,133) 屏幕中心; 点在 stage 空白处, 只触发
 * 控制条显隐切换, 页面自行恢复控制条状态.
 */
export function tapScreen() {
  if (!bilinet || typeof bilinet.exec !== 'function') return
  try {
    bilinet.exec('send_event touch press 240 479')
    bilinet.exec('send_event touch release 240 479')
  } catch (e) {
    console.log(LOG + 'tapScreen exec failed: ' + (e && e.message ? e.message : e))
  }
}

function toMs(v) {
  const n = Number(v)
  if (!isFinite(n) || n < 0) return 0
  return n
}

export function open(url) {
  console.log(LOG + 'open url(前96)=' + String(url).substring(0, 96))
  // rect 缺省 "auto": 设备侧拿到视频分辨率后等比拟合 UI 带 (悬浮控制条模型,
  // 视频不再全屏拉伸, 也不再需要 UI 侧计算/下发矩形)
  gstPlayer.open(String(url))
}

export function start() {
  console.log(LOG + 'start')
  gstPlayer.start()
}

export function pause() {
  console.log(LOG + 'pause')
  gstPlayer.pause()
}

export function resume() {
  console.log(LOG + 'resume')
  gstPlayer.resume()
}

export function close() {
  try {
    console.log(LOG + 'close')
    if (gstPlayer.close) gstPlayer.close()
  } catch (e) {
    console.log(LOG + 'close error: ' + (e && e.message ? e.message : e))
  }
}

export function seek(ms) {
  const t = Math.max(0, Math.round(ms))
  console.log(LOG + 'seek ' + t + 'ms')
  gstPlayer.seek(t)
}

export function getPosition() {
  try {
    return toMs(gstPlayer.getPosition ? gstPlayer.getPosition() : 0)
  } catch (e) { return 0 }
}

export function getDuration() {
  try {
    return toMs(gstPlayer.getDuration ? gstPlayer.getDuration() : 0)
  } catch (e) { return 0 }
}

export function getVideoSize() {
  try {
    return {
      width: gstPlayer.getVideoWidth ? gstPlayer.getVideoWidth() : 0,
      height: gstPlayer.getVideoHeight ? gstPlayer.getVideoHeight() : 0
    }
  } catch (e) { return { width: 0, height: 0 } }
}

// 原生状态订阅. handler(stateString) 由 SDK 从守护进程读线程投递回 JS 线程.
// JQSignal 形态与输入法 textEditFinished 一致: signal.on(handler) / off(handler)
export function onState(handler) {
  if (stateSubscribed.indexOf(handler) >= 0) return
  stateSubscribed.push(handler)
  if (!nativeSubscribed && gstPlayer && gstPlayer.stateChanged) {
    try {
      gstPlayer.stateChanged.on(handleNativeState)
      nativeSubscribed = true
      console.log(LOG + 'stateChanged.on ok')
    } catch (e) {
      console.log(LOG + 'stateChanged.on failed: ' + (e && e.message ? e.message : e))
    }
  }
}

function handleNativeState() {
  // 兼容参数形态: 字符串 / {state} 对象 / 多参数
  let state = ''
  for (let i = 0; i < arguments.length; i++) {
    const a = arguments[i]
    if (typeof a === 'string') { state = a; break }
    if (a && typeof a === 'object' && typeof a.state === 'string') { state = a.state; break }
  }
  if (!state && arguments.length > 0) state = String(arguments[0])
  const snapshot = stateSubscribed.slice()
  for (let i = 0; i < snapshot.length; i++) {
    const h = snapshot[i]
    if (stateSubscribed.indexOf(h) < 0) continue
    try { h(state) } catch (e) {
      console.log(LOG + 'state handler error: ' + (e && e.message ? e.message : e))
    }
  }
}

export function offState(handler) {
  const i = stateSubscribed.indexOf(handler)
  if (i >= 0) stateSubscribed.splice(i, 1)
  if (stateSubscribed.length === 0 && nativeSubscribed && gstPlayer && gstPlayer.stateChanged) {
    try {
      gstPlayer.stateChanged.off(handleNativeState)
      nativeSubscribed = false
    } catch (e) {}
  }
}
