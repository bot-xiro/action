// App 生命周期：读 profile 后用已验证逻辑宽度 setViewPort，注册统一 BasePage。
import { BasePage } from './base-page.js';

// 从 profiles/device-215.yaml 读取：有道词典笔 P5/Melon Pro（RK3562）
// screen.width=960（Falcon 设计宽度），direction=270（竖屏旋转）
const DESIGN_WIDTH = 960;

class App extends $falcon.App {
  constructor() {
    super();
  }

  onLaunch(options) {
    super.onLaunch(options);
    this.setViewPort(DESIGN_WIDTH);
    $falcon.useDefaultBasePageClass(BasePage);
  }

  onShow() {
    super.onShow();
  }

  onHide() {
    super.onHide();
  }

  onDestroy() {
    super.onDestroy();
  }
}

export default App;