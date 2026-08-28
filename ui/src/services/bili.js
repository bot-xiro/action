// 哔哩哔哩网络服务
// 网络层: $falcon.jsapi.http.request (固件 4.3.5 已验证可走通, 日志 tag debug_httpApi)
// 真机已验证的返回形态差异 (v0.1.3 修复 "HTTP 错误"):
//   - 该固件 request resolve 的是响应体自身 (ArrayBuffer / 二进制),
//     不携带 statusCode, 旧代码取不到 status 一律报 "HTTP 错误"
//   - headers 只接受字符串数组 ["Key: value"], 对象形式被原生层丢弃,
//     导致 UA/Referer 实际没发出去
// http API 文档 (references/haasui-docs/docs/jsapi/bashi/http.md):
//   headers 形如 ["Content-Type:application/json"]; timeout 单位是秒(我们传毫秒值
//   会被当作秒, 实际效果偏长, 统一改为秒)

function hasHttp() {
  const j = $falcon && $falcon.jsapi
  return !!(j && ((j.http && j.http.request) || (j.net && j.net.request)))
}

let cookieHeader = ''

async function httpGet(url, headersMap, timeoutSec) {
  const jsapi = $falcon.jsapi
  const headers = []
  for (const k in headersMap) headers.push(k + ': ' + headersMap[k])
  if (cookieHeader) headers.push('Cookie: ' + cookieHeader)
  const params = { url: url, method: 'GET', headers: headers, timeout: timeoutSec }
  if (jsapi.http && jsapi.http.request) return await jsapi.http.request(params)
  return await jsapi.net.request(params)
}

// 二进制 -> UTF-8 字符串
function bytesToString(res) {
  try {
    let u8 = null
    if (typeof ArrayBuffer !== 'undefined' && res instanceof ArrayBuffer) {
      u8 = new Uint8Array(res)
    } else if (typeof ArrayBuffer !== 'undefined' && ArrayBuffer.isView && ArrayBuffer.isView(res)) {
      u8 = new Uint8Array(res.buffer, res.byteOffset || 0, res.byteLength)
    } else if (Array.isArray(res)) {
      u8 = res
    } else {
      return null
    }
    if (typeof TextDecoder !== 'undefined') {
      return new TextDecoder('utf-8').decode(u8 instanceof Uint8Array ? u8 : new Uint8Array(u8))
    }
    // 无 TextDecoder 的兜底 (QuickJS 老版本): 仅保证 ASCII 正确
    let s = ''
    for (let i = 0; i < u8.length; i++) s += String.fromCharCode(u8[i])
    return decodeURIComponent(escape(s))
  } catch (e) {
    return null
  }
}

function parseBody(data) {
  if (data && typeof data === 'object' && !isBinary(data)) return data
  if (typeof data === 'string') {
    try { return JSON.parse(data) } catch (e) {
      throw new Error('服务器返回格式错误')
    }
  }
  const s = bytesToString(data)
  if (s !== null) {
    try { return JSON.parse(s) } catch (e) {
      throw new Error('服务器返回格式错误')
    }
  }
  throw new Error('空响应')
}

function isBinary(v) {
  if (typeof ArrayBuffer === 'undefined') return false
  return v instanceof ArrayBuffer || (ArrayBuffer.isView && ArrayBuffer.isView(v))
}

// 归一化各固件返回形态:
//   1. 裸响应体: ArrayBuffer / TypedArray / string  (本固件实测形态)
//   2. {statusCode|status, data|body}
//   3. {error, result}
// 返回 body 文本/对象; 状态码非 2xx 抛 HTTP 错误
function unwrapResponse(res) {
  if (isBinary(res) || typeof res === 'string') return res
  if (!res || typeof res !== 'object') throw new Error('空响应')
  if (res.error) {
    // 真机实测: 失败时包装为 {error:3, result:{errorMessage:"curl ... resCode:412"}},
    // resCode 才是真正的 HTTP 状态码
    const em = res.result && typeof res.result.errorMessage === 'string' ? res.result.errorMessage : ''
    const m = em.match(/resCode:(\d+)/)
    const code = m ? parseInt(m[1], 10) : (res.statusCode || res.status || 0)
    if (code === 412) throw rateLimitError('请求被风控拦截, 请稍后再试 (HTTP 412)')
    if (code === 429) throw rateLimitError('请求过于频繁, 请稍候片刻再试 (HTTP 429)')
    throw new Error((typeof res.error === 'string' ? res.error : (em || '传输失败'))
      + (code ? ' (HTTP ' + code + ')' : ''))
  }
  const status = res.statusCode || res.status
  const body = res.data !== undefined ? res.data : (res.body !== undefined ? res.body : res.result)
  if (status !== undefined && (status < 200 || status >= 300)) {
    // bilibili 风控常以 HTTP 412 返回
    if (status === 412) throw rateLimitError('请求被风控拦截, 请稍后再试 (HTTP 412)')
    throw new Error('HTTP ' + status)
  }
  if (body === undefined) throw new Error('空响应')
  return body
}

