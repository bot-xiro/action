// 登录态管理 (SESSDATA Cookie 持久化)
//
// 登录途径:
//   1. 扫码登录 (login 页): qrcode generate/poll, 成功后响应体 redirect url
//      携带 DedeUserID/SESSDATA/bili_jct 参数, 无需解析 Set-Cookie 头.
//   2. Cookie 导入: 用户从电脑浏览器复制 bilibili Cookie 字符串粘贴导入.
//
// 存储: 主存储是 /userdisk/xiro/bilibili.db (sqlite, 见 store.js);
// 同时保留 storage KV 作为副存储, 数据库不可用时仍能记住登录。
// 内存是运行期唯一事实来源; 写入即同步持久化。

import { initStore, writeAuth, clearAuthRow, writeProfile } from './store.js'
import { log } from './log.js'

const LOG = '[auth] '

const memory = {
  sessdata: '',
  biliJct: '',
  dedeUserId: '',
  loaded: false
}

const STORE_KEY = 'bilibilipan:auth:v1'

function jsapiStorage() {
  try {
    if ($falcon && $falcon.jsapi && $falcon.jsapi.storage) return $falcon.jsapi.storage
  } catch (e) {}
  return null
}

function normalizeStoredValue(r) {
  if (r == null) return ''
  if (typeof r === 'string') return r
  if (typeof r === 'object') {
    if (typeof r.data === 'string') return r.data
    if (typeof r.value === 'string') return r.value
  }
  return ''
}

function storageGet(key) {
  const s = jsapiStorage()
  if (!s) return ''
  try {
    if (typeof s.getItem === 'function') return normalizeStoredValue(s.getItem({ key: key }))
    if (typeof s.getStorage === 'function') return normalizeStoredValue(s.getStorage(key))
  } catch (e) {
    console.log(LOG + 'storageGet failed: ' + (e && e.message ? e.message : e))
  }
  return ''
}

function storageSet(key, value) {
  const s = jsapiStorage()
  if (!s) return
  try {
    if (typeof s.setItem === 'function') { s.setItem({ key: key, value: value }); return }
    if (typeof s.setStorage === 'function') { s.setStorage(key, value); return }
  } catch (e) {
    console.log(LOG + 'storageSet failed: ' + (e && e.message ? e.message : e))
  }
}

function apply(obj) {
  memory.sessdata = typeof obj.sessdata === 'string' ? obj.sessdata : ''
  memory.biliJct = typeof obj.biliJct === 'string' ? obj.biliJct : ''
  memory.dedeUserId = typeof obj.dedeUserId === 'string' ? obj.dedeUserId : ''
}

export function initAuth() {
  if (memory.loaded) return
  memory.loaded = true
  // 1) 数据库 (/userdisk/xiro/bilibili.db) 优先
  let got = false
  try {
    const row = initStore()
    if (row && row.sessdata) {
      apply(row)
      got = true
      log('登录', '从数据库载入登录态 uid=' + row.dedeUserId)
    }
  } catch (e) {
    console.log(LOG + 'initStore failed: ' + (e && e.message ? e.message : e))
  }
  if (got) return
  // 2) 退回 storage KV
  const raw = storageGet(STORE_KEY)
  if (!raw) return
  try {
    apply(JSON.parse(raw))
    log('登录', '从 KV 载入登录态 uid=' + memory.dedeUserId)
  } catch (e) {
    console.log(LOG + 'stored cookie parse failed')
  }
}

function persist(profile) {
  const raw = JSON.stringify({
    sessdata: memory.sessdata,
    biliJct: memory.biliJct,
    dedeUserId: memory.dedeUserId,
    v: 1
  })
  storageSet(STORE_KEY, raw)
  try {
    writeAuth(memory.sessdata, memory.biliJct, memory.dedeUserId, profile)
  } catch (e) {
    console.log(LOG + 'db write failed: ' + (e && e.message ? e.message : e))
  }
}

export function hasCookie() {
  return memory.sessdata !== ''
}

// 请求头: ["Cookie: SESSDATA=..; bili_jct=..; DedeUserID=.."] (无登录态返回 undefined)
export function cookieHeader() {
  if (memory.sessdata === '') return undefined
  let h = 'Cookie: SESSDATA=' + memory.sessdata
  if (memory.biliJct !== '') h += '; bili_jct=' + memory.biliJct
  if (memory.dedeUserId !== '') h += '; DedeUserID=' + memory.dedeUserId
  return h
}

export function getCsrf() {
  return memory.biliJct
}

/** 当前登录用户 mid (即 DedeUserID cookie 值); 未登录返回 '' */
export function getMid() {
  return memory.dedeUserId || ''
}

export function saveLogin(sessdata, biliJct, dedeUserId, profile) {
  memory.sessdata = sessdata || ''
  memory.biliJct = biliJct || ''
  memory.dedeUserId = dedeUserId || ''
  persist(profile)
  log('登录', '保存登录态 uid=' + memory.dedeUserId +
    ' csrf=' + (memory.biliJct ? 'ok' : 'none'))
}

/** 账号快照落库 (昵称/头像/等级/硬币/B币), 不动 Cookie */
export function saveProfile(info) {
  if (!info) return
  try {
    writeProfile(info)
  } catch (e) {
    console.log(LOG + 'saveProfile failed: ' + (e && e.message ? e.message : e))
  }
}

export function clearLogin() {
  memory.sessdata = ''
  memory.biliJct = ''
  memory.dedeUserId = ''
  try {
    clearAuthRow()
  } catch (e) {
    console.log(LOG + 'db clear failed: ' + (e && e.message ? e.message : e))
  }
  const raw = JSON.stringify({ sessdata: '', biliJct: '', dedeUserId: '', v: 1 })
  storageSet(STORE_KEY, raw)
  log('登录', '已清除登录态')
}

// 解析用户粘贴的 Cookie 文本. 兼容三种形态:
//   1. 完整 Cookie 头: "SESSDATA=xx; bili_jct=yy; DedeUserID=zz; ..."
//   2. url 参数形态: "...?DedeUserID=zz&SESSDATA=xx&bili_jct=yy"
//   3. 仅 SESSDATA 裸值 (无 = 号)
export function parseCookieText(text) {
  const s = String(text || '').trim()
  if (s === '') return null
  const out = { sessdata: '', biliJct: '', dedeUserId: '' }
  const pairs = s.split(/[;&\s]+/)
  for (let i = 0; i < pairs.length; i++) {
    const p = pairs[i]
    const eq = p.indexOf('=')
    if (eq <= 0) continue
    const k = p.substring(0, eq).trim()
    const v = p.substring(eq + 1).trim()
    if (k === 'SESSDATA' && !out.sessdata) out.sessdata = v
    else if (k === 'bili_jct' && !out.biliJct) out.biliJct = v
    else if (k === 'DedeUserID' && !out.dedeUserId) out.dedeUserId = v
  }
  if (!out.sessdata && s.indexOf('=') < 0) out.sessdata = s  // 裸 SESSDATA 值
  if (!out.sessdata) return null
  return out
}
