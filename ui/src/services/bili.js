// 哔哩哔哩网络服务
// 传输层: bilinet 原生模块 httpGet (popen 调设备自带 /bin/curl,
//   固定浏览器 UA + Referer https://www.bilibili.com), 同步返回响应体字符串.
// 真机实测背景 (home 项目 src/utils/api.js 结论, 同型号设备):
//   - 系统 http JSAPI 不发送自定义 header, UA/Referer 全丢,
//     wbi 类/风控敏感接口返回 v_voucher 空壳 (code=0 无 data)
//   - curl 携带浏览器 UA + Referer 后, popular/view/search/space 全部正常
//   - 无 Cookie 态 (无 buvid3) 反而绕开部分风控, 故不再取手指纹

import { bilinet } from 'bilinet'

function hasHttp() {
  return !!(bilinet && typeof bilinet.httpGet === 'function')
}

// 同步原生 GET -> JSON body; 服务器返回什么就透传什么, 业务 code 由调用方判断
function getJson(url, timeoutSec) {
  const s = bilinet.httpGet(url, timeoutSec || 15)
  console.log('[bili] GET ' + url.replace(/(&|\?)w_rid=[^&]+/, '').replace(/(&|\?)wts=[^&]+/, '') + ' -> ' + (s ? s.length : 0) + 'B')
  if (!s) { console.log('[bili] GET 空响应'); throw new Error('请求失败 (空响应)') }
  try {
    return JSON.parse(s)
  } catch (e) {
    // 非 JSON: 风控 HTML 页 / 网关错误页等, 透出真实开头便于诊断
    console.log('[bili] 非JSON body: ' + String(s).substring(0, 300))
    if (String(s).indexOf('<!DOCTYPE') === 0 || String(s).indexOf('<html') === 0) {
      throw new Error('接口被风控拦截 (风控验证页)')
    }
    throw new Error('接口返回非 JSON: ' + String(s).substring(0, 120))
  }
}

// 结果缓存 (减少重复请求 = 直接降低风控触发率)
const resultCache = {} // key -> { at, data }
function cacheGet(key, ttlMs) {
  const c = resultCache[key]
  if (c && Date.now() - c.at < ttlMs) return c.data
  return null
}
function cacheSet(key, data) {
  resultCache[key] = { at: Date.now(), data: data }
}

