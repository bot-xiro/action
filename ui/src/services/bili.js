// 哔哩哔哩网络服务
// 网络层: $falcon.jsapi.http.request (参考 miniapp falcon.d.ts)
// 归一化: statusCode / data(string|object) -> 统一结果或 Error

async function httpRequest(params) {
  const jsapi = $falcon.jsapi
  if (jsapi.http && jsapi.http.request) {
    return await jsapi.http.request(params)
  }
  if (jsapi.net && jsapi.net.request) {
    return await jsapi.net.request(params)
  }
  throw new Error('当前固件不支持 http/net 请求')
}

function parseBody(data) {
  if (data && typeof data === 'object') return data
  if (typeof data === 'string') {
    try { return JSON.parse(data) } catch (e) {
      throw new Error('服务器返回格式错误')
    }
  }
  throw new Error('空响应')
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
  const kw = encodeURIComponent(keyword)
  const url = 'https://api.bilibili.com/x/web-interface/search/type'
    + '?search_type=video&keyword=' + kw
    + '&page=' + (page || 1) + '&pagesize=20'

  let res
  try {
    res = await httpRequest({
      url: url,
      method: 'GET',
      headers: {
        'User-Agent': UA,
        'Referer': REFERER
      },
      timeout: 15000
    })
  } catch (e) {
    throw new Error('网络请求失败: ' + (e && e.message ? e.message : e))
  }

  const status = res && (res.statusCode || res.status)
  if (status !== 200) {
    throw new Error('HTTP ' + (status || '错误'))
  }

  const body = parseBody(res.data != null ? res.data : res.body)
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
