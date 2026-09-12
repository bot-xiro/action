<template>
  <div class="page" :class="entering ? 'page-enter' : ''">
    <!-- 左区: 顶栏 + 模式切换 + 状态.
         电脑同步模式下左区扩展到全宽 960px (该模式无二维码, 不留空白) -->
    <div :class="['left', mode === 'pc' ? 'left-full' : '']">
      <div :class="['topbar', mode === 'pc' ? 'topbar-full' : '']">
        <div class="back" @click="goBack">
          <text class="back-text">‹ 返回</text>
        </div>
        <text class="title">登录哔哩哔哩</text>
      </div>
      <div class="modes">
        <div :class="['mode-tab', mode === 'qr' ? 'mode-active' : '']" @click="switchMode('qr')">
          <text :class="['mode-text', mode === 'qr' ? 'mode-text-active' : '']">扫码登录</text>
        </div>
        <div :class="['mode-tab', mode === 'pc' ? 'mode-active' : '']" @click="switchMode('pc')">
          <text :class="['mode-text', mode === 'pc' ? 'mode-text-active' : '']">电脑同步</text>
        </div>
      </div>

      <!-- 扫码状态 -->
      <div v-if="mode === 'qr'" class="qr-status">
        <text v-if="pollState === 'ok'" class="st st-ok">✓ 登录成功</text>
        <text v-else-if="pollState === 'scanned'" class="st st-ok">已扫描, 请在手机上确认</text>
        <text v-else-if="pollState === 'expired'" class="st st-err">二维码已过期</text>
        <text v-else-if="pollState === 'error'" class="st st-err">{{ pollError }}</text>
        <text v-else class="st">用手机 B 站 App 扫右侧二维码</text>
        <text class="st2">App → 扫一扫 → 确认登录 · 码 3 分钟内有效</text>
        <text v-if="pollState === 'waiting'" class="st2">等待扫描中…</text>
        <div v-if="pollState === 'expired' || pollState === 'error'" class="btn" @click="startQr">
          <text class="btn-text">刷新二维码</text>
        </div>
        <div v-if="pollState === 'ok'" class="btn" @click="goBack">
          <text class="btn-text">完成, 返回</text>
        </div>
      </div>

      <!-- 电脑同步: 左区全宽铺满, 无二维码、不留空白 -->
      <div v-else class="pc-wrap">
        <scroller class="pc-scroll" scroll-direction="vertical" :show-scrollbar="true">
          <text class="pc-tip">① 在电脑上安装 Python 3 (Windows/Mac/Linux 均可), 无需第三方包, 全部使用 Python 标准库。</text>
          <text class="pc-tip">② 电脑与词典笔连接到同一个 WiFi 局域网 (注意: 公共 WiFi 或访客网络可能隔离设备, 建议用家用路由器)。</text>
          <text class="pc-tip">③ 在电脑上运行本仓库 tools/pc-cookie-server.py, 启动后命令行会打印本机局域网 IP, 例如 http://192.168.1.100:9527。</text>
          <text class="pc-tip">④ 电脑浏览器打开 http://127.0.0.1:9527, 在文本框粘贴 B 站 Cookie (要求包含 SESSDATA), 点「保存」。</text>
          <text class="pc-tip">获取 Cookie 的方法: 电脑浏览器登录 bilibili.com → F12 打开 DevTools → Network 标签 → 任一 api.bilibili.com 请求 → 找到 Request Headers → 复制整行 Cookie 值。</text>
          <text class="pc-tip">⑤ 返回本页面, 在下面输入电脑的局域网 IP (如 192.168.1.100), 点「获取并登录」即可把 Cookie 同步到词典笔。</text>
          <text class="pc-tip">安全提示: Cookie 相当于登录凭证, 保存后请尽快关闭电脑上的服务 (Ctrl+C)。笔端获取成功后可立即关闭服务。</text>
          <div class="pc-input" @click="inputIp">
            <text class="pc-input-text">{{ pcIp ? pcIp : '点击输入电脑 IP (如 192.168.1.100)' }}</text>
          </div>
          <div class="btn-row">
            <div class="btn" @click="fetchFromPc">
              <text class="btn-text">{{ fetching ? '获取中…' : '获取并登录' }}</text>
            </div>
          </div>
          <text v-if="pcStatus !== ''" :class="['pc-status', pcOk ? 'st-ok' : 'st-err']">{{ pcStatus }}</text>
        </scroller>
      </div>
    </div>

    <!-- 右区: 二维码 (本地编码器渲染, 不依赖外部图片服务).
         仅扫码登录模式渲染 —— 电脑同步模式无二维码且不留空白. -->
    <div v-if="mode === 'qr'" class="qr-zone">
      <div v-if="qr.size > 0" class="qr-box"
           :style="{ width: (qr.size * MOD + QPAD * 2) + 'px', height: (qr.size * MOD + QPAD * 2) + 'px', left: ((280 - (qr.size * MOD + QPAD * 2)) / 2) + 'px' }">
        <div v-for="(row, r) in qrRows" :key="r" class="qr-row"
             :style="{ top: (r * MOD + QPAD) + 'px', width: (qr.size * MOD + QPAD * 2) + 'px', height: MOD + 'px' }">
          <div v-for="(seg, s) in row" :key="s"
               class="qr-dark"
               :style="{ left: (seg.x * MOD + QPAD) + 'px', width: (seg.w * MOD) + 'px', height: MOD + 'px' }"></div>
        </div>
        <!-- 覆盖层: 过期/出错时可直接点按刷新 -->
        <div v-if="pollState === 'expired' || pollState === 'error'"
             class="qr-mask qr-mask-tap"
             :style="{ width: (qr.size * MOD + QPAD * 2) + 'px', height: (qr.size * MOD + QPAD * 2) + 'px' }"
             @click="startQr">
          <text class="qr-mask-text">{{ pollState === 'expired' ? '已过期' : '出错了' }}</text>
          <text class="qr-mask-sub">点此刷新</text>
        </div>
        <div v-else-if="pollState === 'ok'" class="qr-mask"
             :style="{ width: (qr.size * MOD + QPAD * 2) + 'px', height: (qr.size * MOD + QPAD * 2) + 'px' }">
          <text class="qr-mask-text">✓</text>
        </div>
      </div>
      <text v-else class="qr-ph">{{ qrError !== '' ? '生成失败' : '生成中…' }}</text>
    </div>
  </div>