// ================= wbi 签名 (与官方文档 misc/sign/wbi.md 一致) =================
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
        else if (j % 4 === 2) c = step(f, c, d, a, x, s[2], t)
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
  // nav 匿名可访问, 返回 wbi_img 图片地址, key 缓存 12h (随官方前端节奏)
  if (wbiKeys && Date.now() - wbiKeysAt < 12 * 3600 * 1000) return wbiKeys
  const body = getJson('https://api.bilibili.com/x/web-interface/nav', 10)
  // 匿名 nav 返回 code=-101(账号未登录), 但 data.wbi_img 仍然有效
  if (!body || !body.data || !body.data.wbi_img) {
    throw new Error('wbi key 获取失败 (code=' + (body && body.code) + ')')
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

function stripTags(s) {
  return String(s == null ? '' : s).replace(/<[^>]*>/g, '')
}

function formatPlay(n) {
  const num = Number(n) || 0
  if (num >= 10000) return (num / 10000).toFixed(1) + '万'
  return String(num)
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
 * 获取视频播放地址 (x/player/playurl, MP4 durl 形态, 供 gstplayer 硬解播放)
 * @param {string} bvid
 * @param {number} cid
 * @returns {Promise<{url:string, duration:number}>} duration 为毫秒 (timelength)
 */
export async function getPlayUrl(bvid, cid) {
  if (!hasHttp()) throw new Error('当前固件不支持 http 请求 (缺少 bilinet 模块)')
  // 播放地址 10 分钟内有效, 同参数缓存
  const ckey = 'playurl:' + bvid + ':' + cid
  const cached = cacheGet(ckey, 600000)
  if (cached) return cached
  const url = 'https://api.bilibili.com/x/player/playurl?bvid=' + encodeURIComponent(bvid)
    + '&cid=' + encodeURIComponent(cid) + '&qn=32&fnval=0&fnver=0&fourk=0'
  const body = getJson(url, 15)
  if (body.code !== 0 || !body.data) {
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    throw new Error(body.message || ('播放地址接口错误 code=' + body.code))
  }
  const durl = body.data.durl || []
  if (durl.length === 0 || !durl[0].url) throw new Error('没有可用的 MP4 播放地址')
  let playUrl = durl[0].url
  if (playUrl.indexOf('//') === 0) playUrl = 'https:' + playUrl
  // gstplayer 的 souphttpsrc 走 TLS, 尽量使用 https 直连地址
  if (playUrl.indexOf('http://') === 0) playUrl = 'https://' + playUrl.substring(7)
  const out = { url: playUrl, duration: Number(body.data.timelength) || 0 }
  cacheSet(ckey, out)
  return out
}

// B站图片服务按需裁切 (大幅缩短列表首次渲染的下载+解码耗时)
function thumb(url, w, h) {
  if (!url) return ''
  if (url.indexOf('//') === 0) url = 'https:' + url
  // 已经是缩略尺寸的不重复追加
  if (url.indexOf('@') > 0) return url
  return url + '@' + w + 'w_' + h + 'h_1c.jpg'
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
    pic: thumb(pic, 400, 250)
  }
}

/**
 * 搜索哔哩哔哩视频
 * @param {string} keyword 关键词
 * @param {number} page 页码, 从 1 开始
 * @returns {Promise<Array<{bvid,title,author,playText,duration,pic}>>}
 */
export async function searchVideos(keyword, page) {
  if (!hasHttp()) throw new Error('当前固件不支持 http 请求 (缺少 bilinet 模块)')
  // 同词同页缓存 2 分钟
  const ckey = 'search:' + keyword + ':' + (page || 1)
  const cached = cacheGet(ckey, 120000)
  if (cached) { console.log('[bili] 搜索命中缓存, 不发请求'); return cached }
  // wbi 签名 + dm_* 反爬参数 (官方前端同款)
  const url = 'https://api.bilibili.com/x/web-interface/wbi/search/type?'
    + (await wbiQuery({
      search_type: 'video',
      keyword: keyword,
      page: page || 1,
      pagesize: 20,
      dm_img_list: '[]',
      dm_img_str: 'V2ViR0wgMS4wIChPcGVuR0wgRVMgMi4wIENocm9taXVtKQ',
      dm_cover_img_str: 'QU5HTEUgKEludGVsLCBJbnRlbChSKSBVSEQgR3JhcGhpY3MgNjMwKCAweDAwMDAzRTkxKSBEaXJlY3QzRDExIHZzXzVfMCBwc181XzAsIEQzRDExKUdvb2dsZSBJbmMuIChJbnRlbCk',
      dm_img_inter: '{"ds":[],"wh":[0,0,0],"of":[0,0,0]}'
    }))

  const body = getJson(url, 15)
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
      pic: thumb(pic, 400, 250)
    })
  }
  cacheSet(ckey, videos)
  return videos
}

/**
 * 获取视频详情
 * @param {string} bvid
 * @returns {Promise<{bvid,aid,title,pic,desc,author,duration,pubdateText,playText,danmakuText,likeText,coinText,favText,shareText}>}
 */
