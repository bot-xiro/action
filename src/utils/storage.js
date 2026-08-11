/**
 * 搜索历史持久化（基于系统 storage JSAPI，异步）
 *
 * 【根因修复 2026-08-10】旧实现用 globalThis.$falcon.storage.get/set，
 * 该对象不存在 → 始终回退内存数组 → 应用关闭后历史丢失。
 * 系统正确 JSAPI：import storage from '$jsapi/storage'
 *   → storage.getStorage(key) / storage.setStorage(key, value)（异步 Promise）
 * 见 docs/STUDY_NOTES.md「关键 JSAPI」表与 refs/haasui-docs storage-kv.md。
 *
 * 对外接口保持同步风格调用（返回 Promise）：
 *   getHistory() → Promise<Array<string>>
 *   addHistory(kw) → Promise<Array<string>>（去重置顶，≤10 条）
 *   clearHistory() → Promise<void>
 * 所有方法内部兜底：设备 storage 异常时回退内存数组，不抛异常。
 *
 * ============ 登录 Cookie 存储（预留）===========
 * 登录成功后获取的 SESSDATA 等 cookie 字符串持久化。
 * 真正生效需 native 层扩展 httpGet 支持 cookie（如新增 httpGetWithCookie）。
 * 当前 httpGet 不带 cookie → 登录态无法在后续请求生效，仅作本地存储预留。
 */

import storage from '$jsapi/storage'

const KEY = 'bilibili_search_history'
const COOKIE_KEY = 'bilibili_login_cookie'
const MAX_ITEMS = 10

// 内存兜底缓存（设备 storage 不可用时，页面生命周期内仍可用）
let _memory = []
let _cookieMemory = ''

function safeParse(raw) {
  if (!raw) return []
  try {
    var arr = JSON.parse(raw)
    if (Array.isArray(arr)) return arr
    return []
  } catch (e) {
    return []
  }
}

async function readRaw() {
  try {
    if (storage && typeof storage.getStorage === 'function') {
      const v = await storage.getStorage(KEY)
      if (v !== undefined && v !== null) {
        const arr = safeParse(v)
        _memory = arr.slice()
        return arr
      }
    }
    return _memory.slice()
  } catch (e) {
    // 设备 storage 读取失败 → 回退内存
    return _memory.slice()
  }
}

async function writeRaw(arr) {
  _memory = arr.slice()
  try {
    if (storage && typeof storage.setStorage === 'function') {
      await storage.setStorage(KEY, JSON.stringify(arr))
    }
  } catch (e) {
    // 写入失败仅保留内存，不抛异常
  }
}

async function readCookie() {
  try {
    if (storage && typeof storage.getStorage === 'function') {
      const v = await storage.getStorage(COOKIE_KEY)
      if (v !== undefined && v !== null && typeof v === 'string') {
        _cookieMemory = v
        return v
      }
    }
    return _cookieMemory
  } catch (e) {
    return _cookieMemory
  }
}

async function writeCookie(cookieStr) {
  _cookieMemory = cookieStr || ''
  try {
    if (storage && typeof storage.setStorage === 'function') {
      await storage.setStorage(COOKIE_KEY, _cookieMemory)
    }
  } catch (e) {
    // 仅保留内存
  }
}

function getHistory() {
  return readRaw()
}

/**
 * 添加一条历史（去重，置顶，超出 10 条截断）
 * @param {string} keyword
 * @returns {Promise<Array<string>>}
 */
function addHistory(keyword) {
  return readRaw().then(function (list) {
    if (!keyword || typeof keyword !== 'string') return []
    var kw = keyword.trim()
    if (!kw) return list || []
    const next = [kw]
    for (var i = 0; i < list.length && next.length < MAX_ITEMS; i++) {
      if (list[i] !== kw) next.push(list[i])
    }
    return writeRaw(next).then(function () {
      return next
    })
  })
}

function clearHistory() {
  return writeRaw([])
}

// ============ 登录 Cookie 存取（预留）===========
/**
 * 获取已保存的登录 cookie 字符串（SESSDATA; bili_jct; ...）
 * @returns {Promise<string>}
 */
function getCookie() {
  return readCookie()
}

/**
 * 保存登录 cookie 字符串
 * @param {string} cookieStr
 * @returns {Promise<void>}
 */
function setCookie(cookieStr) {
  return writeCookie(cookieStr)
}

/**
 * 清除登录 cookie
 * @returns {Promise<void>}
 */
function clearCookie() {
  return writeCookie('')
}

export default {
  getHistory: getHistory,
  addHistory: addHistory,
  clearHistory: clearHistory,
  MAX_ITEMS: MAX_ITEMS,
  // 登录 cookie 存取
  getCookie: getCookie,
  setCookie: setCookie,
  clearCookie: clearCookie
}