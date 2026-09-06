<template>
  <div class="page">
    <!-- hole: 挖透显示视频. 视频面在本页之下或之上由 Weston 堆叠决定, 页面用与
         native 相同的拟合公式动态设置 hole 矩形, 两种堆叠态视觉一致
         (references/transparent.md). -->
    <hole class="hole" :style="holeStyle"></hole>

    <!-- 点击空白区域 显示/隐藏控制条; 控制条自身按钮拦截点击 -->
    <div class="stage" @click="toggleBar">

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
import { getVideoDetail, getPlayUrl } from '../../services/bili.js'

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
      titleText: '',
      statusText: '加载中…',
      barVisible: true,
      curMs: 0,
      durMs: 0,
      segList: (function () {
        var a = []
        for (var i = 0; i < SEG_COUNT; i++) a.push(i)
        return a
      })(),
      generation: 0,       // 异步世代: 换源/离开后过期回调不写界面
      videoW: 0,           // 原生上报的视频分辨率 (驱动动态 hole)
      videoH: 0,
      pollTimer: null,
      hideTimer: null
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
    durText: function () { return fmtMs(this.durMs) },
    // 动态 hole 矩形: 与 native fitRect 完全相同的整数拟合 (等比信箱居中).
    // 视频带内缩到上下控制条之间 (44..222 逻辑), 任何堆叠态视频都不遮挡控制条;
    // 视频分辨率未知时退回该带全宽.
    holeStyle: function () {
      var vw = this.videoW, vh = this.videoH
      var bandY = 44, bandH = 178
      if (!vw || !vh) return { left: '0px', top: bandY + 'px', width: '960px', height: bandH + 'px' }
      var ar = vw / vh
      var fw = 960
      var fh = Math.round(fw / ar)
      if (fh > bandH) { fh = bandH; fw = Math.round(fh * ar) }
      var x = Math.floor((960 - fw) / 2)
      var y = bandY + Math.floor((bandH - fh) / 2)
      return { left: x + 'px', top: y + 'px', width: fw + 'px', height: fh + 'px' }
    }
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
      this.videoW = 0
      this.videoH = 0
      this.applyOptions(options || {})
      this.loadAndPlay()
    },

    onHide: function () {
      this.stopPolling()
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

      var self = this
      if (this.directUrl !== '') {
        this.statusText = '加载中…'
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
        this.showBar()
        return
      }
      if (s.indexOf('eos') >= 0 || s.indexOf('ended') >= 0) {
        this.statusText = '播放结束'
        this.playing = false
        this.stopPolling()
        this.showBar()
        return
      }
      if (s.indexOf('play') >= 0) {
        this.playing = true
        if (this.statusText !== '') this.statusText = ''
        this.startPolling()
        this.scheduleHideBar()
        return
      }
      if (s.indexOf('pause') >= 0) {
        this.playing = false
        this.showBar()
        return
      }
      // ready/buffering 过渡态只在未开播时显示; closed/duration 不落界面
      if (s === 'ready' || s === 'buffering' || s === 'loading') {
        if (!this.playing) this.statusText = '加载中…'
      }
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
        var vs = player.getVideoSize()
        if (vs.width > 0 && vs.height > 0 &&
            (vs.width !== self.videoW || vs.height !== self.videoH)) {
          self.videoW = vs.width
          self.videoH = vs.height
        }
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
/* 挖透显示视频: 默认为上下控制条之间的带区, 视频分辨率就绪后由
   holeStyle 内联样式精确对齐视频拟合矩形 */
.hole {
  position: absolute;
  left: 0px;
  top: 44px;
  width: 960px;
  height: 178px;
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
  width: 120px;
  height: 34px;
  margin-left: 12px;
  border-radius: 17px;
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
  max-lines: 1;
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
  height: 32px;
  margin-right: 8px;
  border-radius: 16px;
  background-color: rgba(255, 255, 255, 0.18);
  justify-content: center;
  align-items: center;
}
.btn-mini {
  width: 66px;
}
.btn-main {
  width: 74px;
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