export async function getVideoDetail(bvid) {
  if (!hasHttp()) throw new Error('当前固件不支持 http 请求 (缺少 bilinet 模块)')
  // 详情缓存 5 分钟
  const ckey = 'view:' + bvid
  const cached = cacheGet(ckey, 300000)
  if (cached) { console.log('[bili] 详情命中缓存, 不发请求'); return cached }
  // 详情带 wbi 签名, 与官方前端一致 (wbi/view 为 wbi 版本接口)
  const url = 'https://api.bilibili.com/x/web-interface/wbi/view?'
    + (await wbiQuery({ bvid: bvid }))

  const body = getJson(url, 15)
  if (body.code !== 0 || !body.data) {
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    if (body.code === -404) throw new Error('视频不存在或已删除')
    throw new Error(body.message || ('接口错误 code=' + body.code))
  }

  const d = body.data
  const st = d.stat || {}
  let pic = d.pic || ''
  if (pic.indexOf('//') === 0) pic = 'https:' + pic
  const out = {
    bvid: d.bvid || bvid,
    aid: d.aid || 0,
    title: d.title || '',
    pic: thumb(pic, 640, 400),
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
      return { page: p.page || 0, cid: p.cid || 0, part: p.part || '', duration: formatDuration(p.duration) }
    }),
    // 合集 (不同稿件聚合)
    season: d.ugc_season ? {
      title: d.ugc_season.title || '',
      episodes: (((d.ugc_season.sections || [])[0] || {}).episodes || []).map(function (e) {
        return { bvid: e.bvid || '', title: e.title || '', aid: e.aid || 0 }
      })
    } : null
  }
  cacheSet('view:' + bvid, out)
  return out
}

/**
 * 相关推荐视频 (x/web-interface/archive/related, 匿名可用)
 */
export async function getRelatedVideos(bvid) {
  if (!hasHttp()) throw new Error('当前固件不支持 http 请求 (缺少 bilinet 模块)')
  const ckey = 'related:' + bvid
  const cached = cacheGet(ckey, 300000)
  if (cached) return cached
  const url = 'https://api.bilibili.com/x/web-interface/archive/related?bvid=' + encodeURIComponent(bvid)
  const body = getJson(url, 15)
  if (body.code !== 0) return [] // 推荐失败容忍, 不阻塞详情
  const out = (body.data || []).map(mapFeedItem)
  cacheSet(ckey, out)
  return out
}

/**
 * 全站热门视频 (无需登录)
 * @returns {Promise<Array<feedItem>>}
 */
export async function getPopular(page) {
  if (!hasHttp()) throw new Error('当前固件不支持 http 请求 (缺少 bilinet 模块)')
  const ckey = 'popular:' + (page || 1)
  const cached = cacheGet(ckey, 60000)
  if (cached) return cached
  const url = 'https://api.bilibili.com/x/web-interface/popular?pn=' + (page || 1) + '&ps=20'
  const body = getJson(url, 15)
  if (body.code !== 0) {
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    throw new Error(body.message || ('接口错误 code=' + body.code))
  }
  const list = (body.data && body.data.list) || []
  const videos = []
  for (let i = 0; i < list.length; i++) videos.push(mapFeedItem(list[i]))
  cacheSet(ckey, videos)
  return videos
}

/**
 * UP主基本信息 (x/space/wbi/acc/info, wbi 签名)
 */
