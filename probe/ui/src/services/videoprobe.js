// 对外给页面调引导, 内部直接调用 native module 'videoprobe'
// sync 接口 (短操作), 线程安全由 native 内部守护

export const GST_URI = 'file:///tmp/bunny.mp4'

// 逻辑坐标变换 -> kmssink 物理矩形
// 见 skill media-kms.md 与 profile youdao-rk3562-x7
export function toKmsRect(lx, ly, lw, lh) {
  // 已实测 image 方向下 l=(0,44,960,178) -> phys(44+107, 959-(0+960), 178, 960) = (151, -1, 178, 960)
  // 面板宽 480 即 x [0,480), x=(151,151+178=329<480 合法), y=959-960=-1 需 clamp 0 并降 h
  var px = ly + 107
  var py = 959 - (lx + lw)
  if (py < 0) py = 0
  var pw = lh
  var ph = lw
  if (px + pw > 480) pw = 480 - px
  if (py + ph > 960) ph = 960 - py
  return px + ',' + py + ',' + pw + ',' + ph
}

export const RECT_BAND  = '0,44,960,178'
export const RECT_FULL  = '0,0,960,266'

export function pickRectStr(mode) {
  return mode === 'full' ? RECT_FULL : RECT_BAND
}
export const RectMode = { BAND: 'band', FULL: 'full' }