</template>

<script>
// 登录页: ① 扫码登录 (二维码本地生成 + 2s 轮询; 成功后 Cookie 在响应体
// redirect url 中) ② 电脑同步 (电脑运行 tools/pc-cookie-server.py,
// 浏览器粘贴 Cookie, 笔端按 IP 拉取, 免在笔上输入长文本).
import { createIME } from '../../services/ime.js'
import { qrcodeGenerate, qrcodePoll, getMyInfo, fetchPcCookie } from '../../services/bili.js'
import { saveLogin } from '../../services/auth.js'
import { afterPaint } from '../../base-page.js'
import { makeQR } from '../../services/qrcode.js'

const MOD = 5          // 二维码模块边长 px (本地编码渲染)
const QPAD = 20        // 静默区 >= 4 模块 (ISO/IEC 18004 下限; 面板高 266 受限)
const POLL_MS = 2000   // 轮询周期
const QR_TTL_MS = 180000

export default {
  name: 'login',
  data() {
    return {
      MOD: MOD,
      QPAD: QPAD,
      mode: 'qr',
      entering: true,
      qr: { size: 0, rows: [] },
      qrRows: [],
      qrError: '',
      qrUrl: '',           // 待编码的登录 url (轮询 key 与其绑定)
      qrcodeKey: '',
      qrGeneratedAt: 0,
      pollState: 'generating',  // generating/waiting/scanned/expired/ok/error
      pollError: '',
      pollTimer: null,
      pcIp: '',
      pcStatus: '',
      pcOk: false,
      fetching: false,
      ime: null
    }
  },
  methods: {
    onShow() {
      if (this.entering) {
        const self = this
        if (this.$page && this.$page.setTimeout) {
          this.$page.setTimeout(function () { self.entering = false }, 60)
        } else {
          setTimeout(function () { self.entering = false }, 60)
        }
      }
      // 仅扫码登录模式生成二维码 (电脑同步模式无二维码区)
      if (this.mode === 'qr' && this.pollState === 'generating' && !this.pollTimer) {
        this.startQr()
      }
    },

    onHide() {
      this.stopPoll()
    },

    onUnload() {
      this.stopPoll()
      if (this.ime) { try { this.ime.destroy() } catch (e) {} }
    },

    goBack() {
      this.$page.finish()
    },

    switchMode(m) {
      if (this.mode === m) return
      this.mode = m
      if (m === 'qr') {
        // 回到扫码登录: 二维码需要重新可见, 补一次生成/轮询
        this.startQr()
      } else {
        // 电脑同步: 二维码已隐藏, 停止轮询避免无谓请求
        this.stopPoll()
      }
    },

    // ---- 扫码登录 ----
    startQr() {
      this.stopPoll()
      this.pollState = 'generating'
      this.pollError = ''
      this.qrError = ''
      this.qr = { size: 0, rows: [] }
      this.qrRows = []
      this.qrUrl = ''
      var self = this
      // 网络请求延后到首帧之后 (同步 httpGet 阻塞 JS 线程)
      afterPaint(async function () {
        try {
          const r = await qrcodeGenerate()
          if (!self.$page) return
          self.qrcodeKey = r.qrcodeKey
          self.qrGeneratedAt = Date.now()
          self.qrUrl = r.qrUrl
          // 二维码用本地编码器渲染成<div>矩阵: 不依赖任何外部图片服务.
          // (真机实测 qr.liantu.com / api.vvhan.com 均 DNS NXDOMAIN,
          //  外链图片永远加载不出来 -> 扫码区空白. 本地编码 100% 可用)
          self.renderQr(r.qrUrl)
          self.pollState = 'waiting'
          self.startPoll()
        } catch (err) {
          if (!self.$page) return
          self.qrError = err && err.message ? err.message : String(err)
          self.pollState = 'error'
          self.pollError = '生成失败: ' + self.qrError
        }
      })
    },

    renderQr(url) {
      try {
        const mod = makeQR(url)
        this.qr = mod
        this.qrRows = toRuns(mod)
      } catch (err) {
        this.qrError = err && err.message ? err.message : String(err)
        console.log('[login] 本地编码失败: ' + this.qrError)
        this.pollState = 'error'
        this.pollError = '渲染失败: ' + this.qrError
      }
    },

    startPoll() {
      this.stopPoll()
      var self = this
      const p = this.$page
      this.pollTimer = (p && p.setInterval)
        ? p.setInterval(function () { self.pollOnce() }, POLL_MS)
        : setInterval(function () { self.pollOnce() }, POLL_MS)
    },

    stopPoll() {
      if (this.pollTimer == null) return
      const p = this.$page
      if (p && p.clearInterval) p.clearInterval(this.pollTimer)
      else clearInterval(this.pollTimer)
      this.pollTimer = null
    },

    async pollOnce() {
      if (this.pollState !== 'waiting' && this.pollState !== 'scanned') return
      if (this.qrcodeKey === '') return
      if (Date.now() - this.qrGeneratedAt > QR_TTL_MS) {
        this.pollState = 'expired'
        this.stopPoll()
        return
      }
      try {
        const r = await qrcodePoll(this.qrcodeKey)
        if (r.state === 'ok') {
          this.stopPoll()
          saveLogin(r.cookies.sessdata, r.cookies.biliJct, r.cookies.dedeUserId)
          this.pollState = 'ok'
        } else if (r.state === 'expired') {
          this.stopPoll()
          this.pollState = 'expired'
        } else if (r.state === 'scanned') {
          this.pollState = 'scanned'
        }
      } catch (err) {
        // 单次轮询失败容忍 (网络抖动), 连续失败由过期时间兜底
        console.log('[login] poll error: ' + (err && err.message ? err.message : err))
      }
    },

    // ---- 电脑同步 ----
    async inputIp() {
      if (this.ime == null) this.ime = createIME()
      try {
        const text = await this.ime.open({
          text: this.pcIp,
          placeholder: '输入电脑 IP, 如 192.168.1.100',
          maxlength: 15,
          inputType: 'EnUSPreferred',
          enterButtonText: '确定',
          confirmText: '确定'
        })
        if (text === null) return
        this.pcIp = text.trim()
      } catch (err) {
        this.pcStatus = '输入法打开失败: ' + (err && err.message ? err.message : err)
        this.pcOk = false
      }
    },

    async fetchFromPc() {
      if (this.fetching) return
      this.pcStatus = ''
      this.pcOk = false
      this.fetching = true
      try {
        const cookies = await fetchPcCookie(this.pcIp)
        saveLogin(cookies.sessdata, cookies.biliJct, cookies.dedeUserId)
        // 校验: nav 接口带 Cookie 应返回 isLogin=true
        this.pcStatus = '已获取, 校验登录态…'
        const info = await getMyInfo()
        if (info.isLogin) {
          this.pcOk = true
          this.pcStatus = '✓ 登录成功: ' + info.uname + ' (Lv' + info.level + ')'
        } else {
          this.pcStatus = 'Cookie 无效或已过期 (isLogin=false)'
        }
      } catch (err) {
        this.pcStatus = err && err.message ? err.message : String(err)
      } finally {
        this.fetching = false
      }
    }
  }
}