// 反风控实践 (参考社区协议层分析):
//   - UA 固定主流浏览器指纹
//   - Referer 必须与访问场景一致: 搜索->search.bilibili.com,
//     详情->视频页, 空间->space.bilibili.com, 热门->www.bilibili.com
//   - Accept/Accept-Language 补全, 与浏览器请求形态对齐
const UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36'
const REFERER = 'https://www.bilibili.com'
const REFERER_SEARCH = 'https://search.bilibili.com/all'

function sleep(ms) {
  return new Promise(function (resolve) { setTimeout(resolve, ms) })
}

// getJson: 所有 JSON 请求的唯一入口. 传输层抖动最多重试 3 次
// (退避 800/1600ms); 限流类错误 (-352/-412/HTTP412/过于频繁) 直接抛出
function safeJson(v) {
  try { return JSON.stringify(v) } catch (e) { return String(v) }
}

// 相邻请求至少 1.2s (类人节奏); 命中 412/-352/-412 即进入冷却,
// 连续命中按 1→2→5→10 分钟升级, 成功一次后清零
let lastRequestAt = 0
let cooldownUntil = 0
let cookieTried = false
let consecutiveLimits = 0

function rateLimitError(msg) {
  consecutiveLimits++
  const minutes = [1, 2, 5, 10][Math.min(consecutiveLimits - 1, 3)]
  cooldownUntil = Date.now() + minutes * 60000
  const e = new Error(msg)
  e.rateLimited = true
  return e
}

function isRateLimitError(e) {
  return !!(e && e.rateLimited)
}

// 反风控: 先取 buvid3 设备指纹 cookie, 之后所有请求带上, 可显著降低被风控概率
async function ensureCookie() {
  if (cookieTried) return
  cookieTried = true
  try {
    const res = await httpGet('https://api.bilibili.com/x/frontend/finger/spi',
      { 'User-Agent': UA, 'Referer': REFERER }, 15)
    lastRequestAt = Date.now()
    const body = parseBody(unwrapResponse(res))
    if (body && body.code === 0 && body.data && body.data.b_3) {
      cookieHeader = 'buvid3=' + body.data.b_3
      if (body.data.b_4) cookieHeader += '; buvid4=' + body.data.b_4
      console.log('[bili] buvid3 获取成功')
    } else {
      console.log('[bili] buvid3 不可用, code=' + (body && body.code))
    }
  } catch (e) {
    console.log('[bili] buvid3 获取失败: ' + (e && e.message ? e.message : e))
    lastRequestAt = Date.now()
  }
}

// ================= wbi 签名 (BACNext: 空间类接口已全部 wbi 化) =================
const WBI_MIXIN_TAB = [46, 47, 18, 2, 53, 8, 23, 32, 15, 50, 10, 31, 58, 3, 45, 35,
  27, 43, 5, 49, 33, 9, 42, 19, 29, 28, 14, 39, 12, 38, 41, 13, 37, 48, 7, 16, 24,
  55, 40, 61, 26, 17, 0, 1, 60, 51, 30, 4, 22, 25, 54, 21, 56, 59, 6, 63, 57, 62, 11,
  36, 20, 34, 44, 52]
let wbiKeys = null
let wbiKeysAt = 0

