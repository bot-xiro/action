// 运行日志: 追加写入 /userdisk/xiro/bilibili.log
//
// 为什么不用系统 fs JSAPI: 它只提供 readdir/stat/exists/readFile/mkdir/rm,
// **没有写入接口**, 且限定在应用 data 目录。这里改用 bilinet 原生模块的
// readFile/writeFile/mkdirs (直接 libc 打开绝对路径), 不受该限制。
//
// 日志格式沿用设备 /userdisk/xiro/ 下其他应用的约定:
//   [09-12 22:03:15][模块] 内容
//
// 安全: Cookie (SESSDATA / bili_jct / DedeUserID) 一律脱敏后再落盘。
// 体积: 启动时截断到最近 200 行, 运行中每超过 600 行再截断一次。

import { bilinet } from 'bilinet'

const LOG_DIR = '/userdisk/xiro'
const LOG_PATH = '/userdisk/xiro/bilibili.log'
const KEEP_ON_BOOT = 200   // 启动/截断时保留的历史行数
const MAX_LINES = 600      // 单次运行内累计超过这个数触发截断

let ready = false
let failed = false
let lines = 0

function pad(n) {
  return n < 10 ? '0' + n : '' + n
}

function timeStr() {
  const d = new Date()
  return pad(d.getMonth() + 1) + '-' + pad(d.getDate()) + ' ' +
    pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' + pad(d.getSeconds())
}

// 脱敏: 把 SESSDATA=xxx / bili_jct=xxx / DedeUserID=xxx 的值换成 ***
function sanitize(s) {
  let out = String(s)
  const keys = ['SESSDATA=', 'bili_jct=', 'DedeUserID=']
  for (let i = 0; i < keys.length; i++) {
    const k = keys[i]
    let pos = out.indexOf(k)
    while (pos >= 0) {
      let end = pos + k.length
      let len = 0
      while (end + len < out.length && len < 256) {
        const c = out.charAt(end + len)
        if (c === ';' || c === '&' || c === ' ' || c === '\'' || c === '"' || c === ',') break
        len++
      }
      out = out.substring(0, end) + '***' + out.substring(end + len)
      pos = out.indexOf(k, end + 3)
    }
  }
  return out
}

function hasFileApi() {
  return !!(bilinet && typeof bilinet.writeFile === 'function' &&
    typeof bilinet.readFile === 'function')
}

function splitLines(text) {
  const arr = String(text).split('\n')
  while (arr.length > 0 && arr[arr.length - 1] === '') arr.pop()
  return arr
}

function trimTo(n) {
  try {
    const old = bilinet.readFile(LOG_PATH)
    if (!old) return
    const arr = splitLines(old)
    const keep = arr.length > n ? arr.slice(arr.length - n) : arr
    if (bilinet.writeFile(LOG_PATH, keep.join('\n') + '\n', false)) lines = keep.length
  } catch (e) {
    // 截断失败不影响主流程
  }
}

/**
 * 初始化日志 (在 App onLaunch 里调用一次)
 * @param {string} extra 附加到首行的信息, 如 'v0.8.5 appid=...'
 */
export function initLog(extra) {
  if (!hasFileApi()) {
    failed = true
    console.log('[log] bilinet 缺少文件接口, 日志不可用')
    return
  }
  try {
    bilinet.mkdirs(LOG_DIR)
    const old = bilinet.readFile(LOG_PATH)
    if (old) {
      const arr = splitLines(old)
      if (arr.length > KEEP_ON_BOOT) {
        const keep = arr.slice(arr.length - KEEP_ON_BOOT)
        bilinet.writeFile(LOG_PATH, keep.join('\n') + '\n', false)
        lines = keep.length
      } else {
        lines = arr.length
      }
    }
    ready = true
  } catch (e) {
    failed = true
    console.log('[log] 初始化失败: ' + e)
    return
  }
  log('应用', '启动' + (extra ? ' ' + extra : ''))
}

/**
 * 写一行日志 (同时打到 console)
 * @param {string} tag 模块名, 如 '登录' / '网络' / '播放器'
 * @param {string} msg 内容
 */
export function log(tag, msg) {
  const line = '[' + timeStr() + '][' + tag + '] ' + sanitize(msg)
  try {
    console.log(line)
  } catch (e) {}
  if (!ready || failed) return
  try {
    if (bilinet.writeFile(LOG_PATH, line + '\n', true)) {
      lines++
      if (lines > MAX_LINES) trimTo(KEEP_ON_BOOT)
    } else {
      failed = true
      console.log('[log] 写入失败, 停止落盘')
    }
  } catch (e) {
    failed = true
  }
}

/** 给「我的」页面显示的状态文本 */
export function logStatus() {
  if (failed) return '日志不可用'
  if (!ready) return '日志未初始化'
  return '日志 ' + LOG_PATH
}

export function logPath() {
  return LOG_PATH
}
