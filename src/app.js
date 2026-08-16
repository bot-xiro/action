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
    // 将 gstPlayer.preheat() 延后到 onLaunch 生命周期钩子执行，
    // 确保 JQuick ES module 初始化完成后再调用原生 JS 对象方法。
    try {
      var r = gstPlayer && gstPlayer.preheat
        ? gstPlayer.preheat()
        : undefined;
      console.warn('[app] preheat in onLaunch ret=' + r)
    } catch (err) {
      console.warn('[app] gstPlayer.preheat error in onLaunch: ' + (err && err.message))
    }
    // 初始化 bili-auth 原生模块（注册 native bili-auth 模块，供后续 import 'biliauth' 使用）
    try {
      var r2 = biliAuth && biliAuth.init
        ? biliAuth.init()
        : undefined;
      console.warn('[app] biliAuth.init in onLaunch ret=' + r2)
    } catch (err) {
      console.warn('[app] biliAuth.init error in onLaunch: ' + (err && err.message))
    }
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

// 预热 gstplayer 原生模块：延后到 App.onLaunch 执行（避免 JQuick ES module 初始化期间调用被禁用）
import { gstPlayer } from 'gstplayer'
// 强制引用 bili-auth，使 aiot-cli 静态分析将其纳入 manifest.json 打包进 .amr
import { biliAuth } from 'biliauth'

export default App
