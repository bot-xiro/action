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

  let res
  try {
    res = await httpGet(url, { 'User-Agent': UA, 'Referer': REFERER }, 15)
  } catch (e) {
    throw new Error('网络请求失败: ' + (e && e.message ? e.message : e))
  }

  // 诊断: 记录返回值形态 (设备日志 tag console().log)
  try {
    let desc
    if (isBinary(res)) desc = 'Binary(len=' + (res.byteLength || res.length) + ')'
    else if (res && typeof res === 'object') desc = 'keys=[' + Object.keys(res).join(',') + ']'
    else desc = 'typeof=' + typeof res + ' value=' + String(res).substring(0, 120)
    console.log('[bili] response ' + desc)
  } catch (e) { console.log('[bili] response 检查异常: ' + e) }

  const body = parseBody(unwrapResponse(res))
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
