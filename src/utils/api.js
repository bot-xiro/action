/**
 * Bilibili 网页端 API 封装
 * 基于 http JSAPI（返回二进制 → 转字符串 → JSON.parse）
 * 注意: 系统 JSAPI 必须用 '$jsapi/xxx' 语法导入（会转换为 $falcon.jsapi['xxx']）
 */
import http from '$jsapi/http'
import md5 from './md5.js'

const BASE = 'https://api.bilibili.com'

// Wbi 签名混入表（2023-10 后固定）
const MIXIN_KEY_ENC_TAB = [
  46, 47, 18, 2, 53, 8, 23, 32, 15, 50, 10, 31, 58, 3, 45, 35, 27, 43, 5, 49, 33, 9, 42, 19, 29, 28, 14, 39, 12, 38, 41, 13, 37, 48, 7, 16, 24, 55, 40, 61, 26, 17, 0, 1, 60, 51, 30, 4, 22, 25, 54, 21, 56, 59, 6, 63, 57, 62, 11, 36, 20, 34, 44, 52
]

// 缓存 wbi keys（img_key/sub_key 每日更替）
let _imgKey = ''
let _subKey = ''
let _navTime = 0

function getMixinKey(orig) {
  return MIXIN_KEY_ENC_TAB.map(n => orig[n]).join('').slice(0, 32)
}

function bytesToStr(res) {
  // 设备实测: http.request 返回 { result: "<响应体字符串>" }
  // 兼容文档所述的二进制数组场景
  if (res && typeof res === 'object') {
    if (typeof res.result === 'string') {
      return res.result
    }
    if (res.result && res.result.length) {
      return bytesFromArray(res.result)
    }
  }
  if (res && (res.length !== undefined || res.byteLength !== undefined)) {
    return bytesFromArray(res)
  }
  return ''
}

function bytesFromArray(arr) {
  const bytes = new Uint8Array(arr)
  let str = ''
  // 分批转换避免调用栈溢出（大响应）
  const CHUNK = 8192
  for (let i = 0; i < bytes.length; i += CHUNK) {
    str += String.fromCharCode.apply(null, bytes.subarray(i, i + CHUNK))
  }
  return str
}

async function request(url, options) {
  options = options || {}
  let res
  try {
    res = await http.request({
      url: url,
      method: options.method || 'GET',
      headers: options.headers || [
        'User-Agent:Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
        'Referer:https://www.bilibili.com'
      ],
      data: options.data || '',
      timeout: options.timeout || 10
    })
  } catch (e) {
    console.warn('[api] http.request failed: ' + url + ' :: ' + JSON.stringify(e))
    throw new Error('http.request: ' + JSON.stringify(e))
  }
  const str = bytesToStr(res)
  if (!str || str.length === 0) {
    console.warn('[api] empty response: ' + url)
    throw new Error('empty response')
  }
  try {
    return JSON.parse(str)
  } catch (e) {
    console.warn('[api] JSON.parse failed: ' + url + ' :: ' + str.substring(0, 200))
    throw new Error('JSON.parse: ' + e.message)
  }
}

async function getWbiKeys(forceRefresh) {
  // 缓存 12 小时
  if (!forceRefresh && _imgKey && _subKey && Date.now() - _navTime < 12 * 3600 * 1000) {
    return { imgKey: _imgKey, subKey: _subKey }
  }
  const data = await request(BASE + '/x/web-interface/nav', { timeout: 8 })
  // 注意: 设备端无登录态，nav 返回 code=-101（账号未登录），但 wbi_img 仍然下发。
  // 因此不要求 code===0，只要 data.data.wbi_img 存在即可提取 keys。
  if (data && data.data && data.data.wbi_img) {
    // img_url 形如 https://i0.hdslb.com/bfs/wbi/xxx.png
    _imgKey = data.data.wbi_img.img_url.split('/').pop().split('.')[0]
    _subKey = data.data.wbi_img.sub_url.split('/').pop().split('.')[0]
    _navTime = Date.now()
    return { imgKey: _imgKey, subKey: _subKey }
  }
  throw new Error('getWbiKeys failed: ' + (data ? data.code : 'no data'))
}

/**
 * 生成 wbi 签名 query 字符串（不含 ?）
 * @param {Object} params 原始参数
 */
