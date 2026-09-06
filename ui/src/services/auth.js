// 登录态管理 (SESSDATA Cookie 持久化)
//
// 登录途径:
//   1. 扫码登录 (login 页): qrcode generate/poll, 成功后响应体 redirect url
//      携带 DedeUserID/SESSDATA/bili_jct 参数, 无需解析 Set-Cookie 头.
//   2. Cookie 导入: 用户从电脑浏览器复制 bilibili Cookie 字符串粘贴导入.
//
// 存储: 运行时 storage JSAPI 形态未知 (skill: 同名 API 也要验证参数/返回包装),
// 适配多种形态, 全部失败降级内存态 (重启后需重新登录, 不影响功能验证).
// 内存是运行期唯一事实来源; 写入即异步持久化 (fire and forget).

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

export function initAuth() {
  if (memory.loaded) return
  memory.loaded = true
  const raw = storageGet(STORE_KEY)
  if (!raw) return
  try {
    const obj = JSON.parse(raw)
    memory.sessdata = typeof obj.sessdata === 'string' ? obj.sessdata : ''
    memory.biliJct = typeof obj.biliJct === 'string' ? obj.biliJct : ''
    memory.dedeUserId = typeof obj.dedeUserId === 'string' ? obj.dedeUserId : ''
    console.log(LOG + 'loaded cookie from storage: ' + (memory.sessdata ? 'yes' : 'empty'))
  } catch (e) {
    console.log(LOG + 'stored cookie parse failed')
  }
}

function persist() {
  const raw = JSON.stringify({
    sessdata: memory.sessdata,
    biliJct: memory.biliJct,
    dedeUserId: memory.dedeUserId,
    v: 1
  })
  storageSet(STORE_KEY, raw)
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

export function saveLogin(sessdata, biliJct, dedeUserId) {
  memory.sessdata = sessdata || ''
  memory.biliJct = biliJct || ''
  memory.dedeUserId = dedeUserId || ''
  persist()
  console.log(LOG + 'login saved (sessdata ' + (memory.sessdata ? 'ok' : 'empty') +
    ', csrf ' + (memory.biliJct ? 'ok' : 'none') + ')')
}

export function clearLogin() {
  memory.sessdata = ''
  memory.biliJct = ''
  memory.dedeUserId = ''
  persist()
  console.log(LOG + 'login cleared')
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
