// 防息屏 JSAPI 适配 (系统 brightness/global 模块, 官方三件套)
//
// 背景 (panapp/系统播放器防息屏机制分析.md, 同型号真机只读探测):
//   - 设备内置视频播放器用 JSAPI 三件套防息屏: startAlwaysScreenOn /
//     stopAlwaysScreenOn / keepScreenOn, 同时挂在 global 与 brightness 两个
//     JSAPI 代理上, 底层 YDALAPI::YBrightnessModule (libjsapi_export.so 符号实证)
//   - Weston --idle-time=0: 合成器不做空闲息屏; 息屏由系统按输入事件空闲计时,
//     官方防法就是这三个 JSAPI (内置视频播放器播放路径调用, 退出路径 stop)
//   - 模块名 brightness (videoplayer index.js 模块表实证); 方法参数/返回值未验证
//     → 全部 try/catch, 探测失败只降级 (player 走 exec 注入 touch move 兜底)
//
// 导入: 命名空间导入 (不会因导出名不符而链接失败); ES 模块未注册时整个模块图
// 失败, 故本文件只被 player.vue 引用 (失败只影响播放页, 不影响首页其它页面).
import * as brightnessMod from 'brightness'

let resolved = undefined   // undefined=未探测, null=不可用, 对象=可用

function hasApiMethods(obj) {
  return !!obj && (typeof obj.startAlwaysScreenOn === 'function' ||
    typeof obj.keepScreenOn === 'function' ||
    typeof obj.stopAlwaysScreenOn === 'function')
}

// 模块形态逐一探测: { brightness } / { Brightness } 构造器 / getBrightnessManager() /
// 默认导出 / 模块自身代理
function resolveFrom(mod) {
  if (!mod) return null
  const cands = [mod.brightness, mod.Brightness, mod.default, mod]
  if (typeof mod.getBrightnessManager === 'function') {
    try { cands.push(mod.getBrightnessManager()) } catch (e) {}
  }
  for (let i = 0; i < cands.length; i++) {
    const c = cands[i]
    if (hasApiMethods(c)) return c
    if (typeof c === 'function') {
      // 构造器形态 (mpp 同款: import { mpp } from 'mpp'; new mpp())
      try {
        const inst = new c()
        if (hasApiMethods(inst)) return inst
      } catch (e) {}
    }
  }
  return null
}

function probe() {
  if (resolved !== undefined) return resolved
  resolved = resolveFrom(brightnessMod)
  // $falcon.jsapi 代理形态兜底 (storage 同款挂载)
  if (!resolved) {
    try {
      if ($falcon && $falcon.jsapi) {
        resolved = resolveFrom($falcon.jsapi.brightness) || resolveFrom($falcon.jsapi.global)
      }
    } catch (e) {}
  }
  console.log('[screenon] 防息屏 JSAPI ' + (resolved
    ? '可用 (' + (resolved.startAlwaysScreenOn ? 'startAlwaysScreenOn' : 'keepScreenOn') + ')'
    : '不可用, 播放页走 exec 注入兜底'))
  return resolved
}

/** 播放开始: 开启常亮 (startAlwaysScreenOn + keepScreenOn, 全 try/catch) */
export function screenOnStart() {
  const api = probe()
  if (!api) return false
  try {
    if (typeof api.startAlwaysScreenOn === 'function') api.startAlwaysScreenOn()
  } catch (e) {
    console.log('[screenon] startAlwaysScreenOn 失败: ' + (e && e.message ? e.message : e))
  }
  try {
    if (typeof api.keepScreenOn === 'function') api.keepScreenOn()
  } catch (e) {}
  return true
}

/** 播放中周期保活 (keepScreenOn 一次性语义兜底, 若是持久常亮则冗余无害) */
export function screenOnTick() {
  const api = probe()
  if (!api) return false
  try {
    if (typeof api.keepScreenOn === 'function') api.keepScreenOn()
  } catch (e) {
    return false
  }
  return true
}

/** 页面退出: 关闭常亮 (系统播放器同款, 退出路径调用 stopAlwaysScreenOn) */
export function screenOnStop() {
  const api = probe()
  if (!api) return
  try {
    if (typeof api.stopAlwaysScreenOn === 'function') api.stopAlwaysScreenOn()
  } catch (e) {}
}

/** 是否具备 JSAPI 防息屏能力 */
export function screenOnAvailable() {
  return !!probe()
}
