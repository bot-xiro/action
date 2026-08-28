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

async function httpGet(url, headersMap, timeoutSec) {
  const jsapi = $falcon.jsapi
  const headers = []
  for (const k in headersMap) headers.push(k + ': ' + headersMap[k])
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
  if (res.error) throw new Error(typeof res.error === 'string' ? res.error : '网络请求失败')
  const status = res.statusCode || res.status
  const body = res.data !== undefined ? res.data : (res.body !== undefined ? res.body : res.result)
  if (status !== undefined && (status < 200 || status >= 300)) {
    throw new Error('HTTP ' + status)
  }
  if (body === undefined) throw new Error('空响应')
  return body
}

const UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36'
const REFERER = 'https://www.bilibili.com'

function sleep(ms) {
  return new Promise(function (resolve) { setTimeout(resolve, ms) })
}

// 带有限重试的 GET-JSON: 设备网络偶发抖动 (真机观察到间歇性
// "网络请求失败"), 传输层错误最多重试 3 次, 退避 800/1600ms;
// 已拿到响应后的业务错误 (HTTP 状态 / 解析失败) 也按同一路径重试,
// 单次 15s 收发不变, 最坏 15*3s 才报失败
async function getJson(url) {
  let lastErr = null
  for (let attempt = 1; attempt <= 3; attempt++) {
    try {
      const res = await httpGet(url, { 'User-Agent': UA, 'Referer': REFERER }, 15)
      return parseBody(unwrapResponse(res))
    } catch (e) {
      lastErr = e
      console.log('[bili] 第' + attempt + '次请求失败: ' + (e && e.message ? e.message : e))
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

  const body = await getJson(url)
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

  const body = await getJson(url)
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
    shareText: formatPlay(st.share)
  }
}
