// videoprobe app 入口
import { BasePage } from './base-page.js'

// 预热 native 模块 (扫描一次 GLib plugins; native 侧 setenv WAYLAND_DISPLAY 在 daemon 里做)
import * as vpmod from 'videoprobe'
try {
  console.log('[probeapp] videoprobe module=' + !!vpmod.videoprobe)
  if (vpmod && vpmod.videoprobe) {
    // 日志上行到 falcon console
    console.log('[probeapp] version=' + (vpmod.videoprobe.version || '?'))
  }
} catch (e) {
  console.warn('[probeapp] videoprobe import fail: ' + (e && e.message ? e.message : e))
}

class App extends $falcon.App {
  constructor() { super() }
  onLaunch(options) {
    super.onLaunch(options)
    $falcon.useDefaultBasePageClass(BasePage)
  }
}
export default App
