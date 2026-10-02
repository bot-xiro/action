// 应用设置持久化: /userdisk/xiro/bilibilipan.cfg.json
//
// 为什么自建: 系统 storage JSAPI 只能存字符串且作用域受限, 这里用 bilinet
// 原生 readFile/writeFile 直接写绝对路径, 与 log.js 一致.
// 蓝牙补偿同时写一份纯文本 /userdisk/xiro/btaudio_ms 给原生播放器 (gstplayer) 读.

import { bilinet } from 'bilinet'
import { log } from './log.js'

const CFG_PATH = '/userdisk/xiro/bilibilipan.cfg.json'
const BT_PATH = '/userdisk/xiro/btaudio_ms'

// btaudioMs: 蓝牙音画延迟补偿 (毫秒). 画面超前 -> 加大; 声音超前 -> 减小/0
// keepAwake: 播放期间是否每 6s 调 hal-screen on 防息屏
const DEFAULTS = { btaudioMs: 200, keepAwake: true }

let cache = null

function hasFs() {
  return !!(bilinet && typeof bilinet.readFile === 'function' && typeof bilinet.writeFile === 'function')
}

function clampMs(n) {
  const v = Math.round(Number(n))
  if (!isFinite(v)) return DEFAULTS.btaudioMs
  return Math.max(-500, Math.min(800, v))
}

export function loadConfig() {
  if (cache) return cache
  cache = { btaudioMs: DEFAULTS.btaudioMs, keepAwake: DEFAULTS.keepAwake }
  try {
    if (hasFs()) {
      const s = bilinet.readFile(CFG_PATH)
      if (s) {
        const o = JSON.parse(s)
        if (o && typeof o === 'object') {
          if (o.btaudioMs !== undefined) cache.btaudioMs = clampMs(o.btaudioMs)
          if (typeof o.keepAwake === 'boolean') cache.keepAwake = o.keepAwake
        }
      }
    }
  } catch (e) {
    log('设置', '读取配置失败: ' + (e && e.message ? e.message : e))
  }
  return cache
}

export function getCfg(key) {
  return loadConfig()[key]
}

export function saveConfig() {
  const c = loadConfig()
  try {
    if (hasFs()) bilinet.writeFile(CFG_PATH, JSON.stringify(c))
    // 原生播放器读的纯文本
    if (hasFs()) bilinet.writeFile(BT_PATH, String(c.btaudioMs))
  } catch (e) {
    log('设置', '写入配置失败: ' + (e && e.message ? e.message : e))
  }
  return c
}

export function setCfg(key, value) {
  const c = loadConfig()
  if (key === 'btaudioMs') c.btaudioMs = clampMs(value)
  else if (key === 'keepAwake') c.keepAwake = !!value
  else c[key] = value
  saveConfig()
  log('设置', key + ' = ' + c[key])
  return c
}

export function resetConfig() {
  cache = { btaudioMs: DEFAULTS.btaudioMs, keepAwake: DEFAULTS.keepAwake }
  saveConfig()
  return cache
}

export const CONFIG_PATH = CFG_PATH
export const BT_PATH_CONST = BT_PATH
