// 登录内容持久化: /userdisk/xiro/bilibili.db (sqlite3)
//
// 为什么放 /userdisk/xiro/: 需求指定日志与数据库都落在这个目录
// (该目录下已有其它应用写的 status.json / wifi.log, 我们的文件名不与之冲突)。
//
// 为什么走 bilinet 原生模块: 系统 fs JSAPI 没有写入接口; 而 sqlite3 是否是
// 本固件注册的 JSAPI 无法在编译期确认, 一旦 import 失败整个 app 就起不来。
// bilinet 用 dlopen 加载系统的 libsqlite3.so.0, 失败只是这个能力不可用,
// 上层自动退回 JSON 文件兜底, 不影响主流程。所有接口都是同步的。
//
// 表结构:
//   auth(id=1)  当前登录态 + 最近一次账号快照
//   login_log   每次登录 / 退出的历史 (只记账号, 不记 Cookie 明文)

import { bilinet } from 'bilinet'

const DB_DIR = '/userdisk/xiro'
const DB_PATH = '/userdisk/xiro/bilibili.db'
const JSON_PATH = '/userdisk/xiro/bilibili-auth.json'

let opened = false
let failed = false
let reason = ''
let cache = null          // 当前登录行

function hasDb() {
  return !!(bilinet && typeof bilinet.dbOpen === 'function' &&
    typeof bilinet.dbExec === 'function' && typeof bilinet.dbQuery === 'function')
}

function hasFileApi() {
  return !!(bilinet && typeof bilinet.writeFile === 'function' &&
    typeof bilinet.readFile === 'function')
}

function nowSec() {
  return Math.floor(Date.now() / 1000)
}

