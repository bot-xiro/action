// 播放器原生适配层 (gstplayer JSAPI: GStreamer + MPP 硬解 + kmssink)
// 跨层契约 (pages/player/player.vue 只依赖本模块):
//   open(url, rect)        打开网络视频流 (souphttpsrc, UA/Referer 由原生带)
//   play() / pause() / resume() / stop()
//   seek(ms)               跳转, 参数毫秒
//   getPosition()/getDuration()  返回毫秒 number (解析失败返回 0)
//   onState(handler)       订阅原生状态串 ("ready"/"play"/"pause"/"eos"/"error:xxx"...)
//   offState(handler)      退订 (与 onState 严格成对)
// 能力探测: isSupported() 为 false 时调用方提示「当前固件不支持视频播放」

import { gstPlayer } from 'gstplayer'

const LOG = '[player] '

let stateSubscribed = []
let nativeSubscribed = false

function logApiSurface() {
  if (!gstPlayer) return
  if (logApiSurface.done) return
  logApiSurface.done = true
  const names = []
  for (const k in gstPlayer) names.push(k + ':' + typeof gstPlayer[k])
  console.log(LOG + 'api surface -> ' + names.join(','))
}

export function isSupported() {
  const ok = !!(gstPlayer && typeof gstPlayer.open === 'function')
  if (ok) logApiSurface()
  return ok
}

function toMs(v) {
  const n = Number(v)
  if (!isFinite(n) || n < 0) return 0
  return n
}

export function open(url, rect) {
  // open(uri, audio, rect, fill): 安卓真机原生日志形态
  //   [gstplayer] open uri=%s audio=%d rect=%s fill=%s
  const r = rect || '0,0,960,266'
  console.log(LOG + 'open url(前96)=' + String(url).substring(0, 96) + ' rect=' + r)
  gstPlayer.open(String(url), 1, r, 'contain')
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

// 原生状态订阅. handler(stateString) 在总线线程投递回 JS 线程后调用.
// JQSignal 形态与输入法 textEditFinished 一致: signal.on(handler) / signal.off(handler)
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
  // 兼容参数形态: 字符串 / 多参数; 只取第一个可字符串化的值
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
