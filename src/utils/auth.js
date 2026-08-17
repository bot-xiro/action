/**
 * B站登录认证（纯 JS 实现，基于 storage.js + gstPlayer.httpGet）
 *
 * Cookie/设置存储：复用 storage.js 的系统 storage JSAPI（已验证可用）
 * 二维码生成/轮询：使用 gstPlayer.httpGet（native gstplayer 已加载正常，含 curl + UA/Referer）
 *
 * 对外接口（返回 Promise）：
 *   init()                        // 预热（空实现，兼容旧调用）
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

import storage from './storage.js'
import { gstPlayer } from 'gstplayer'

async function ensureInit() {
  return true
}

async function getCookie() {
  try {
    return await storage.getCookie()
  } catch (e) {
    console.warn('[bili-auth] getCookie failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return ''
  }
}

async function setCookie(cookieStr) {
  try {
    var ok = await storage.setCookie(cookieStr || '')
    if (ok) {
      // 解析并保存用户信息
      var info = parseUserInfoFromCookie(cookieStr)
      if (info && info.mid) {
        await storage.setSetting('user_info', JSON.stringify(info))
      }
    }
    return ok
  } catch (e) {
    console.warn('[bili-auth] setCookie failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return false
  }
}

async function clearCookie() {
  try {
    return await storage.clearCookie()
  } catch (e) {
    console.warn('[bili-auth] clearCookie failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return false
  }
}

async function getSettings() {
  try {
    const jsonStr = await storage.getSetting('all')
    if (!jsonStr) return {}
    return JSON.parse(jsonStr)
  } catch (e) {
    console.warn('[bili-auth] getSettings failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return {}
  }
}

async function setSetting(key, value) {
  try {
    return await storage.setSetting(key, String(value))
  } catch (e) {
    console.warn('[bili-auth] setSetting failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return false
  }
}

// 通用 HTTP GET（复用 gstPlayer.httpGet，已带 UA+Referer）
async function httpGet(url, timeout = 10) {
  try {
    if (!gstPlayer || typeof gstPlayer.httpGet !== 'function') {
      throw new Error('gstPlayer.httpGet not available')
    }
    return await gstPlayer.httpGet(url, timeout)
  } catch (e) {
    console.warn('[bili-auth] httpGet failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    throw e
  }
}

async function generateQrCode() {
  try {
    const url = 'https://passport.bilibili.com/x/passport-login/web/qrcode/generate?source=main-fe-header'
    const body = await httpGet(url, 10)
    if (!body) return null
    return JSON.parse(body)
  } catch (e) {
    console.warn('[bili-auth] generateQrCode failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return null
  }
}

async function pollQrCode(qrcodeKey) {
  try {
    if (!qrcodeKey) return { code: -1, message: 'empty qrcodeKey' }
    const url = 'https://passport.bilibili.com/x/passport-login/web/qrcode/poll?qrcode_key=' + encodeURIComponent(qrcodeKey) + '&source=main-fe-header'
    const body = await httpGet(url, 10)
    if (!body) return { code: -1, message: 'empty response' }
    return JSON.parse(body)
  } catch (e) {
    console.warn('[bili-auth] pollQrCode failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return { code: -1, message: 'error' }
  }
}

async function isLoggedIn() {
  try {
    const cookie = await storage.getCookie()
    return !!cookie
  } catch (e) {
    console.warn('[bili-auth] isLoggedIn failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return false
  }
}

async function getUserInfo() {
  try {
    const jsonStr = await storage.getSetting('user_info')
    if (!jsonStr) return {}
    return JSON.parse(jsonStr)
  } catch (e) {
    console.warn('[bili-auth] getUserInfo failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return {}
  }
}

async function logout() {
  try {
    await storage.clearCookie()
    await storage.setSetting('user_info', '')
    return true
  } catch (e) {
    console.warn('[bili-auth] logout failed: ' + (e && e.message ? e.message : JSON.stringify(e)))
    return false
  }
}

// 从 cookie 字符串解析用户信息（DedeUserID 等）
function parseUserInfoFromCookie(cookieStr) {
  if (!cookieStr) return null
  try {
    var info = {}
    var parts = cookieStr.split(';')
    for (var i = 0; i < parts.length; i++) {
      var kv = parts[i].trim().split('=')
      if (kv.length === 2) {
        var k = kv[0].trim()
        var v = kv[1].trim()
        if (k === 'DedeUserID') info.mid = v
        else if (k === 'DedeUserID__ckMd5') info.mid_ckmd5 = v
        else if (k === 'SESSDATA') info.sessdata = v
        else if (k === 'bili_jct') info.bili_jct = v
        else if (k === 'sid') info.sid = v
      }
    }
    return info
  } catch (e) {
    return null
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
  logout: logout,
  parseUserInfoFromCookie: parseUserInfoFromCookie
}