// SQL 字符串字面量: 单引号翻倍
function q(v) {
  return String(v === undefined || v === null ? '' : v).replace(/'/g, "''")
}

function n(v) {
  const x = Number(v)
  return isFinite(x) ? x : 0
}

function str(v) {
  return v === undefined || v === null ? '' : String(v)
}

// ------------------------------ JSON 兜底 ------------------------------

function jsonRead() {
  if (!hasFileApi()) return null
  try {
    const txt = bilinet.readFile(JSON_PATH)
    if (!txt) return null
    const o = JSON.parse(txt)
    if (o && typeof o === 'object' && o.sessdata) return o
    return null
  } catch (e) {
    return null
  }
}

function jsonWrite(obj) {
  if (!hasFileApi()) return false
  try {
    bilinet.mkdirs(DB_DIR)
    return bilinet.writeFile(JSON_PATH, JSON.stringify(obj), false)
  } catch (e) {
    return false
  }
}

// ------------------------------ sqlite ------------------------------

const SQL_AUTH = 'CREATE TABLE IF NOT EXISTS auth (' +
  'id INTEGER PRIMARY KEY CHECK (id = 1), ' +
  'sessdata TEXT NOT NULL, ' +
  'bili_jct TEXT NOT NULL, ' +
  'dede_userid TEXT NOT NULL, ' +
  'uname TEXT, mid INTEGER, face TEXT, level INTEGER, ' +
  'coin REAL, money REAL, vip TEXT, updated_at INTEGER)'

const SQL_LOG = 'CREATE TABLE IF NOT EXISTS login_log (' +
  'id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL, ' +
  'event TEXT NOT NULL, ' +
  'mid INTEGER, uname TEXT, ' +
  'created_at INTEGER)'

function dbLoad() {
  let rows = []
  try {
    rows = JSON.parse(bilinet.dbQuery('SELECT * FROM auth WHERE id = 1'))
  } catch (e) {
    return null
  }
  if (!rows || rows.length === 0) return null
  const r = rows[0]
  if (!r.sessdata) return null
  return {
    sessdata: str(r.sessdata),
    biliJct: str(r.bili_jct),
    dedeUserId: str(r.dede_userid),
    uname: str(r.uname),
    mid: n(r.mid),
    face: str(r.face),
    level: n(r.level),
    coin: n(r.coin),
    money: n(r.money),
    vip: str(r.vip),
    updatedAt: n(r.updated_at)
  }
}

function dbUpsert(a) {
  const sql = 'INSERT OR REPLACE INTO auth ' +
    '(id, sessdata, bili_jct, dede_userid, uname, mid, face, level, coin, money, vip, updated_at) ' +
    "VALUES (1, '" + q(a.sessdata) + "', '" + q(a.biliJct) + "', '" + q(a.dedeUserId) + "', '" +
    q(a.uname) + "', " + n(a.mid) + ", '" + q(a.face) + "', " + n(a.level) + ', ' +
    n(a.coin) + ', ' + n(a.money) + ", '" + q(a.vip) + "', " + n(a.updatedAt) + ')'
  return bilinet.dbExec(sql)
}

function dbLog(event, mid, uname) {
  const sql = "INSERT INTO login_log (event, mid, uname, created_at) VALUES ('" +
    q(event) + "', " + n(mid) + ", '" + q(uname) + "', " + nowSec() + ')'
  return bilinet.dbExec(sql)
}

/**
 * 打开数据库并建表。同步执行, 返回当前登录行 (没有则 null)。
 * 失败不抛异常, 自动退回 JSON 兜底。
 */
export function initStore() {
  if (opened) return cache
  if (!hasDb()) {
    failed = true
    reason = 'bilinet 缺少 db 接口'
    cache = jsonRead()
    return cache
  }
  try {
    bilinet.mkdirs(DB_DIR)
  } catch (e) {}
  if (!bilinet.dbOpen(DB_PATH)) {
    failed = true
    reason = '打开数据库失败'
    cache = jsonRead()
    return cache
  }
  if (!bilinet.dbExec(SQL_AUTH) || !bilinet.dbExec(SQL_LOG)) {
    failed = true
    reason = '建表失败'
    cache = jsonRead()
    return cache
  }
  opened = true
  cache = dbLoad()
  return cache
}

/** 同步取当前登录行 (initStore 之前调用返回 null) */
export function getStoredAuth() {
  return cache
}

/**
 * 写入登录态。profile 可选 (账号快照: uname/mid/face/level/coin/money/vip)。
 */
export function writeAuth(sessdata, biliJct, dedeUserId, profile) {
  // 没带账号快照时沿用上一次的, 别把昵称/等级冲成空
  const p = profile || (cache || {})
  const a = {
    sessdata: str(sessdata),
    biliJct: str(biliJct),
    dedeUserId: str(dedeUserId),
    uname: str(p.uname),
    mid: n(p.mid),
    face: str(p.face),
    level: n(p.level),
    coin: n(p.coin),
    money: n(p.money),
    vip: str(p.vip),
    updatedAt: nowSec()
  }
  if (!a.sessdata) return false
  cache = a
  jsonWrite(a)
  if (!opened) return true
  try {
    if (dbUpsert(a)) dbLog('login', a.mid, a.uname)
    else reason = '写入失败'
  } catch (e) {
    reason = String(e && e.message ? e.message : e)
    return false
  }
  return true
}

/** 只更新账号快照, 不动 Cookie (每次刷出「我的」信息后调用) */
export function writeProfile(profile) {
  if (!cache || !profile) return false
  cache.uname = str(profile.uname)
  cache.mid = n(profile.mid)
  cache.face = str(profile.face)
  cache.level = n(profile.level)
  cache.coin = n(profile.coin)
  cache.money = n(profile.money)
  cache.vip = str(profile.vip)
  cache.updatedAt = nowSec()
  jsonWrite(cache)
  if (!opened) return true
  try {
    return dbUpsert(cache)
  } catch (e) {
    return false
  }
}

/** 退出登录: 清掉 auth 行, 留一条 logout 记录 */
export function clearAuthRow() {
  const old = cache
  cache = null
  try {
    jsonWrite({ sessdata: '', biliJct: '', dedeUserId: '' })
  } catch (e) {}
  if (!opened) return
  try {
    bilinet.dbExec('DELETE FROM auth WHERE id = 1')
    dbLog('logout', old ? old.mid : 0, old ? old.uname : '')
  } catch (e) {
    reason = String(e && e.message ? e.message : e)
  }
}

/** 给「我的」页面显示的一行状态 */
export function storeStatus() {
  if (failed) return '数据库不可用' + (reason ? '(' + reason + ')' : '')
  if (!opened) return '数据库未就绪'
  return '数据库 ' + DB_PATH
}

export function storePath() {
  return DB_PATH
}
