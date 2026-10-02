<template>
  <div class="page">
    <!-- hole: 全带挖透, 视频由设备侧等比拟合全带 (信箱居中, 不裁切不变形),
         Weston 视频 surface 在 UI 之下透出 (references/transparent.md) -->
    <hole class="hole"></hole>

    <!-- 点击空白区域 显示/隐藏控制条; 控制条自身按钮拦截点击 -->
    <div class="stage" @click="toggleBar" @touchstart="markUserTouch">

      <!-- 顶部悬浮栏: 返回 + 标题 (悬浮于视频上方) -->
      <div v-if="barVisible" class="top-bar">
        <div class="back" @click="goBack">
          <text class="back-text">‹ 返回</text>
        </div>
        <text class="title">{{ titleText }}</text>
      </div>

      <!-- 中央状态提示 -->
      <div v-if="statusText !== ''" class="center">
        <text class="status">{{ statusText }}</text>
      </div>

      <!-- 底部悬浮控制条: 播放/快进退 + 进度条 + 时间 (悬浮于视频上方) -->
      <div v-if="barVisible" class="ctrl">
        <div class="btn btn-mini" @click="seekBack">
          <text class="btn-text">«10s</text>
        </div>
        <div class="btn btn-main" @click="togglePlay">
          <text class="btn-text">{{ playing ? '❚❚' : '▶' }}</text>
        </div>
        <div class="btn btn-mini" @click="seekForward">
          <text class="btn-text">10s»</text>
        </div>
        <!-- 进度条: 按 width% 渲染播放位置, 叠 N 个隐形点击分段实现点击调节 -->
        <div class="seek">
          <div class="track">
            <div class="fill" :style="fillStyle"></div>
            <div class="thumb" :style="thumbStyle"></div>
          </div>
          <div class="segs">
            <div v-for="seg in segList" :key="seg" class="seg" @click="seekBySeg(seg)"></div>
          </div>
        </div>
        <text class="time">{{ curText }}/{{ durText }}</text>
      </div>
    </div>
  </div>
</template>

<script>
// 播放页 v2 (全重写)
// - 视频由 gstplayer 原生层 (gstplayerd 守护进程) 播放: waylandsink 进 Weston
//   合成, 在 UI 之下; 本页全屏 <hole> 透出视频, 控制条悬浮在视频上方.
// - 视频尺寸自动适配: 设备侧按视频分辨率等比拟合屏幕 UI 带 (信箱式), 页面零几何.
// - 生命周期契约:
//     首次 onShow        读 options -> 取流地址 -> open/start, 订阅原生状态
//     onNewOptions       同一 player 页被 navTo 重开 -> 换源重播
//     onHide             暂停播放并停轮询 (回前台由用户手动恢复)
//     onUnload           单一 stop 路径: generation++ -> 停 timer/订阅 -> close native
import * as player from '../../services/player.js'
import * as screenon from '../../services/screenon.js'
import { getVideoDetail, getPlayUrl } from '../../services/bili.js'
import { afterPaint } from '../../base-page.js'
import { log } from '../../services/log.js'

var SEG_COUNT = 24       // 进度条点击分段数
var POLL_MS = 500        // 进度轮询周期
var BAR_HIDE_MS = 5000   // 播放中控制条自动隐藏延时
var SEEK_STEP_MS = 10000 // 快退/快进步长

function pad2(n) { return n < 10 ? '0' + n : '' + n }

function fmtMs(ms) {
  var total = Math.floor(ms / 1000)
  var h = Math.floor(total / 3600)
  var m = Math.floor((total % 3600) / 60)
  var s = total % 60
  if (h > 0) return h + ':' + pad2(m) + ':' + pad2(s)
  return m + ':' + pad2(s)
}

function setTimer(vm, ms, fn) {
  var p = vm.$page
  if (p && p.setTimeout) return p.setTimeout(fn, ms)
  return setTimeout(fn, ms)
}
function setTicker(vm, ms, fn) {
  var p = vm.$page
  if (p && p.setInterval) return p.setInterval(fn, ms)
  return setInterval(fn, ms)
}
function clearTimer(vm, token) {
  if (token == null) return
  var p = vm.$page
  if (p && p.clearTimeout) p.clearTimeout(token); else clearTimeout(token)
}
function clearTicker(vm, token) {
  if (token == null) return
  var p = vm.$page
  if (p && p.clearInterval) p.clearInterval(token); else clearInterval(token)
}