// 矩阵 -> 按行暗色游程 [{x, w}], 减少渲染节点 (v6 约 450 个 vs 1681)
function toRuns(m) {
  var out = []
  for (var r = 0; r < m.size; r++) {
    var segs = []
    var c = 0
    while (c < m.size) {
      if (m.rows[r][c]) {
        var x = c
        while (c < m.size && m.rows[r][c]) c++
        segs.push({ x: x, w: c - x })
      } else {
        c++
      }
    }
    out.push(segs)
  }
  return out
}
</script>

<style scoped>
.page {
  position: absolute;
  left: 0px;
  top: 0px;
  width: 960px;
  height: 266px;
  background-color: #16181c;
}
.left {
  position: absolute;
  left: 0px;
  top: 0px;
  width: 680px;
  height: 266px;
}
/* 电脑同步: 无二维码区, 左区铺满全宽 960px, 不预留空白 */
.left-full {
  width: 960px;
}
.topbar {
  position: absolute;
  left: 0px;
  top: 0px;
  width: 680px;
  height: 44px;
  flex-direction: row;
  align-items: center;
  background-color: #21242b;
}
.topbar-full {
  width: 960px;
}
.back {
  width: 100px;
  height: 34px;
  margin-left: 12px;
  border-radius: 17px;
  background-color: #37404a;
  justify-content: center;
  align-items: center;
}
.back-text {
  font-size: 22px;
  color: #ffffff;
}
.title {
  font-size: 24px;
  color: #ffffff;
  margin-left: 14px;
}
.modes {
  position: absolute;
  left: 12px;
  top: 58px;
  width: 656px;
  height: 40px;
  flex-direction: row;
}
.mode-tab {
  height: 40px;
  padding-left: 20px;
  padding-right: 20px;
  border-radius: 20px;
  margin-right: 12px;
  background-color: #2a2f38;
  justify-content: center;
  align-items: center;
}
.mode-active {
  background-color: #fb7299;
}
.mode-text {
  font-size: 21px;
  color: #aab3bf;
}
.mode-text-active {
  color: #ffffff;
}
.qr-status {
  position: absolute;
  left: 12px;
  top: 116px;
  width: 656px;
  height: 140px;
}
.st {
  font-size: 25px;
  color: #ffffff;
  margin-bottom: 10px;
}
.st-ok {
  color: #3fd67a;
}
.st-err {
  color: #ff7a7a;
}
.st2 {
  font-size: 19px;
  color: #8a94a6;
  margin-bottom: 8px;
}
.btn {
  margin-top: 14px;
  width: 220px;
  height: 42px;
  border-radius: 21px;
  background-color: #fb7299;
  justify-content: center;
  align-items: center;
}
.btn-text {
  font-size: 21px;
  color: #ffffff;
}
/* 电脑同步: 全宽 936px (960 - 左右各 12 边距), 不再被右侧二维码挤压 */
.pc-wrap {
  position: absolute;
  left: 12px;
  top: 108px;
  width: 936px;
  height: 150px;
}
/* 电脑同步内容超出可视高度, 需可上下滑动 (用户反馈: cookie 页面无法上下滑动) */
.pc-scroll {
  width: 936px;
  height: 150px;
}
.pc-tip {
  font-size: 16px;
  color: #8a94a6;
  margin-bottom: 10px;
}
.pc-input {
  width: 936px;
  height: 42px;
  border-radius: 10px;
  background-color: #21242b;
  justify-content: center;
  padding-left: 14px;
}
.pc-input-text {
  font-size: 20px;
  color: #aab3bf;
  max-lines: 1;
  text-overflow: ellipsis;
  overflow: hidden;
}
.btn-row {
  flex-direction: row;
  margin-top: 10px;
}
.pc-status {
  margin-top: 8px;
  font-size: 19px;
}
/* 右区二维码: 常驻嵌入 (不随模式切换隐藏), 266 全高 */
.qr-zone {
  position: absolute;
  left: 680px;
  top: 0px;
  width: 280px;
  height: 266px;
  background-color: #21242b;
  justify-content: center;
  align-items: center;
}
/* 白盒尺寸与水平居中由 :style 动态给出 (随二维码版本变化).
   垂直居中: v6 白盒 245px, (266-245)/2 ≈ 10px */
.qr-box {
  position: absolute;
  top: 10px;
  background-color: #ffffff;
}
/* 每行: 必须是「整个二维码宽度」, 否则绝对定位的 .qr-dark 子元素
   会被行盒裁剪 / 无法定位 (曾因删掉 width 导致二维码整片空白).
   宽度与高度随 MOD 动态给出. */
.qr-row {
  position: absolute;
  left: 0px;
}
.qr-dark {
  position: absolute;
  top: 0px;
  background-color: #16181c;
}
/* 宽高由 :style 动态给出 (随二维码版本变化) */
.qr-mask {
  position: absolute;
  left: 0px;
  top: 0px;
  background-color: rgba(255, 255, 255, 0.9);
  justify-content: center;
  align-items: center;
}
.qr-mask-text {
  font-size: 40px;
  color: #16181c;
}
/* 过期/出错覆盖层: 可点按刷新 */
.qr-mask-tap {
  justify-content: center;
  align-items: center;
}
.qr-mask-sub {
  margin-top: 8px;
  font-size: 22px;
  color: #fb7299;
}
.qr-ph {
  font-size: 20px;
  color: #6a7684;
}
</style>
