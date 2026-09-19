// 哔哩哔哩网络服务
// 传输层: bilinet 原生模块 httpGet (popen 调设备自带 /bin/curl,
//   固定浏览器 UA + Referer https://www.bilibili.com), 同步返回响应体字符串.
// 真机实测背景 (home 项目 src/utils/api.js 结论, 同型号设备):
//   - 系统 http JSAPI 不发送自定义 header, UA/Referer 全丢,
//     wbi 类/风控敏感接口返回 v_voucher 空壳 (code=0 无 data)
//   - curl 携带浏览器 UA + Referer 后, popular/view/search/space 全部正常
//   - 无 Cookie 态 (无 buvid3) 反而绕开部分风控, 故不再取手指纹

import { bilinet } from 'bilinet'
import * as auth from './auth.js'
import { log } from './log.js'

function hasHttp() {
  return !!(bilinet && typeof bilinet.httpGet === 'function')
}

// 同步原生 GET -> JSON body; 服务器返回什么就透传什么, 业务 code 由调用方判断.
// 登录态存在时自动携带 Cookie 头 (动态/评论/用户信息等登录接口依赖).
function getJson(url, timeoutSec) {
  const headers = auth.hasCookie() ? [auth.cookieHeader()] : undefined
  const s = headers
    ? bilinet.httpGet(url, timeoutSec || 15, headers)
    : bilinet.httpGet(url, timeoutSec || 15)
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

// 同步原生 POST 表单 -> JSON body (评论发送等需要登录 + csrf 的接口)
function postJson(url, data, timeoutSec) {
  const headers = ['Content-Type: application/x-www-form-urlencoded']
  if (auth.hasCookie()) headers.push(auth.cookieHeader())
  const s = bilinet.httpPost(url, data, timeoutSec || 15, headers)
  console.log('[bili] POST ' + url.substring(0, 80) + ' -> ' + (s ? s.length : 0) + 'B')
  if (!s) throw new Error('请求失败 (空响应)')
  try {
    return JSON.parse(s)
  } catch (e) {
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
  // B 站对部分视频隐藏播放量, 返回 -1 (动态流 stat.play 常见), 显示 '--'
  if (num < 0) return '--'
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
  // 设备 image 组件对 http:// 的加载不可靠 (0.8.6 动态封面不显示的主嫌疑),
  // 统一升级成 https —— B 站图床 i0.hdslb.com 支持 https, 无兼容风险.
  if (url.indexOf('http://') === 0) url = 'https://' + url.substring(7)
  // 已经是缩略尺寸的不重复追加
  if (url.indexOf('@') > 0) return url
  return url + '@' + w + 'w_' + h + 'h_1c.jpg'
}

// ================= 评论表情 (B 站 emote 优先 + unicode emoji 内置图) =================

// unicode emoji 设备字体渲染不出 (豆腐块), 常用的转成内置 PNG (CLI 编译时打包进应用,
// 不依赖网络/CDN —— 防止系统不显示). key 为 twemoji 文件名 (不带 .png).
// B 站独有表情优先级更高: content.emote 有映射的 (含 emoji 字符 key) 一律走 B 站 CDN 图.
// 内置映射由页面层传入 (scanEmoji 的 builtin 参数) —— aiot-cli 只编译 .vue 文件里的
// require png, .js 文件里的 require 原样保留会在 QuickJS 运行时崩掉 (无 require 函数).

// 「强 emoji」码点 (转图片); ©®™ 等弱符号单独出现时保持文本
function isStrongEmoji(cp) {
  return (cp >= 0x1f000 && cp <= 0x1ffff) ||   // 主体 emoji (含 🤣 ⭐ 等)
    (cp >= 0x2600 && cp <= 0x27bf) ||           // ☀ ❤ ✨ 等
    (cp >= 0x2b00 && cp <= 0x2bff) ||           // ⭐ ⬆ 等
    (cp >= 0x231a && cp <= 0x231b) || cp === 0x2328 ||
    (cp >= 0x23e9 && cp <= 0x23fa) || cp === 0x24c2 ||
    (cp >= 0x25aa && cp <= 0x25fe) ||           // ▶ ▪ 等
    (cp >= 0x2934 && cp <= 0x2935) ||
    cp === 0x3030 || cp === 0x303d || cp === 0x3297 || cp === 0x3299
}

// emoji 序列的起始码点 (含 ©®™, 它们带变体选择符时是 emoji)
function isEmojiStart(cp) {
  return isStrongEmoji(cp) || cp === 0x00a9 || cp === 0x00ae || cp === 0x2122
}

// 连接符/选择符 (并入当前 emoji 序列)
function isEmojiJoiner(cp) {
  return cp === 0x200d || cp === 0xfe0f || cp === 0xfe0e || cp === 0x20e3
}

// 文本段追加 (过滤空段)
function pushText(segs, s) {
  if (s) segs.push({ t: 0, v: s })
}

// 扫描文本: unicode emoji 转 twemoji 图片段, 其余为文字段.
// segment: { t:0, v:文字 } | { t:1, v:图URL, w, h }
function scanEmoji(text, segs, builtin) {
  if (!text) return
  let buf = ''
  let i = 0
  while (i < text.length) {
    const cp = text.codePointAt(i)
    if (!isEmojiStart(cp)) {
      buf += text.charAt(i)
      i++
      continue
    }
    // 收集 emoji 序列: 起始字符 + 连接符/后续 emoji.
    // 仅 ZWJ 连接的 emoji 属于同一序列 (👍❤️ 相邻无 ZWJ 必须切开, 否则文件名不存在)
    let end = i + (cp > 0xffff ? 2 : 1)
    let prev = cp
    while (end < text.length) {
      const c2 = text.codePointAt(end)
      if (isEmojiJoiner(c2)) {
        end += c2 > 0xffff ? 2 : 1
        prev = c2
        continue
      }
      if (isEmojiStart(c2) && prev === 0x200d) {
        end += c2 > 0xffff ? 2 : 1
        prev = c2
        continue
      }
      break
    }
    const slice = text.substring(i, end)
    // twemoji 文件名规则: codepoints 去掉 FE0F, 其余 '-' 连接 (ZWJ/keycap 保留)
    let convert = false
    const names = []
    for (let j = 0; j < slice.length; j++) {
      const c = slice.codePointAt(j)
      if (c > 0xffff) j++   // 代理对: 码点读全, 下标多跳 1
      if (c === 0xfe0f) { convert = true; continue }
      if (c === 0xfe0e) continue   // 文本变体: 保持字体渲染
      if (c === 0x200d || c === 0x20e3) { convert = true; names.push(c.toString(16)); continue }
      if (isStrongEmoji(c)) convert = true
      names.push(c.toString(16))
    }
    if (convert) {
      // 优先内置图片 (打包文件, 不依赖网络); 未内置的保持原字符
      const img = builtin ? builtin[names.join('-')] : null
      if (img) {
        pushText(segs, buf)
        buf = ''
        segs.push({ t: 1, v: img, w: 30, h: 30 })
      } else {
        buf += slice
      }
    } else {
      buf += slice   // ©®™ 单独出现, 保持文本
    }
    i = end
  }
  pushText(segs, buf)
}

// 解析评论内容: B 站表情占位符 ([dog] 等) 按 content.emote 映射成图片,
// 剩余文本再做 emoji 切分. emote 图按 API 给的原始尺寸等比缩到高 34 内.
export function parseMessage(text, emotes, builtin) {
  const segs = []
  const rest = String(text == null ? '' : text).replace(/\r/g, '')
  const emoteMap = emotes || {}
  const keys = []
  for (const k in emoteMap) keys.push(k)
  let pieces
  if (keys.length > 0) {
    // 循环找最早出现的占位符切分 (emote key 自带方括号, 完整匹配不互吃)
    pieces = []
    let pos = 0
    while (true) {
      let best = -1
      let bestKey = ''
      for (let i = 0; i < keys.length; i++) {
        const p = rest.indexOf(keys[i], pos)
        if (p >= 0 && (best < 0 || p < best)) { best = p; bestKey = keys[i] }
      }
      if (best < 0) break
      pieces.push({ emote: false, s: rest.substring(pos, best) })
      pieces.push({ emote: true, key: bestKey })
      pos = best + bestKey.length
    }
    pieces.push({ emote: false, s: rest.substring(pos) })
  } else {
    pieces = [{ emote: false, s: rest }]
  }
  for (let i = 0; i < pieces.length; i++) {
    if (!pieces[i].emote) {
      scanEmoji(pieces[i].s, segs, builtin)
      continue
    }
    const em = emoteMap[pieces[i].key] || {}
    let url = em.url || ''
    if (url.indexOf('//') === 0) url = 'https:' + url
    if (url.indexOf('http://') === 0) url = 'https://' + url.substring(7)
    if (!url) continue
    const w = Number(em.width) || 30
    const h = Number(em.height) || 30
    const scale = Math.min(1, 34 / Math.max(w, h))
    segs.push({ t: 1, v: url, w: Math.round(w * scale), h: Math.round(h * scale) })
  }
  return segs
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
 * @param {boolean} [noCache] 跳过缓存强拉 (点赞/投币等操作后刷新状态用)
 * @returns {Promise<{bvid,aid,title,pic,desc,author,duration,pubdateText,playText,danmakuText,likeText,coinText,favText,shareText,reqLike,reqCoin,reqFav}>}
 */
export async function getVideoDetail(bvid, noCache) {
  if (!hasHttp()) throw new Error('当前固件不支持 http 请求 (缺少 bilinet 模块)')
  // 详情缓存 5 分钟 (noCache: 交互操作后强拉最新状态)
  const ckey = 'view:' + bvid
  if (!noCache) {
    const cached = cacheGet(ckey, 300000)
    if (cached) { console.log('[bili] 详情命中缓存, 不发请求'); return cached }
  }
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
  // req_user: 登录态下本账号与该稿件的交互状态 (like=已赞, coin>0=已投币, favorite=已收藏)
  const ru = d.req_user || {}
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
    // 交互状态 (未登录时 req_user 缺失, 全 false)
    reqLike: ru.like === 1,
    reqCoin: (Number(ru.coin) || 0) > 0,
    reqFav: ru.favorite === 1,
    // 稍后再看状态 view 接口不返回, 由页面异步查 isInToView 后回填;
    // 这里先声明字段 (Vue2 新增属性不响应, 必须建对象时就带上)
    reqToview: false,
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
 * 真主页推荐流 (x/web-interface/index/top/feed/rcmd, 无需登录, 官网首页同款).
 * 分页游标 fresh_idx 递增即可无限翻页, 返回空数组即到底.
 * @returns {Promise<Array<feedItem>>}
 */
export async function getRecommend(page) {
  if (!hasHttp()) throw new Error('当前固件不支持 http 请求 (缺少 bilinet 模块)')
  const ckey = 'rcmd:' + (page || 1)
  const cached = cacheGet(ckey, 60000)
  if (cached) return cached
  const url = 'https://api.bilibili.com/x/web-interface/index/top/feed/rcmd?ps=12'
    + '&fresh_idx=' + (page || 1) + '&fresh_idx_1h=' + (page || 1) + '&fresh_type=4&version=1'
  const body = getJson(url, 15)
  if (body.code !== 0 || !body.data) {
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    throw new Error(body.message || ('接口错误 code=' + body.code))
  }
  const list = body.data.item || []
  const videos = []
  for (let i = 0; i < list.length; i++) {
    const v = list[i]
    if (!v) continue
    let pic = v.pic || ''
    if (pic.indexOf('//') === 0) pic = 'https:' + pic
    videos.push({
      bvid: v.bvid || '',
      aid: v.aid || 0,
      // rcmd 标题带 <em class="keyword"> 高亮标记, stripTags 统一去除
      title: stripTags(v.title),
      author: (v.owner && v.owner.name) || '',
      playText: formatPlay(v.stat && v.stat.view),
      duration: typeof v.duration === 'number' ? formatDuration(v.duration) : (v.duration || ''),
      pic: thumb(pic, 400, 250)
    })
  }
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

// ================= 交互操作 (点赞/投币/收藏/三连/稍后再看, 需登录 + csrf) =================

function needCsrf() {
  if (!auth.hasCookie()) throw new Error('登录后才能操作')
  const csrf = auth.getCsrf()
  if (!csrf) throw new Error('Cookie 缺少 bili_jct (请重新登录)')
  return csrf
}

/**
 * 点赞/取消赞 (x/web-interface/archive/like, like=1 赞 / 2 取消)
 * @returns {Promise<{ok:boolean}>}
 */
export async function likeVideo(aid, on) {
  const csrf = needCsrf()
  const data = 'aid=' + encodeURIComponent(aid) + '&like=' + (on === false ? 2 : 1)
    + '&csrf=' + encodeURIComponent(csrf)
  const body = postJson('https://api.bilibili.com/x/web-interface/archive/like', data, 15)
  if (body.code !== 0) {
    if (body.code === -101) throw new Error('登录已过期, 请重新登录')
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    throw new Error(body.message || ('点赞失败 code=' + body.code))
  }
  return true
}

/**
 * 投币 (x/web-interface/coin/add, multiply=1/2, select_like=1 同时点赞)
 * @returns {Promise<{ok:boolean, like:boolean}>}
 */
export async function addCoin(aid, multiply, withLike) {
  const csrf = needCsrf()
  const data = 'aid=' + encodeURIComponent(aid) + '&multiply=' + (multiply === 2 ? 2 : 1)
    + '&select_like=' + (withLike ? 1 : 0) + '&csrf=' + encodeURIComponent(csrf)
  const body = postJson('https://api.bilibili.com/x/web-interface/coin/add', data, 15)
  if (body.code !== 0) {
    if (body.code === -101) throw new Error('登录已过期, 请重新登录')
    if (body.code === 34002) throw new Error('硬币不足')
    if (body.code === 34003) throw new Error('超过投币上限')
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    throw new Error(body.message || ('投币失败 code=' + body.code))
  }
  return { like: body.data && body.data.like === true }
}

/**
 * 收藏夹列表 (x/v3/fav/folder/created/list-all, 需登录)
 * @param {number} rid 稿件 aid (传入时返回该稿件的收藏状态)
 * @returns {Promise<Array<{id,title,mediaCount,favoured}>>}
 */
export async function getFavFolders(rid) {
  if (!auth.hasCookie()) throw new Error('登录后才能查看收藏夹')
  const url = 'https://api.bilibili.com/x/v3/fav/folder/created/list-all?type=2'
    + (rid ? '&rid=' + encodeURIComponent(rid) : '')
  const body = getJson(url, 15)
  if (body.code !== 0 || !body.data) {
    if (body.code === -101) throw new Error('登录已过期, 请重新登录')
    throw new Error(body.message || ('收藏夹接口错误 code=' + body.code))
  }
  const list = body.data.list || []
  const out = []
  for (let i = 0; i < list.length; i++) {
    const f = list[i]
    if (!f) continue
    out.push({
      id: f.id || 0,
      title: f.title || '收藏夹',
      mediaCount: f.media_count || 0,
      favoured: f.favoured === 1
    })
  }
  return out
}

/**
 * 收藏/取消收藏 (x/v3/fav/resource/deal, add/del_media_ids)
 */
export async function dealFav(aid, folderId, on) {
  const csrf = needCsrf()
  const data = 'rid=' + encodeURIComponent(aid) + '&type=2&csrf=' + encodeURIComponent(csrf)
    + '&' + (on === false ? 'del_media_ids=' + encodeURIComponent(folderId)
                          : 'add_media_ids=' + encodeURIComponent(folderId))
  const body = postJson('https://api.bilibili.com/x/v3/fav/resource/deal', data, 15)
  if (body.code !== 0) {
    if (body.code === -101) throw new Error('登录已过期, 请重新登录')
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    throw new Error(body.message || ('收藏失败 code=' + body.code))
  }
  return true
}

/**
 * 添加稍后再看 (x/v2/history/toview/add, 需登录 + csrf)
 */
export async function addToViewLater(aid) {
  const csrf = needCsrf()
  const data = 'aid=' + encodeURIComponent(aid) + '&csrf=' + encodeURIComponent(csrf)
  const body = postJson('https://api.bilibili.com/x/v2/history/toview/add', data, 15)
  if (body.code !== 0) {
    if (body.code === -101) throw new Error('登录已过期, 请重新登录')
    if (body.code === 57001) throw new Error('稍后再看列表已满')
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    throw new Error(body.message || ('添加失败 code=' + body.code))
  }
  return true
}

/**
 * 取消稍后再看 (x/v2/history/toview/del, 需登录 + csrf)
 */
export async function delToViewLater(aid) {
  const csrf = needCsrf()
  const data = 'aid=' + encodeURIComponent(aid) + '&csrf=' + encodeURIComponent(csrf)
  const body = postJson('https://api.bilibili.com/x/v2/history/toview/del', data, 15)
  if (body.code !== 0) {
    if (body.code === -101) throw new Error('登录已过期, 请重新登录')
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    throw new Error(body.message || ('取消失败 code=' + body.code))
  }
  return true
}

/**
 * 查询某视频是否已在稍后再看列表 (只看第一页 20 条, 够用且省请求).
 * 失败返回 false (不阻塞详情页, 按钮退化为「未加入」态).
 */
export async function isInToView(aid) {
  if (!auth.hasCookie()) return false
  try {
    const url = 'https://api.bilibili.com/x/v2/history/toview/web?pn=1&ps=20'
    const body = getJson(url, 10)
    if (body.code !== 0 || !body.data) return false
    const list = body.data.list || []
    for (let i = 0; i < list.length; i++) {
      if (list[i] && Number(list[i].aid) === Number(aid)) return true
    }
  } catch (e) {
    console.log('[bili] toview 状态查询失败: ' + (e && e.message ? e.message : e))
  }
  return false
}

// ================= 我的页面子列表 (历史记录/收藏/稍后再看, 需登录) =================

/**
 * 历史记录 (x/web-interface/history/search, wbi 签名)
 * @returns {Promise<{items:Array, hasMore:boolean}>}
 */
export async function getHistoryList(pn) {
  if (!auth.hasCookie()) throw new Error('未登录')
  const url = 'https://api.bilibili.com/x/web-interface/history/search?'
    + (await wbiQuery({ pn: pn || 1, ps: 20, business: 'all' }))
  const body = getJson(url, 15)
  if (body.code !== 0 || !body.data) {
    if (body.code === -101) throw new Error('登录已过期, 请重新登录')
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    throw new Error(body.message || ('历史接口错误 code=' + body.code))
  }
  const list = body.data.list || []
  const items = []
  for (let i = 0; i < list.length; i++) {
    const h = list[i]
    if (!h) continue
    // history.business: archive=视频 / pugv=课程 / article=专栏, 只展示视频类
    const his = h.history || {}
    if (his.business !== 'archive' || !his.bvid) continue
    items.push({
      bvid: his.bvid,
      aid: his.aid || 0,
      title: stripTags(h.title || h.show_title || ''),
      author: h.author_name || '',
      // progress 秒: 看到第几秒, 0 = 未看; duration 秒
      progressText: h.progress > 0 && h.duration > 0
        ? '看到 ' + formatDuration(h.progress) + ' / ' + formatDuration(h.duration)
        : (h.duration > 0 ? formatDuration(h.duration) : ''),
      pic: thumb(h.cover || '', 400, 250),
      pubText: formatDate(h.view_at) ? '看于 ' + formatDate(h.view_at) : ''
    })
  }
  return { items: items, hasMore: (body.data.has_more === 1) && items.length > 0 }
}

/**
 * 收藏夹内容 (x/v3/fav/resource/list, 需登录)
 * @returns {Promise<{items:Array, hasMore:boolean}>}
 */
export async function getFavList(mediaId, pn) {
  if (!auth.hasCookie()) throw new Error('未登录')
  const url = 'https://api.bilibili.com/x/v3/fav/resource/list?media_id='
    + encodeURIComponent(mediaId) + '&pn=' + (pn || 1) + '&ps=20&keyword=&order=mtime&type=0&tid=0'
  const body = getJson(url, 15)
  if (body.code !== 0 || !body.data) {
    if (body.code === -101) throw new Error('登录已过期, 请重新登录')
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    throw new Error(body.message || ('收藏列表接口错误 code=' + body.code))
  }
  const list = body.data.medias || []
  const items = []
  for (let i = 0; i < list.length; i++) {
    const m = list[i]
    if (!m) continue
    if (m.title === '已失效视频') continue
    items.push({
      bvid: m.bvid || '',
      aid: m.id || 0,
      title: stripTags(m.title || ''),
      author: m.upper ? (m.upper.name || '') : '',
      playText: formatPlay(m.cnt_info && m.cnt_info.play),
      duration: m.duration ? formatDuration(m.duration) : '',
      pic: thumb(m.cover || '', 400, 250)
    })
  }
  // has_more: data.has_more (1=还有) + total 对比兜底
  const total = body.data.info ? (body.data.info.total || 0) : 0
  return { items: items, hasMore: body.data.has_more === 1 && items.length > 0 }
}

/**
 * 稍后再看 (x/v2/history/toview/web, 需登录)
 * @returns {Promise<{items:Array, count:number}>}
 */
export async function getToViewList(pn) {
  if (!auth.hasCookie()) throw new Error('未登录')
  const url = 'https://api.bilibili.com/x/v2/history/toview/web?pn=' + (pn || 1) + '&ps=20'
  const body = getJson(url, 15)
  if (body.code !== 0 || !body.data) {
    if (body.code === -101) throw new Error('登录已过期, 请重新登录')
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    throw new Error(body.message || ('稍后再看接口错误 code=' + body.code))
  }
  const list = body.data.list || []
  const items = []
  for (let i = 0; i < list.length; i++) {
    const t = list[i]
    if (!t) continue
    items.push({
      bvid: t.bvid || '',
      aid: t.aid || 0,
      title: stripTags(t.title || ''),
      author: t.owner ? (t.owner.name || '') : '',
      playText: '',
      duration: t.duration ? formatDuration(t.duration) : '',
      pic: thumb(t.pic || '', 400, 250),
      pubText: formatDate(t.add_at) ? '加于 ' + formatDate(t.add_at) : ''
    })
  }
  return { items: items, count: body.data.count || 0 }
}

// ================= 登录 (二维码 + Cookie) / 我的 / 动态 / 评论 =================

/**
 * 生成登录二维码. 返回 {qrcodeKey, qrUrl}:
 *   qrcodeKey 用于轮询, qrUrl 需编码为二维码供 B 站 App 扫描.
 */
export async function qrcodeGenerate() {
  if (!hasHttp()) throw new Error('当前固件不支持 http 请求 (缺少 bilinet 模块)')
  const body = getJson('https://passport.bilibili.com/x/passport-login/web/qrcode/generate', 10)
  if (body.code !== 0 || !body.data || !body.data.qrcode_key) {
    throw new Error(body.message || ('二维码接口错误 code=' + body.code))
  }
  return { qrcodeKey: body.data.qrcode_key, qrUrl: body.data.url }
}

/**
 * 轮询二维码状态. 返回:
 *   {state: 'waiting'|'scanned'|'expired'|'ok', cookies?: {sessdata,biliJct,dedeUserId}}
 * 成功时 cookie 参数直接在响应体 redirect url 中 (无需解析 Set-Cookie 头).
 */
export async function qrcodePoll(qrcodeKey) {
  const body = getJson('https://passport.bilibili.com/x/passport-login/web/qrcode/poll?qrcode_key='
    + encodeURIComponent(qrcodeKey), 10)
  if (body.code !== 0 || !body.data) {
    throw new Error(body.message || ('轮询接口错误 code=' + body.code))
  }
  const c = body.data.code
  if (c === 0) {
    const url = body.data.url || ''
    const cookies = parseLoginUrlParams(url)
    if (!cookies || !cookies.sessdata) throw new Error('登录成功但未取到 Cookie')
    return { state: 'ok', cookies: cookies }
  }
  if (c === 86090) return { state: 'scanned' }
  if (c === 86038) return { state: 'expired' }
  return { state: 'waiting' }  // 86039 未扫描
}

// 从跨域跳转 url 的 query 里解析登录 Cookie 参数
function parseLoginUrlParams(url) {
  if (!url) return null
  const out = { sessdata: '', biliJct: '', dedeUserId: '' }
  const qs = url.indexOf('?') >= 0 ? url.substring(url.indexOf('?') + 1) : ''
  const pairs = qs.split('&')
  for (let i = 0; i < pairs.length; i++) {
    const eq = pairs[i].indexOf('=')
    if (eq <= 0) continue
    const k = pairs[i].substring(0, eq)
    let v = pairs[i].substring(eq + 1)
    try { v = decodeURIComponent(v) } catch (e) {}
    if (k === 'SESSDATA') out.sessdata = v
    else if (k === 'bili_jct') out.biliJct = v
    else if (k === 'DedeUserID') out.dedeUserId = v
  }
  return out.sessdata ? out : null
}

/**
 * 我的账号信息 (x/web-interface/nav, 带 Cookie; 匿名返回 isLogin=false)
 */
export async function getMyInfo() {
  if (!hasHttp()) throw new Error('当前固件不支持 http 请求 (缺少 bilinet 模块)')
  const body = getJson('https://api.bilibili.com/x/web-interface/nav', 10)
  if (!body || !body.data) throw new Error('nav 接口错误')
  const d = body.data
  let face = d.face || ''
  if (face.indexOf('//') === 0) face = 'https:' + face
  // 设备 image 组件不支持 webp, 借 B 站图片服务转成 jpg.
  // 注意: 必须保留原扩展名再加参数, 写成 xxx@144w.jpg 会 404.
  if (face && face.indexOf('@') < 0) face = face + '@144w_144h_1c.jpg'
  // nav 返回的是 level_info (下划线), 不是 levelInfo —— 之前写错导致等级恒为 0
  const li = d.level_info || d.levelInfo || {}
  const wallet = d.wallet || {}
  return {
    isLogin: d.isLogin === true,
    uname: d.uname || '',
    face: face,
    mid: d.mid || 0,
    level: li.current_level || 0,
    // 实测: nav 的 data.money 是「硬币」(如 1652.1), wallet.bcoin_balance 才是「B币」
    coin: typeof d.money === 'number' ? Math.floor(d.money) : 0,
    money: typeof wallet.bcoin_balance === 'number' ? wallet.bcoin_balance : 0,
    vip: (d.vip && d.vip.status === 1) ? '大会员' : ''
  }
}

/**
 * 动态视频流 (x/polymer/web-dynamic/v1/feed/all, 需登录 Cookie)
 * @param {string} offset 分页游标 (首次传 '')
 * @returns {Promise<{items:Array, offset:string, hasMore:boolean}>}
 */
export async function getDynamicFeed(offset) {
  if (!hasHttp()) throw new Error('当前固件不支持 http 请求 (缺少 bilinet 模块)')
  if (!auth.hasCookie()) throw new Error('未登录')
  const url = 'https://api.bilibili.com/x/polymer/web-dynamic/v1/feed/all?timezone_offset=-480&type=video'
    + (offset ? '&offset=' + encodeURIComponent(offset) : '')
  const body = getJson(url, 15)
  if (body.code === -101) throw new Error('未登录或登录已过期')
  if (body.code !== 0 || !body.data) {
    throw new Error(body.message || ('动态接口错误 code=' + body.code))
  }
  const list = body.data.items || []
  const items = []
  for (let i = 0; i < list.length; i++) {
    const it = list[i]
    if (!it) continue
    const md = (it.modules && it.modules.module_dynamic) || {}
    const ma = (it.modules && it.modules.module_author) || {}
    const major = md.major || {}
    // 视频动态: major.archive (封面字段叫 cover, 没有 pic —— 搜索/热门接口才是 pic)
    if (it.type === 'DYNAMIC_TYPE_AV') {
      const arc = major.archive || {}
      if (!arc.bvid) continue
      let pic = arc.cover || arc.pic || ''
      if (pic.indexOf('//') === 0) pic = 'https:' + pic
      const st = arc.stat || {}
      items.push({
        type: 'video',
        bvid: arc.bvid,
        aid: arc.aid || 0,
        title: stripTags(arc.title),
        author: ma.name || '',
        playText: formatPlay(st.play),
        duration: arc.duration_text || '',
        pic: thumb(pic, 400, 250),
        pubText: ma.pub_time || ''
      })
      continue
    }
    // 图文动态: major.draw.items[] (src 是 i0.hdslb.com 图床), title 用动态文案
    if (it.type === 'DYNAMIC_TYPE_DRAW') {
      const draw = major.draw || {}
      const pics = draw.items || []
      const first = pics.length > 0 ? (pics[0].src || '') : ''
      let pic = first
      if (pic.indexOf('//') === 0) pic = 'https:' + pic
      const txt = stripTags(md.desc && md.desc.text ? md.desc.text : '')
      items.push({
        type: 'draw',
        bvid: '',
        aid: 0,
        title: txt !== '' ? txt : '图文动态',
        author: ma.name || '',
        playText: '',
        duration: (pics.length > 1 ? pics.length + '图' : '图文'),
        pic: pic !== '' ? thumb(pic, 400, 400) : '',
        pubText: ma.pub_time || ''
      })
      continue
    }
    // 其他动态类型 (转发/文字等) 不展示
  }
  // 封面诊断: 0.8.6 动态封面不显示过一次, 留下实际下发的 URL 便于设备上 curl 验证
  if (items.length > 0) log('动态', '首条封面 ' + items[0].pic)
  return {
    items: items,
    offset: body.data.offset || '',
    hasMore: body.data.has_more === 1
  }
}

/**
 * 视频评论列表 (x/v2/reply, 匿名可读; type=1 视频评论区)
 * @param {number} aid 视频 aid
 * @param {number} pn 页码 (从 1 开始)
 * @param {object} builtinEmoji 内置 emoji 图片映射 (由页面层传入)
 * @param {string} sort 'hot'=热度(sort=2) / 'time'=最新(sort=0)
 * @returns {Promise<{total:number, replies:Array}>}
 */
export async function getReplies(aid, pn, builtinEmoji, sort) {
  if (!hasHttp()) throw new Error('当前固件不支持 http 请求 (缺少 bilinet 模块)')
  const sortParam = sort === 'time' ? 0 : 2
  const url = 'https://api.bilibili.com/x/v2/reply?type=1&oid=' + encodeURIComponent(aid)
    + '&pn=' + (pn || 1) + '&ps=20&sort=' + sortParam + '&jsonp=json'
  const body = getJson(url, 15)
  if (body.code !== 0 || !body.data) {
    if (body.code === 12009) throw new Error('评论区已关闭')
    throw new Error(body.message || ('评论接口错误 code=' + body.code))
  }
  const page = body.data.page || {}
  const list = body.data.replies || []
  const replies = []
  for (let i = 0; i < list.length; i++) {
    const r = list[i]
    if (!r) continue
    const member = r.member || {}
    const content = r.content || {}
    let face = member.avatar || ''
    if (face.indexOf('//') === 0) face = 'https:' + face
    const upperMid = body.data.upper ? body.data.upper.mid : 0
    replies.push({
      rpid: r.rpid || 0,
      author: (member.uname || '用户') + (r.mid === upperMid ? ' (UP)' : ''),
      face: thumb(face, 60, 60),
      // segs: 图文混排段 (B 站 emote + unicode emoji -> 图片); message 保留纯文本兜底
      message: stripTags(content.message),
      segs: parseMessage(content.message, content.emote, builtinEmoji),
      likeText: formatPlay(r.like),
      timeText: formatRelative(r.ctime),
      replyCount: r.rcount || 0
    })
  }
  return { total: page.count || 0, replies: replies }
}

/**
 * 楼中楼 (某条主评论的子回复列表, x/v2/reply/reply)
 * @param {number} aid 视频 aid (oid)
 * @param {number} root 顶层主评论 rpid
 * @param {number} pn 页码
 * @param {object} builtinEmoji 内置 emoji 图片映射
 */
export async function getSubReplies(aid, root, pn, builtinEmoji) {
  if (!hasHttp()) throw new Error('当前固件不支持 http 请求 (缺少 bilinet 模块)')
  const url = 'https://api.bilibili.com/x/v2/reply/reply?type=1&oid=' + encodeURIComponent(aid)
    + '&root=' + encodeURIComponent(root) + '&pn=' + (pn || 1) + '&ps=20&jsonp=json'
  const body = getJson(url, 15)
  if (body.code !== 0 || !body.data) {
    if (body.code === 12009) throw new Error('评论区已关闭')
    throw new Error(body.message || ('回复接口错误 code=' + body.code))
  }
  const page = body.data.page || {}
  const list = body.data.replies || []
  const replies = []
  for (let i = 0; i < list.length; i++) {
    const r = list[i]
    if (!r) continue
    const member = r.member || {}
    const content = r.content || {}
    let face = member.avatar || ''
    if (face.indexOf('//') === 0) face = 'https:' + face
    // reply_to: 楼中楼里被回复的人 (扁平结构, 回复某人时标注)
    const rt = r.reply_to ? (r.reply_to.uname || '') : ''
    replies.push({
      rpid: r.rpid || 0,
      author: member.uname || '用户',
      replyTo: rt,
      face: thumb(face, 60, 60),
      message: stripTags(content.message),
      segs: parseMessage(content.message, content.emote, builtinEmoji),
      likeText: formatPlay(r.like),
      timeText: formatRelative(r.ctime)
    })
  }
  return { total: page.count || 0, replies: replies }
}

/**
 * 发表评论 (x/v2/reply/add, 需登录 Cookie + csrf)
 * @param {number} aid 视频 aid (oid)
 * @param {string} message 内容
 * @param {number} [root] 楼中楼: 顶层主评论 rpid (发主评论时不传)
 * @param {number} [parent] 楼中楼: 被直接回复的那条 rpid (默认等于 root)
 */
export async function addReply(aid, message, root, parent) {
  if (!hasHttp()) throw new Error('当前固件不支持 http 请求 (缺少 bilinet 模块)')
  if (!auth.hasCookie()) throw new Error('登录后才能评论')
  const csrf = auth.getCsrf()
  if (!csrf) throw new Error('Cookie 缺少 bili_jct (请重新登录)')
  let data = 'oid=' + encodeURIComponent(aid) + '&type=1&message='
    + encodeURIComponent(message) + '&csrf=' + encodeURIComponent(csrf)
  if (root) {
    data += '&root=' + encodeURIComponent(root)
    data += '&parent=' + encodeURIComponent(parent || root)
  }
  const body = postJson('https://api.bilibili.com/x/v2/reply/add', data, 15)
  if (body.code !== 0) {
    if (body.code === -101) throw new Error('登录已过期, 请重新登录')
    if (body.code === -412) throw new Error('请求被风控拦截, 请稍后再试')
    throw new Error(body.message || ('评论发送失败 code=' + body.code))
  }
  return true
}

/**
 * 从电脑端 pc-cookie-server.py 拉取 Cookie (电脑同步登录)
 * @param {string} ip 电脑局域网 IP, 如 "192.168.1.100"
 * @returns {Promise<{sessdata,biliJct,dedeUserId}>}
 */
export async function fetchPcCookie(ip) {
  if (!hasHttp()) throw new Error('当前固件不支持 http 请求 (缺少 bilinet 模块)')
  const clean = String(ip || '').trim()
  if (!/^\d{1,3}(\.\d{1,3}){3}$/.test(clean)) throw new Error('IP 格式不正确')
  const url = 'http://' + clean + ':9527/bilibilipan/cookie'
  const s = bilinet.httpGet(url, 5)
  console.log('[bili] PC cookie fetch ' + clean + ' -> ' + (s ? s.length : 0) + 'B')
  if (!s) throw new Error('连不上电脑 (' + clean + '), 请确认已运行 pc-cookie-server.py 且同一 WiFi')
  let body
  try {
    body = JSON.parse(s)
  } catch (e) {
    throw new Error('电脑返回的数据不是 JSON, 请确认端口为 9527')
  }
  if (!body.ok || !body.sessdata) throw new Error('电脑端还没有保存 Cookie')
  return {
    sessdata: body.sessdata,
    biliJct: body.bili_jct || '',
    dedeUserId: body.dedeuserid || ''
  }
}

// 相对时间 (评论发布时间): N分钟前/N小时前/N天前/日期
function formatRelative(epochSec) {
  if (!epochSec) return ''
  const dt = new Date(epochSec * 1000)
  function pad(n) { return n < 10 ? '0' + n : '' + n }
  const diff = Date.now() / 1000 - epochSec
  if (diff < 60) return '刚刚'
  if (diff < 3600) return Math.floor(diff / 60) + '分钟前'
  if (diff < 86400) return Math.floor(diff / 3600) + '小时前'
  if (diff < 86400 * 30) return Math.floor(diff / 86400) + '天前'
  return dt.getFullYear() + '-' + pad(dt.getMonth() + 1) + '-' + pad(dt.getDate())
}
