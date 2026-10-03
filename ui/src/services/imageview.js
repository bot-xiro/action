// 图片查看器共用的「取图 + 取景数学」(真正的查看器在独立 native 模块 imageviewer)
//
// 为什么要单独一层:
//   1) turbojpeg 只解 JPEG —— B 站图床统一走 \`@2040w.jpg\`, 既拿到大图又保证是 JPEG
//      (原图可能是 png/webp, 直接丢给 turbojpeg 会 header 解析失败);
//   2) 列表里的图都是缩略(\`@160w_160h_1c\`), 查看器必须去掉缩略后缀拿原图, 否则放大就是糊的;
//   3) fit / zoom / 平移夹取 三个数学放一处, 评论页与动态页共用, 避免两处各写一份又各错一份.

export const VIEW_W = 960
export const VIEW_H = 266
export const MAX_ZOOM = 8

// ---- 对外语义: scale(倍率) 100% = 整图适配屏幕(= 用户口中的"全屏") ----
// 内部 zoom = fitZoom * scale 才交给 native.
export const MIN_SCALE = 0.25
export const MAX_SCALE = 16

export function clampScale(s) {
  let v = s
  if (!(v > 0)) v = 1
  if (v < MIN_SCALE) v = MIN_SCALE
  if (v > MAX_SCALE) v = MAX_SCALE
  return v
}

// 缩略 URL -> 原图(大图) URL
export function bigUrl(u) {
  let s = String(u == null ? '' : u)
  if (s === '') return s
  const q = s.indexOf('?')
  if (q >= 0) s = s.slice(0, q)
  const at = s.indexOf('@')
  if (at >= 0) s = s.slice(0, at)
  if (s.indexOf('hdslb.com') >= 0 || s.indexOf('bilivideo') >= 0 || s.indexOf('biliimg') >= 0) {
    return s + '@2040w.jpg'
  }
  return s
}

// 整图适配的 zoom (输出像素 / 原图像素)
export function fitZoom(w, h, outW, outH) {
  const W = outW || VIEW_W
  const H = outH || VIEW_H
  if (!w || !h) return 1
  const z = Math.min(W / w, H / h)
  return z > 0 ? z : 1
}

// zoom 夹取: 下限 = 整图适配的一半(再小没意义), 上限 = MAX_ZOOM
export function clampZoom(z, w, h) {
  const fit = fitZoom(w, h)
  let lo = fit * 0.5
  if (lo < 0.02) lo = 0.02
  let v = z
  if (!(v > 0)) v = fit
  if (v < lo) v = lo
  if (v > MAX_ZOOM) v = MAX_ZOOM
  return v
}

// 视口中心夹取: 图比视口大才允许平移, 否则强制居中(不会拖出黑边)
export function clampCenter(c, view, total) {
  if (!total) return 0
  if (view >= total) return total / 2
  let v = c
  if (v < view / 2) v = view / 2
  if (v > total - view / 2) v = total - view / 2
  return v
}