export default {
  name: 'player',
  data: function () {
    return {
      bvid: '',
      pageNo: 1,
      directUrl: '',       // 调试直链, 由 options.url 传入
      inited: false,
      opened: false,
      playing: false,
      started: false,      // 是否出过画面: 区分「未开播」与「暂停后重发 ready/buffering」
      titleText: '',
      statusText: '加载中…',
      barVisible: true,
      lastUserTouchAt: 0,   // 最近一次用户真实触摸 (保活注入避让用)
      curMs: 0,
      durMs: 0,
      segList: (function () {
        var a = []
        for (var i = 0; i < SEG_COUNT; i++) a.push(i)
        return a
      })(),
      generation: 0,       // 异步世代: 换源/离开后过期回调不写界面
      pollTimer: null,
      hideTimer: null,
      keepTimer: null      // 防息屏注入定时器 (播放中每 4s 一次)
    }
  },
  computed: {
    fillPct: function () {
      if (!this.durMs) return 0
      var pct = (this.curMs / this.durMs) * 100
      if (pct < 0) return 0
      if (pct > 100) return 100
      return pct
    },
    fillStyle: function () { return { width: this.fillPct + '%' } },
    thumbStyle: function () { return { left: this.fillPct + '%' } },
    curText: function () { return fmtMs(this.curMs) },
    durText: function () { return fmtMs(this.durMs) }
  },
  methods: {
    // ---------------- 生命周期 ----------------
    onShow: function () {
      if (!this.inited) {
        // 同页 navTo 的 onNewOptions 只发到 Page 实例, 需显式挂钩到组件
        if (this.$page && !this._newOptionsBound) {
          this._newOptionsBound = true
          var self = this
          this.$page.onNewOptions = function (options) { self.onNewOptions(options) }
        }
        this.applyOptions((this.$page && this.$page.options) || {})
        this.loadAndPlay()
        return
      }
      if (this.opened && !this.playing) this.showBar()
      this.startPolling()
    },

    onNewOptions: function (options) {
      console.log('[player] onNewOptions bvid=' + (options && options.bvid))
      this.generation++
      this.stopPolling()
      this.cancelHideBar()
      if (this.opened) {
        try { player.close() } catch (e) {}
        this.opened = false
      }
      this.playing = false
      this.curMs = 0
      this.durMs = 0
      this.applyOptions(options || {})
      this.loadAndPlay()
    },

    onHide: function () {
      this.stopPolling()
      this.stopKeepAwake()
      screenon.screenOnStop()
      this.cancelHideBar()
      if (this.opened && this.playing) {
        try { player.pause() } catch (e) {}
        this.playing = false
      }
      this.showBar()
    },

    onUnload: function () {
      this.generation++
      this.stopPolling()
      this.stopKeepAwake()
      screenon.screenOnStop()
      this.cancelHideBar()
      try { player.offState(this.onNativeState) } catch (e) {}
      if (this.opened) {
        try { player.close() } catch (e) {}
        this.opened = false
      }
      this.playing = false
    },

    // ---------------- 打开与换源 ----------------
    applyOptions: function (options) {
      this.bvid = options.bvid || ''
      this.pageNo = parseInt(options.page || '1', 10) || 1
      this.titleText = options.title || ''
      this.directUrl = options.url || ''
    },

    loadAndPlay: function () {
      var gen = ++this.generation
      if (!player.isSupported()) {
        this.statusText = '当前固件不支持视频播放 (缺少 gstplayer 模块)'
        return
      }
      player.onState(this.onNativeState)
      this.inited = true

      // 先让首帧画出「加载中…」再取流地址: bilinet.httpGet 同步阻塞 JS 线程,
      // 不延迟的话网络差时加载态画不出来, 表现为上一页面冻结 (卡死)
      var self = this
      afterPaint(function () { self.fetchAndOpen(gen) })
    },

    // 网络取流 (首帧绘制后执行): 取详情/播放地址 -> 打开流
    fetchAndOpen: function (gen) {
      if (gen !== this.generation || !this.$page) return
      var self = this
      if (this.directUrl !== '') {
        this.statusText = '缓冲中…'
        this.openStream(this.directUrl, gen)
        return
      }
      if (!this.bvid) {
        this.statusText = '缺少视频参数 (bvid)'
        return
      }
      this.statusText = '加载中…'
      getVideoDetail(this.bvid).then(function (detail) {
        if (gen !== self.generation) return
        if (self.titleText === '' && detail.title) self.titleText = detail.title
        var page = detail.pages && detail.pages.length > 0
          ? detail.pages[Math.min(self.pageNo, detail.pages.length) - 1]
          : null
        var cid = page ? page.cid : 0
        if (!cid) throw new Error('未找到视频 cid')
        return getPlayUrl(self.bvid, cid)
      }).then(function (play) {
        if (gen !== self.generation || !play) return
        if (play.duration > 0) self.durMs = play.duration
        self.openStream(play.url, gen)
      }).catch(function (err) {
        if (gen !== self.generation) return
        self.statusText = err && err.message ? err.message : String(err)
        self.playing = false
        self.showBar()
      })
    },

    openStream: function (url, gen) {
      if (gen !== this.generation) return
      this.started = false   // 换源/重开: 过渡态重新允许显示「加载中」
      try {
        this.statusText = '缓冲中…'
        player.open(url)
      } catch (e) {
        this.statusText = '打开失败: ' + (e && e.message ? e.message : String(e))
        this.showBar()
        return
      }
      // open 可能同步派发错误状态 (此时 onNativeState 已置错误提示);
      // playing/控制条自动隐藏一律由原生 play 状态驱动, 不在此乐观置位,
      // 否则 open 报错后 playing 残留 true, 控制条 5s 后自动隐藏.
      if (this.statusText.indexOf('播放错误') === 0) return
      this.opened = true
      player.start()
      this.startPolling()
    },

    // ---------------- 原生状态回调 ----------------
    onNativeState: function (state) {
      if (!this.$page) return
      var s = String(state || '').toLowerCase()
      console.log('[player] stateChanged: ' + s)
      if (s.indexOf('error') === 0) {
        this.statusText = '播放错误: ' + state
        this.playing = false
        this.stopPolling()
        this.stopKeepAwake()
        this.showBar()
        return
      }
      if (s.indexOf('eos') >= 0 || s.indexOf('ended') >= 0) {
        this.statusText = '播放结束'
        this.playing = false
        this.stopPolling()
        this.stopKeepAwake()
        this.showBar()
        return
      }
      // 注意: 'pause' 包含子串 'play', 必须先判 pause 再判 play
      // (0.9.3 前 play 在前, pause 状态被误判为播放中)
      if (s.indexOf('pause') >= 0) {
        this.playing = false
        this.stopPolling()   // 暂停后 getPosition 可能返回 0, 轮询会把进度打回 0:00
        this.stopKeepAwake() // 暂停时不注入输入事件, 不干扰用户点按
        this.showBar()
        return
      }
      if (s.indexOf('play') >= 0) {
        var firstPlay = !this.started
        this.playing = true
        this.started = true  // 已出过画面: 之后不再显示「加载中」过渡态
        if (this.statusText !== '') this.statusText = ''
        this.startPolling()
        this.startKeepAwake()
        // 首播出画: 修复视频 surface 盖住 UI 的层级问题 (真机实测点一下屏幕恢复)
        if (firstPlay) this.fixLayer()
        this.scheduleHideBar()
        return
      }
      // ready/buffering 过渡态只在「从未播过」时显示;
      // 暂停后 native 常重发 ready/buffering, 用 started 区分, 否则暂停会误显示「加载中」
      if (s === 'ready' || s === 'buffering' || s === 'loading') {
        if (!this.started) this.statusText = '加载中…'
      }
    },

    // ---------------- 层级修复 + 防息屏 ----------------
    // 真机实测: 视频面 (waylandsink) 初始盖在整个 UI 之上, 点一下屏幕 UI 重新置顶.
    // 首播出画后自动等效「点一下」: exec 注入合成点击 (press+release);
    // 起播后连踢三次 (0 / 0.8s / 2s), 覆盖 surface 创建 / 首帧渲染 / 稳定三个时机.
    // exec 不可用时退化为控制条 v-if 翻转强制 UI 重新合成.
    fixLayer: function () {
      var self = this
      if (!player.keepAwakeSupported()) {
        var was = this.barVisible
        this.barVisible = !this.barVisible
        setTimer(this, 120, function () { self.barVisible = was })
        return
      }
      this.kickLayer()
      setTimer(this, 800, function () { if (self.playing) self.kickLayer() })
      setTimer(this, 2000, function () { if (self.playing) self.kickLayer() })
    },

    // 合成一次点击 (让 UI 重新置顶), 随后把控制条恢复为显示态
    kickLayer: function () {
      var self = this
      player.tapScreen()
      setTimer(this, 260, function () { self.showBar() })
    },

    // 播放中防息屏: 官方 JSAPI 优先 (系统播放器同款三件套, 见 services/screenon.js),
    // 不可用或持续失败时降级 exec 注入 touch move (重置输入空闲计时, 无副作用).
    // 用户真实触摸 (拖进度条/点按钮) —— 保活注入要避让, 别打断操作
    markUserTouch: function () {
      this.lastUserTouchAt = Date.now()
    },

    // 保活注入: 真机实测 **只有 press/release 能重置息屏计时**
    // (send_event 的 slip 不行 —— 0.9.10 只注 slip, 播放 35s 后屏幕照样黑).
    // 但点击 stage 会切换控制条, 所以点完立刻把控制条恢复成原状态,
    // 视觉上只有 ~80ms 的一闪.
    keepAwakeTap: function () {
      var self = this
      var wasVisible = this.barVisible
      player.tapScreen()
      setTimer(this, 80, function () {
        if (wasVisible) self.showBar(); else self.hideBar()
      })
    },

    startKeepAwake: function () {
      if (this.keepTimer != null) return
      var self = this
      var jsapiOn = screenon.screenOnAvailable()
      if (jsapiOn) screenon.screenOnStart()
      try { log('播放器', '防息屏: JSAPI=' + (jsapiOn ? 'on' : 'off') + ' + 每 6s 合成点击保活') } catch (e) {}
      // 系统息屏阈值实测 ~10s, 6s 一次留出余量; JSAPI 仍照调 (能生效更好)
      this.keepTimer = setTicker(this, 6000, function () {
        if (!self.playing) return
        if (screenon.screenOnAvailable()) {
          try { screenon.screenOnTick() } catch (e) {}
        }
        if (Date.now() - (self.lastUserTouchAt || 0) < 2500) return
        self.keepAwakeTap()
      })
    },
    stopKeepAwake: function () {
      clearTicker(this, this.keepTimer)
      this.keepTimer = null
    },

    // ---------------- 进度轮询 ----------------
    startPolling: function () {
      if (this.pollTimer != null || !this.opened) return
      var self = this
      this.pollTimer = setTicker(this, POLL_MS, function () {
        if (!self.opened) return
        var dur = player.getDuration()
        if (dur > 0) self.durMs = dur
        self.curMs = player.getPosition()
      })
    },
    stopPolling: function () {
      clearTicker(this, this.pollTimer)
      this.pollTimer = null
    },

    // ---------------- 控制条显隐 ----------------
    showBar: function () {
      this.barVisible = true
      this.cancelHideBar()
      if (this.playing) this.scheduleHideBar()
    },
    toggleBar: function () {
      if (this.barVisible) this.hideBar(); else this.showBar()
    },
    hideBar: function () {
      this.cancelHideBar()
      this.barVisible = false
    },
    scheduleHideBar: function () {
      this.cancelHideBar()
      if (!this.playing) return
      var self = this
      this.hideTimer = setTimer(this, BAR_HIDE_MS, function () {
        self.hideTimer = null
        if (self.playing) self.hideBar()
      })
    },
    cancelHideBar: function () {
      clearTimer(this, this.hideTimer)
      this.hideTimer = null
    },

    // ---------------- 播放控制 ----------------
    togglePlay: function () {
      if (!this.opened) return
      this.showBar()
      try {
        if (this.playing) {
          player.pause()
          this.playing = false
          this.stopPolling()   // 立即停轮询, 保住当前进度 (暂停后 getPosition 可能返回 0)
        } else {
          player.resume()
          this.playing = true
          if (this.statusText === '播放结束') this.statusText = ''
          this.startPolling()
          this.scheduleHideBar()
        }
      } catch (e) {
        this.statusText = '控制失败: ' + (e && e.message ? e.message : String(e))
      }
    },

    seekBack: function () { this.seekBy(-SEEK_STEP_MS) },
    seekForward: function () { this.seekBy(SEEK_STEP_MS) },
    seekBy: function (deltaMs) {
      if (!this.opened) return
      this.showBar()
      this.applySeek(player.getPosition() + deltaMs)
    },

    // 进度条分段点击: segIndex 0..N-1 -> 跳到 (i+0.5)/SEG_COUNT 处
    seekBySeg: function (segIndex) {
      if (!this.opened) return
      this.showBar()
      var dur = this.durMs || player.getDuration()
      if (dur <= 0) return
      this.applySeek(Math.round(((segIndex + 0.5) / SEG_COUNT) * dur))
    },

    applySeek: function (targetMs) {
      var dur = this.durMs || player.getDuration()
      var t = targetMs
      if (dur > 0 && t > dur - 500) t = dur - 500
      if (t < 0) t = 0
      try {
        player.seek(t)
        this.curMs = t
        if (this.statusText === '播放结束') this.statusText = ''
      } catch (e) {
        console.log('[player] seek error: ' + (e && e.message ? e.message : e))
      }
    },

    goBack: function () {
      this.$page.finish()
    }
  }
}
</script>

