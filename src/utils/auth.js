/**
 * B站登录认证与本地数据库存储（基于 native bili-auth 模块）
 *
 * 数据库路径：/userdisk/xiro/bili/app.db
 * 表结构：
 *   - cookies: key TEXT PRIMARY KEY, value TEXT, updated_at INTEGER
 *   - settings: key TEXT PRIMARY KEY, value TEXT, updated_at INTEGER
 *   - user_info: key TEXT PRIMARY KEY, value TEXT, updated_at INTEGER
 *
 * 对外接口（返回 Promise）：
 *   init()                        // 初始化数据库
 *   getCookie()                   // 获取保存的 cookie 字符串
 *   setCookie(cookieStr)          // 保存 cookie 字符串
 *   clearCookie()                 // 清除 cookie
 *   getSettings()                 // 获取所有设置
 *   setSetting(key, value)        // 保存单个设置
 *   generateQrCode()              // 生成登录二维码
 *   pollQrCode(qrcodeKey)         // 轮询二维码登录状态
 *   isLoggedIn()                  // 检查是否已登录
 *   getUserInfo()                 // 获取用户信息
 *   logout()                      // 登出
 */

import { biliAuth } from 'biliauth'

let _inited = false

async function ensureInit() {
  if (!_inited) {
    try {
      await biliAuth.init()
      _inited = true
    } catch (e) {
      console.warn('[bili-auth] init failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    }
  }
}

async function getCookie() {
  await ensureInit()
  try {
    return await biliAuth.getCookie()
  } catch (e) {
    console.warn('[bili-auth] getCookie failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return ''
  }
}

async function setCookie(cookieStr) {
  await ensureInit()
  try {
    return await biliAuth.setCookie(cookieStr || '')
  } catch (e) {
    console.warn('[bili-auth] setCookie failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return false
  }
}

async function clearCookie() {
  await ensureInit()
  try {
    return await biliAuth.clearCookie()
  } catch (e) {
    console.warn('[bili-auth] clearCookie failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return false
  }
}

async function getSettings() {
  await ensureInit()
  try {
    const jsonStr = await biliAuth.getSettings()
    if (!jsonStr) return {}
    return JSON.parse(jsonStr)
  } catch (e) {
    console.warn('[bili-auth] getSettings failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return {}
  }
}

async function setSetting(key, value) {
  await ensureInit()
  try {
    return await biliAuth.setSetting(key, String(value))
  } catch (e) {
    console.warn('[bili-auth] setSetting failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return false
  }
}

async function generateQrCode() {
  await ensureInit()
  try {
    const jsonStr = await biliAuth.generateQrCode()
    if (!jsonStr) return null
    return JSON.parse(jsonStr)
  } catch (e) {
    console.warn('[bili-auth] generateQrCode failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return null
  }
}

async function pollQrCode(qrcodeKey) {
  await ensureInit()
  try {
    const jsonStr = await biliAuth.pollQrCode(qrcodeKey)
    if (!jsonStr) return { code: -1, message: 'empty response' }
    return JSON.parse(jsonStr)
  } catch (e) {
    console.warn('[bili-auth] pollQrCode failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return { code: -1, message: 'error' }
  }
}

async function isLoggedIn() {
  await ensureInit()
  try {
    return await biliAuth.isLoggedIn()
  } catch (e) {
    console.warn('[bili-auth] isLoggedIn failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return false
  }
}

async function getUserInfo() {
  await ensureInit()
  try {
    const jsonStr = await biliAuth.getUserInfo()
    if (!jsonStr) return {}
    return JSON.parse(jsonStr)
  } catch (e) {
    console.warn('[bili-auth] getUserInfo failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return {}
  }
}

async function logout() {
  await ensureInit()
  try {
    return await biliAuth.logout()
  } catch (e) {
    console.warn('[bili-auth] logout failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return false
  }
}

export default {
  init: ensureInit,
  getCookie: getCookie,
  setCookie: setCookie,
  clearCookie: clearCookie,
  getSettings: getSettings,
  setSetting: setSetting,
  generateQrCode: generateQrCode,
  pollQrCode: pollQrCode,
  isLoggedIn: isLoggedIn,
  getUserInfo: getUserInfo,
  logout: logout
}