// ---- 纯 JS MD5 (QuickJS 20200705 无字符串 hash; 固件 crypto 只有 hashFile) ----
function md5Utf8(str) {
  // UTF-8 编码为字节数组 (含代理对处理)
  const bytes = []
  for (let i = 0; i < str.length; i++) {
    let c = str.charCodeAt(i)
    if (c >= 0xd800 && c <= 0xdbff && i + 1 < str.length) {
      const c2 = str.charCodeAt(i + 1)
      if (c2 >= 0xdc00 && c2 <= 0xdfff) { c = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00); i++ }
    }
    if (c < 0x80) bytes.push(c)
    else if (c < 0x800) { bytes.push(0xc0 | (c >> 6), 0x80 | (c & 0x3f)) }
    else if (c < 0x10000) { bytes.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f)) }
    else { bytes.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 0x3f), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f)) }
  }
  const bitLen = bytes.length * 8
  bytes.push(0x80)
  while (bytes.length % 64 !== 56) bytes.push(0)
  for (let i = 0; i < 8; i++) bytes.push(Math.floor(bitLen / Math.pow(2, i * 8)) & 0xff)

  const w = []
  for (let i = 0; i < bytes.length; i += 4) {
    w.push(bytes[i] | (bytes[i + 1] << 8) | (bytes[i + 2] << 16) | (bytes[i + 3] << 24))
  }

  function add(x, y) {
    const l = (x & 0xffff) + (y & 0xffff)
    return (((x >> 16) + (y >> 16) + (l >> 16)) << 16) | (l & 0xffff)
  }
  function rol(n, s) { return (n << s) | (n >>> (32 - s)) }
  function F(x, y, z) { return (x & y) | (~x & z) }
  function G(x, y, z) { return (x & z) | (y & ~z) }
  function H(x, y, z) { return x ^ y ^ z }
  function I(x, y, z) { return y ^ (x | ~z) }
  function step(fn, a, b, c, d, x, s, t) { return add(rol(add(add(a, fn(b, c, d)), add(x, t)), s), b) }

  const T = []
  for (let i = 1; i <= 64; i++) T.push(Math.floor(Math.abs(Math.sin(i)) * 4294967296))
  const S = [7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21]
  const FN = [F, G, H, I]

  let a = 0x67452301, b = 0xefcdab89, c = 0x98badcfe, d = 0x10325476
  for (let k = 0; k < w.length; k += 16) {
    const aa = a, bb = b, cc = c, dd = d
    for (let round = 0; round < 4; round++) {
      const f = FN[round]
      const s = [S[round * 4], S[round * 4 + 1], S[round * 4 + 2], S[round * 4 + 3]]
      for (let j = 0; j < 16; j++) {
        let idx
        if (round === 0) idx = j
        else if (round === 1) idx = (5 * j + 1) % 16
        else if (round === 2) idx = (3 * j + 5) % 16
        else idx = (7 * j) % 16
        const x = w[k + idx]
        const t = T[round * 16 + j]
        if (j % 4 === 0) a = step(f, a, b, c, d, x, s[0], t)
        else if (j % 4 === 1) d = step(f, d, a, b, c, x, s[1], t)
        else if (j % 4 === 2) c = step(f, c, d, a, b, x, s[2], t)
        else b = step(f, b, c, d, a, x, s[3], t)
      }
    }
    a = add(a, aa); b = add(b, bb); c = add(c, cc); d = add(d, dd)
  }
  function hexWord(n) {
    let s = ''
    for (let i = 0; i < 4; i++) s += ('0' + ((n >> (i * 8)) & 0xff).toString(16)).slice(-2)
    return s
  }
  return hexWord(a) + hexWord(b) + hexWord(c) + hexWord(d)
}