<style scoped>
.page {
  position: absolute;
  left: 0px;
  top: 0px;
  width: 960px;
  height: 266px;
  /* 不透明黑: 信箱区/条带区无论视频面在 UI 上方还是下方都呈黑色,
     hole 矩形与视频矩形由同一公式给出, 两种堆叠态视觉一致 */
  background-color: #000000;
}
/* 全带挖透: 视频由 native 等比拟合全带 (信箱居中, 不裁切), UI 之下透出 */
.hole {
  position: absolute;
  left: 0px;
  top: 0px;
  width: 960px;
  height: 266px;
}
.stage {
  position: absolute;
  left: 0px;
  top: 0px;
  width: 960px;
  height: 266px;
}
.top-bar {
  position: absolute;
  left: 0px;
  top: 0px;
  width: 960px;
  height: 44px;
  flex-direction: row;
  align-items: center;
  background-color: rgba(0, 0, 0, 0.55);
}
.back {
  width: 132px;
  height: 38px;
  margin-left: 12px;
  border-radius: 19px;
  background-color: rgba(255, 255, 255, 0.18);
  justify-content: center;
  align-items: center;
}
.back-text {
  font-size: 22px;
  color: #ffffff;
}
.title {
  font-size: 22px;
  color: #ffffff;
  margin-left: 16px;
  /* Falcon 不支持 max-lines (0.9.3 前无效, 长标题换行溢出顶栏), 用 lines: 1 */
  lines: 1;
  text-overflow: ellipsis;
  overflow: hidden;
}
.center {
  position: absolute;
  left: 0px;
  top: 111px;
  width: 960px;
  height: 44px;
  align-items: center;
  justify-content: center;
}
.status {
  font-size: 20px;
  color: #e6a23c;
}
.ctrl {
  position: absolute;
  left: 0px;
  top: 222px;
  width: 960px;
  height: 44px;
  padding-left: 10px;
  padding-right: 10px;
  background-color: rgba(0, 0, 0, 0.55);
  flex-direction: row;
  align-items: center;
}
.btn {
  height: 36px;
  margin-right: 8px;
  border-radius: 18px;
  background-color: rgba(255, 255, 255, 0.18);
  justify-content: center;
  align-items: center;
}
.btn-mini {
  width: 72px;
}
.btn-main {
  width: 88px;
  background-color: #fb7299;
}
.btn-text {
  font-size: 20px;
  color: #ffffff;
}
.seek {
  width: 500px;
  height: 44px;
  flex-direction: row;
  align-items: center;
}
.track {
  position: absolute;
  left: 0px;
  top: 18px;
  width: 500px;
  height: 8px;
  border-radius: 4px;
  background-color: rgba(255, 255, 255, 0.25);
}
.fill {
  position: absolute;
  left: 0px;
  top: 0px;
  height: 8px;
  border-radius: 4px;
  background-color: #fb7299;
}
.thumb {
  position: absolute;
  top: -4px;
  width: 16px;
  height: 16px;
  margin-left: -8px;
  border-radius: 8px;
  background-color: #ffffff;
}
.segs {
  width: 500px;
  height: 44px;
  flex-direction: row;
}
.seg {
  width: 20.83px;
  height: 44px;
}
.time {
  font-size: 16px;
  color: #ffffff;
  width: 160px;
  margin-left: 8px;
}
</style>
