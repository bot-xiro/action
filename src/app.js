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
