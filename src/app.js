class App extends $falcon.App {
  /**
   * 构造函数,应用生命周期内只构造一次
   */
  constructor() {
    super()
  }

  /**
   * 应用生命周期:应用启动. 初始化完成时回调,全局只触发一次.
   * @param {Object} options 启动参数
   */
  onLaunch(options) {
    super.onLaunch(options)
  }

  /**
   * 应用生命周期,应用启动或应用从后台切换到前台时触发
   */
  onShow() {
    super.onShow()
  }

  /**
   * 应用生命周期:应用退出前或者应用从前台切换到后台时触发
   */
  onHide() {
    super.onHide()
  }

  /**
   * 应用生命周期:应用销毁前触发
   */
  onDestroy() {
    super.onDestroy()
  }
}

try {
  globalThis['window'] = {
    requestAnimationFrame,
    cancelAnimationFrame
  }
} catch (err) {
  console.log(err)
}

// 预热 gstplayer 原生模块：app 启动即完成 gst_init（插件扫描 100~300ms），
// 从播放路径上移除，缩短首次播放首帧延迟（gstplayer_init 内 ensureGstInit）
import { gstPlayer } from 'gstplayer'

// 【探测 2026-08-11】系统播放器（8001661999525016）用框架原生 CVPlayer 播放
// （视频在 UI 层内合成，控制栏共存）。探测我们的 app 能否拿到 CVPlayer：
try {
  console.warn('[probe] typeof getCVPlayerManager=' + typeof getCVPlayerManager)
  console.warn('[probe] typeof CVPlayer=' + typeof CVPlayer)
  console.warn('[probe] typeof getVideoManager=' + typeof getVideoManager)
  const gk = Object.keys(globalThis || {}).filter(k => /player|video|cv|proxy|manager|media/i.test(k))
  console.warn('[probe] global matches: ' + JSON.stringify(gk))
  console.warn('[probe] $falcon keys: ' + JSON.stringify(Object.keys($falcon)))
  if ($falcon.jsapi) console.warn('[probe] jsapi keys: ' + JSON.stringify(Object.keys($falcon.jsapi)))
  // 内部映射探测（系统播放器 videoproxy/CVPlayer 藏身处）
  if ($falcon._modules) console.warn('[probe] _modules keys: ' + JSON.stringify(Object.keys($falcon._modules)))
  if ($falcon._serviceMap) console.warn('[probe] _serviceMap keys: ' + JSON.stringify(Object.keys($falcon._serviceMap)))
  if ($falcon.__JSAPI) console.warn('[probe] __JSAPI keys: ' + JSON.stringify(Object.keys($falcon.__JSAPI)))
  if ($falcon.__NAVIGATOR) console.warn('[probe] __NAVIGATOR keys: ' + JSON.stringify(Object.keys($falcon.__NAVIGATOR)))
  if ($falcon.env) console.warn('[probe] env: ' + JSON.stringify(Object.keys($falcon.env)))
  // 尝试 require 框架模块（videoproxy/cvplayer/pm，系统播放器 index.js 的依赖）
  try { console.warn('[probe] require videoproxy: ' + typeof require('videoproxy')) } catch (e) { console.warn('[probe] require videoproxy err: ' + (e && e.message)) }
  try { console.warn('[probe] require cvplayer: ' + typeof require('cvplayer')) } catch (e) { console.warn('[probe] require cvplayer err: ' + (e && e.message)) }
  try { console.warn('[probe] require pm: ' + typeof require('pm')) } catch (e) { console.warn('[probe] require pm err: ' + (e && e.message)) }
} catch (err) {
  console.warn('[probe] err: ' + (err && err.message))
}

// 闪烁修复（2026-08-09）：UI 主平面 zpos 层级提升全局只执行一次，且必须在
// 播放路径之外。之前每次 gstPlayer.open 都要 setPlanezPos(54,3)，播放瞬间
// 反复改写 UI 平面层级 → 合成器短暂混乱、画面闪烁。改为应用启动时预热。
// preheat 内部幂等（原子标志保证只执行一次），此处调用即使失败也不影响播放。
try {
  gstPlayer.preheat()
} catch (err) {
  console.warn('[app] gstPlayer.preheat error: ' + (err && err.message))
}

export default App
