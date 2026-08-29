// 应用入口
// 参考 references/miniapp 与 skill falcon-runtime.md:
// - 不调用 setViewPort, 使用固件 cfg.json 中的屏幕几何 (960x266, direction 270)
// - 统一注册 BasePage 管理事件/timer 资源
import { BasePage } from './base-page.js'

// 预热并注册 gstplayer 原生模块 (bili.js 的 httpGet 依赖它; 模仿 home 项目 app.js)
import { gstPlayer } from 'gstplayer'
try {
  console.log('[app] gstplayer.httpGet=' + (typeof gstPlayer.httpGet))
} catch (e) {
  console.warn('[app] gstplayer import check failed: ' + (e && e.message ? e.message : e))
}

class App extends $falcon.App {
  constructor() {
    super()
  }

  /**
   * 应用生命周期:应用启动. 初始化完成时回调,全局只触发一次.
   * @param {Object} options 启动参数
   */
  onLaunch(options) {
    super.onLaunch(options)
    // 设置页面基类,应用全局的$falcon.Page将被替换成此处指定的BasePage.
    $falcon.useDefaultBasePageClass(BasePage)
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

export default App