export async function getUpInfo(mid) {
  if (!hasHttp()) throw new Error('当前固件不支持 http 请求 (缺少 bilinet 模块)')
  const ckey = 'upinfo:' + mid
  const cached = cacheGet(ckey, 600000)
  if (cached) return cached
  // 先走 acc/info (字段全)
  try {
    const url = 'https://api.bilibili.com/x/space/wbi/acc/info?' + (await wbiQuery({ mid: mid }))
    const body = getJson(url, 15)
    if (body.code !== 0 || !body.data) {
      if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
      if (body.code === -352) throw new Error('接口风控, 无法获取UP主信息')
      throw new Error(body.message || ('接口错误 code=' + body.code))
    }
    const d = body.data
    let face = d.face || ''
    if (face.indexOf('//') === 0) face = 'https:' + face
    const out = {
      mid: d.mid || mid,
      name: d.name || '',
      sign: d.sign || '',
      face: thumb(face, 96, 96),
      levelText: 'Lv' + (d.level !== undefined ? d.level : '?')
    }
    cacheSet('upinfo:' + mid, out)
    return out
  } catch (accErr) {
    console.log('[bili] acc/info 失败, 降级 x/web-interface/card: ' + (accErr && accErr.message))
  }
  // 降级: x/web-interface/card (免登录基本资料接口, 风控比空间接口宽松)
  // 注意: 字段嵌套在 data.card 下, level 在 level_info.current_level
  const cardBody = getJson('https://api.bilibili.com/x/web-interface/card?mid=' + encodeURIComponent(mid), 15)
  if (cardBody.code !== 0 || !cardBody.data) {
    throw new Error(cardBody.message || ('接口错误 code=' + cardBody.code))
  }
  const cd = cardBody.data.card || cardBody.data
  const lv = cd.level_info && cd.level_info.current_level !== undefined
    ? cd.level_info.current_level : cd.level
  let face = cd.face || ''
  if (face.indexOf('//') === 0) face = 'https:' + face
  const cardOut = {
    mid: cd.mid || mid,
    name: cd.name || '',
    sign: cd.sign || '',
    face: thumb(face, 96, 96),
    levelText: 'Lv' + (lv !== undefined ? lv : '?')
  }
  cacheSet('upinfo:' + mid, cardOut)
  return cardOut
}

/**
 * UP主粉丝数 (x/relation/stat; 独立接口, 失败容忍)
 */
export async function getUpFans(mid) {
  const url = 'https://api.bilibili.com/x/relation/stat?vmid=' + encodeURIComponent(mid)
  const body = getJson(url, 10)
  if (body.code !== 0 || !body.data) return ''
  return formatPlay(body.data.follower)
}

/**
 * UP主视频 (x/space/wbi/arc/search, wbi 签名; 风控时降级 x/series/recArchivesByKeywords)
 */
export async function getUpVideos(mid, page) {
  if (!hasHttp()) throw new Error('当前固件不支持 http 请求 (缺少 bilinet 模块)')
  const ckey = 'upvideos:' + mid + ':' + (page || 1)
  const cached = cacheGet(ckey, 120000)
  if (cached) return cached
  // 主路径: 空间投稿接口 (wbi 签名)
  try {
    const url = 'https://api.bilibili.com/x/space/wbi/arc/search?'
      + (await wbiQuery({ mid: mid, pn: page || 1, ps: 20, order: 'pubdate' }))
    const body = getJson(url, 15)
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
        pic: thumb(pic, 400, 250)
      })
    }
    cacheSet(ckey, videos)
    return videos
  } catch (arcErr) {
    console.log('[bili] arc/search 失败, 降级 recArchivesByKeywords: ' + (arcErr && arcErr.message))
  }
  // 降级: x/series/recArchivesByKeywords (不需要 wbi 签名, 官方文档注"暂未发现风控校验")
  const url2 = 'https://api.bilibili.com/x/series/recArchivesByKeywords?mid='
    + encodeURIComponent(mid) + '&keywords=&ps=20&pn=' + (page || 1) + '&orderby=pubdate'
  const body2 = getJson(url2, 15)
  if (body2.code !== 0) {
    throw new Error(body2.message || ('接口错误 code=' + body2.code))
  }
  const list2 = (body2.data && body2.data.archives) || []
  const videos2 = []
  for (let i = 0; i < list2.length; i++) {
    const v = list2[i]
    let pic = v.pic || ''
    if (pic.indexOf('//') === 0) pic = 'https:' + pic
    videos2.push({
      bvid: v.bvid || '',
      aid: v.aid || 0,
      title: stripTags(v.title),
      author: '',
      playText: formatPlay(v.stat && v.stat.view),
      duration: v.duration ? formatDuration(v.duration) : '',
      pic: thumb(pic, 400, 250)
    })
  }
  cacheSet(ckey, videos2)
  return videos2
}