async function encWbi(params) {
  const { imgKey, subKey } = await getWbiKeys()
  const mixinKey = getMixinKey(imgKey + subKey)
  const currTime = Math.round(Date.now() / 1000)
  const chrFilter = /[!'()*]/g
  const merged = Object.assign({}, params, { wts: currTime })
  const query = Object.keys(merged)
    .sort()
    .map(key => {
      const value = String(merged[key]).replace(chrFilter, '')
      return encodeURIComponent(key) + '=' + encodeURIComponent(value)
    })
    .join('&')
  const wRid = md5(query + mixinKey)
  return query + '&w_rid=' + wRid
}

/**
 * 热门视频列表（无需签名）
 * @param {number} pn 页码
 * @param {number} ps 每页数量(1-20)
 */
async function getPopular(pn, ps) {
  const url = BASE + '/x/web-interface/popular?pn=' + (pn || 1) + '&ps=' + (ps || 20)
  const data = await request(url, { timeout: 10 })
  if (data && data.code === 0) {
    return data.data
  }
  throw new Error('getPopular failed: ' + (data ? data.code : 'no data'))
}

/**
 * 视频信息（无需签名）
 * @param {string} bvid
 */
async function getVideoInfo(bvid) {
  const url = BASE + '/x/web-interface/view?bvid=' + encodeURIComponent(bvid)
  const data = await request(url, { timeout: 10 })
  if (data && data.code === 0) {
    return data.data
  }
  throw new Error('getVideoInfo failed: ' + (data ? data.code : 'no data'))
}

/**
 * 播放地址（需 Wbi 签名）
 * @param {string} bvid
 * @param {number|string} cid
 * @param {number} qn 清晰度 32/64/80/116
 * @param {number} fnval 16=DASH, 1=MP4
 */
async function getPlayUrl(bvid, cid, qn, fnval) {
  const params = {
    bvid: bvid,
    cid: cid,
    qn: qn || 64,
    fnval: fnval || 1,
    fourk: 0
  }
  // 优先使用 wbi 签名；设备实测 nav 返回 -101 时 playurl 不带签名也能成功，
  // 因此 wbi 失败时降级为无签名请求。
  let url
  try {
    const query = await encWbi(params)
    url = BASE + '/x/player/wbi/playurl?' + query
  } catch (e) {
    console.warn('[api] wbi sign failed, fallback unsigned: ' + (e && e.message ? e.message : JSON.stringify(e)))
    const qs = Object.keys(params)
      .sort()
      .map(k => encodeURIComponent(k) + '=' + encodeURIComponent(params[k]))
      .join('&')
    url = BASE + '/x/player/wbi/playurl?' + qs
  }
  const data = await request(url, { timeout: 10 })
  if (data && data.code === 0) {
    return data.data
  }
  console.warn('[api] getPlayUrl bad code: ' + url.substring(0, 150) + ' :: ' + JSON.stringify(data).substring(0, 300))
  throw new Error('getPlayUrl failed: ' + (data ? data.code : 'no data') + ' msg=' + (data ? data.message : ''))
}

/**
 * 搜索（需 Wbi 签名）
 * @param {string} keyword
 * @param {number} page
 */
async function searchVideo(keyword, page) {
  const params = {
    search_type: 'video',
    keyword: keyword,
    page: page || 1
  }
  const query = await encWbi(params)
  const url = BASE + '/x/web-interface/wbi/search/type?' + query
  const data = await request(url, { timeout: 10 })
  if (data && data.code === 0) {
    return data.data
  }
  throw new Error('searchVideo failed: ' + (data ? data.code : 'no data'))
}

/**
 * 相关视频推荐（无需签名）
 * @param {string} bvid
 */
async function getRelated(bvid) {
  const url = BASE + '/x/web-interface/archive/related?bvid=' + encodeURIComponent(bvid)
  const data = await request(url, { timeout: 10 })
  if (data && data.code === 0) {
    return data.data || []
  }
  throw new Error('getRelated failed: ' + (data ? data.code : 'no data'))
}

export default {
  getPopular: getPopular,
  getVideoInfo: getVideoInfo,
  getPlayUrl: getPlayUrl,
  searchVideo: searchVideo,
  getRelated: getRelated,
  encWbi: encWbi
}