async function getWbiKeys() {
  // nav 匿名可访问, 返回 wbi_img 图片地址, key 缓存 24h
  if (wbiKeys && Date.now() - wbiKeysAt < 86400000) return wbiKeys
  const res = await httpGet('https://api.bilibili.com/x/web-interface/nav',
    { 'User-Agent': UA, 'Referer': REFERER, 'Accept': 'application/json' }, 15)
  lastRequestAt = Date.now()
  const body = parseBody(unwrapResponse(res))
  if (!body || body.code !== 0 || !body.data || !body.data.wbi_img) {
    throw new Error('wbi key 获取失败')
  }
  const imgUrl = body.data.wbi_img.img_url
  const subUrl = body.data.wbi_img.sub_url
  const imgKey = imgUrl.substring(imgUrl.lastIndexOf('/') + 1).split('.')[0]
  const subKey = subUrl.substring(subUrl.lastIndexOf('/') + 1).split('.')[0]
  const orig = imgKey + subKey
  let mixin = ''
  for (let i = 0; i < 32; i++) mixin += orig.charAt(WBI_MIXIN_TAB[i])
  wbiKeys = mixin
  wbiKeysAt = Date.now()
  console.log('[bili] wbi key 获取成功')
  return wbiKeys
}

function encodeURIComponentRFC3986(s) {
  return encodeURIComponent(String(s)).replace(/[!'()*]/g, '')
}

// 返回带签名的完整 query 串: k=v&k=v&wts=..&w_rid=md5(...)
async function wbiQuery(params) {
  const mixin = await getWbiKeys()
  const p = {}
  for (const k in params) p[k] = params[k]
  p.wts = Math.floor(Date.now() / 1000)
  const keys = Object.keys(p).sort()
  const pairs = []
  for (let i = 0; i < keys.length; i++) {
    pairs.push(encodeURIComponentRFC3986(keys[i]) + '=' + encodeURIComponentRFC3986(p[keys[i]]))
  }
  const qs = pairs.join('&')
  return qs + '&w_rid=' + md5Utf8(qs + mixin)
}

async function getJson(url, referer) {
  await ensureCookie()
  if (Date.now() < cooldownUntil) {
    // 冷却期内直接失败, 且不延长冷却 (反复点击不会加重封禁)
    const secs = Math.ceil((cooldownUntil - Date.now()) / 1000)
    console.log('[bili] 风控冷却中, 剩余 ' + secs + 's, 本次直接放弃')
    const err = new Error('请求过于频繁, 请稍候约 ' + Math.ceil(secs / 60) + ' 分钟再试')
    err.rateLimited = true
    throw err
  }
  const headers = {
    'User-Agent': UA,
    'Referer': referer || REFERER,
    'Accept': 'application/json, text/plain, */*',
    'Accept-Language': 'zh-CN,zh;q=0.9'
  }
  let lastErr = null
  for (let attempt = 1; attempt <= 3; attempt++) {
    // 节流: 类人节奏, 与上一次请求至少间隔 1.2s
    const waitMs = lastRequestAt + 1200 - Date.now()
    if (waitMs > 0) await sleep(waitMs)

    let res
    try {
      res = await httpGet(url, headers, 15)
      lastRequestAt = Date.now()
    } catch (e) {
      lastErr = e
      console.log('[bili] 第' + attempt + '次传输失败: ' + (e && e.message ? e.message : safeJson(e)))
      if (attempt < 3) await sleep(attempt * 800)
      continue
    }
    try {
      const body = parseBody(unwrapResponse(res))
      // 业务码限流: 不重试
      if (body && (body.code === -352 || body.code === -412)) {
        throw rateLimitError('请求过于频繁, 请稍候片刻再试')
      }
      consecutiveLimits = 0 // 成功一次, 冷却升级清零
      return body
    } catch (e) {
      if (isRateLimitError(e)) {
        console.log('[bili] 限流, 不重试: ' + (e.message || e))
        throw e
      }
      lastErr = e
      // 关键诊断: 下载成功却报错时, 打出原生返回的真实形态/状态码
      console.log('[bili] 第' + attempt + '次响应异常: ' + (e && e.message ? e.message : e)
        + ' raw=' + safeJson(res).substring(0, 400))
      if (attempt < 3) await sleep(attempt * 800)
    }
  }
  throw new Error('网络请求失败: ' + (lastErr && lastErr.message ? lastErr.message : lastErr))
}

function stripTags(s) {
  return String(s == null ? '' : s).replace(/<[^>]*>/g, '')
}

function formatPlay(n) {
  const num = Number(n) || 0
  if (num >= 10000) return (num / 10000).toFixed(1) + '万'
  return String(num)
}

/**
 * 搜索哔哩哔哩视频
 * @param {string} keyword 关键词
 * @param {number} page 页码, 从 1 开始
 * @returns {Promise<Array<{bvid,title,author,playText,duration,pic}>>}
 */
export async function searchVideos(keyword, page) {
  if (!hasHttp()) throw new Error('当前固件不支持 http/net 请求')
  const kw = encodeURIComponent(keyword)
  const url = 'https://api.bilibili.com/x/web-interface/search/type'
    + '?search_type=video&keyword=' + kw
    + '&page=' + (page || 1) + '&pagesize=20'

  const body = await getJson(url, REFERER_SEARCH)
  if (body.code !== 0) {
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    throw new Error(body.message || ('接口错误 code=' + body.code))
  }

  const list = (body.data && body.data.result) || []
  const videos = []
  for (let i = 0; i < list.length; i++) {
    const item = list[i]
    if (!item || item.type !== 'video') continue
    let pic = item.pic || ''
    if (pic.indexOf('//') === 0) pic = 'https:' + pic
    videos.push({
      bvid: item.bvid || '',
      aid: item.aid || 0,
      title: stripTags(item.title),
      author: item.author || '',
      playText: formatPlay(item.play),
      duration: item.duration || '',
      pic: pic
    })
  }
  return videos
}

// QuickJS 20200705 不保证 Date.prototype.toISOString, 手工格式化
function formatDate(epochSec) {
  if (!epochSec) return ''
  const dt = new Date(epochSec * 1000)
  function pad(n) { return n < 10 ? '0' + n : '' + n }
  return dt.getFullYear() + '-' + pad(dt.getMonth() + 1) + '-' + pad(dt.getDate())
}

function formatDuration(sec) {
  const s = Math.max(0, Number(sec) || 0)
  const m = Math.floor(s / 60)
  const r = Math.floor(s % 60)
  return m + ':' + (r < 10 ? '0' : '') + r
}

/**
 * 获取视频详情
 * @param {string} bvid
 * @returns {Promise<{bvid,aid,title,pic,desc,author,duration,pubdateText,playText,danmakuText,likeText,coinText,favText,shareText}>}
 */
export async function getVideoDetail(bvid) {
  if (!hasHttp()) throw new Error('当前固件不支持 http/net 请求')
  const url = 'https://api.bilibili.com/x/web-interface/view?bvid=' + encodeURIComponent(bvid)

  const body = await getJson(url, 'https://www.bilibili.com/video/' + encodeURIComponent(bvid))
  if (body.code !== 0 || !body.data) {
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    if (body.code === -404) throw new Error('视频不存在或已删除')
    throw new Error(body.message || ('接口错误 code=' + body.code))
  }

  const d = body.data
  const st = d.stat || {}
  let pic = d.pic || ''
  if (pic.indexOf('//') === 0) pic = 'https:' + pic
  return {
    bvid: d.bvid || bvid,
    aid: d.aid || 0,
    title: d.title || '',
    pic: pic,
    desc: d.desc || '',
    author: (d.owner && d.owner.name) || '',
    duration: formatDuration(d.duration),
    pubdateText: formatDate(d.pubdate),
    playText: formatPlay(st.view),
    danmakuText: formatPlay(st.danmaku),
    likeText: formatPlay(st.like),
    coinText: formatPlay(st.coin),
    favText: formatPlay(st.favorite),
    shareText: formatPlay(st.share),
    mid: (d.owner && d.owner.mid) || 0,
    // 分 P (同稿件多段)
    pages: (d.pages || []).map(function (p) {
      return { page: p.page || 0, part: p.part || '', duration: formatDuration(p.duration) }
    }),
    // 合集 (不同稿件聚合)
    season: d.ugc_season ? {
      title: d.ugc_season.title || '',
      episodes: (((d.ugc_season.sections || [])[0] || {}).episodes || []).map(function (e) {
        return { bvid: e.bvid || '', title: e.title || '', aid: e.aid || 0 }
      })
    } : null
  }
}

/**
 * 相关推荐视频 (x/web-interface/archive/related, 匿名可用)
 */
export async function getRelatedVideos(bvid) {
  if (!hasHttp()) throw new Error('当前固件不支持 http/net 请求')
  const url = 'https://api.bilibili.com/x/web-interface/archive/related?bvid=' + encodeURIComponent(bvid)
  const body = await getJson(url, 'https://www.bilibili.com/video/' + encodeURIComponent(bvid))
  if (body.code !== 0) return [] // 推荐失败容忍, 不阻塞详情
  return (body.data || []).map(mapFeedItem)
}

function mapFeedItem(v) {
  let pic = v.pic || ''
  if (pic.indexOf('//') === 0) pic = 'https:' + pic
  return {
    bvid: v.bvid || '',
    aid: v.aid || 0,
    title: stripTags(v.title),
    author: v.author || (v.owner && v.owner.name) || '',
    playText: formatPlay(v.play !== undefined ? v.play : (v.stat && v.stat.view)),
    duration: typeof v.duration === 'number' ? formatDuration(v.duration) : (v.duration || ''),
    pic: pic
  }
}

/**
 * 全站热门视频 (无需登录)
 * @returns {Promise<Array<feedItem>>}
 */
export async function getPopular(page) {
  if (!hasHttp()) throw new Error('当前固件不支持 http/net 请求')
  const url = 'https://api.bilibili.com/x/web-interface/popular?pn=' + (page || 1) + '&ps=20'
  const body = await getJson(url, 'https://www.bilibili.com/v/popular/all')
  if (body.code !== 0) {
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    throw new Error(body.message || ('接口错误 code=' + body.code))
  }
  const list = (body.data && body.data.list) || []
  const videos = []
  for (let i = 0; i < list.length; i++) videos.push(mapFeedItem(list[i]))
  return videos
}

/**
 * UP主基本信息 (x/space/wbi/acc/info, wbi 签名)
 */
export async function getUpInfo(mid) {
  if (!hasHttp()) throw new Error('当前固件不支持 http/net 请求')
  const url = 'https://api.bilibili.com/x/space/wbi/acc/info?' + (await wbiQuery({ mid: mid }))
  const body = await getJson(url, 'https://space.bilibili.com/' + encodeURIComponent(mid))
  if (body.code !== 0 || !body.data) {
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    if (body.code === -352) throw new Error('接口风控, 无法获取UP主信息')
    throw new Error(body.message || ('接口错误 code=' + body.code))
  }
  const d = body.data
  let face = d.face || ''
  if (face.indexOf('//') === 0) face = 'https:' + face
  return {
    mid: d.mid || mid,
    name: d.name || '',
    sign: d.sign || '',
    face: face,
    levelText: 'Lv' + (d.level !== undefined ? d.level : '?')
  }
}

/**
 * UP主粉丝数 (x/relation/stat; 独立接口, 失败容忍)
 */
export async function getUpFans(mid) {
  const url = 'https://api.bilibili.com/x/relation/stat?vmid=' + encodeURIComponent(mid)
  const body = await getJson(url, 'https://space.bilibili.com/' + encodeURIComponent(mid))
  if (body.code !== 0 || !body.data) return ''
  return formatPlay(body.data.follower)
}

/**
 * UP主视频 (x/space/wbi/arc/search, wbi 签名)
 */
export async function getUpVideos(mid, page) {
  if (!hasHttp()) throw new Error('当前固件不支持 http/net 请求')
  const url = 'https://api.bilibili.com/x/space/wbi/arc/search?'
    + (await wbiQuery({ mid: mid, pn: page || 1, ps: 20, order: 'pubdate' }))
  const body = await getJson(url, 'https://space.bilibili.com/' + encodeURIComponent(mid) + '/video')
  if (body.code !== 0) {
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    if (body.code === -352) throw new Error('接口风控, 视频列表暂不可用')
    throw new Error(body.message || ('接口错误 code=' + body.code))
  }
  const list = (body.data && body.data.list && body.data.list.vlist) || []
  const videos = []
  for (let i = 0; i < list.length; i++) {
    const v = list[i]
    let pic = v.pic || ''
    if (pic.indexOf('//') === 0) pic = 'https:' + pic
    videos.push({
      bvid: v.bvid || '',
      aid: v.aid || 0,
      title: stripTags(v.title),
      author: v.author || '',
      playText: formatPlay(v.play),
      duration: v.length || '',
      pic: pic
    })
  }
  return videos
}
