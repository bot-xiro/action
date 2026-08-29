<template>
  <div class="page">
    <!-- 点击空白区域 显示/隐藏控制条; 控制条上按钮各自拦截, Falcone 点击不冒泡 -->
    <div class="stage" @click="toggleBar">

      <!-- 顶栏: 返回 + 标题 -->
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

      <!-- 底部控制区 -->
      <div v-if="barVisible" class="ctrl">
        <!-- 进度条: 底部为按 width% 渲染的播放位置, 上面叠 N 个隐形点击分段实现点击调节 -->
        <div class="seek">
          <div class="track">
            <div class="fill" :style="fillStyle"></div>
            <div class="thumb" :style="thumbStyle"></div>
          </div>
          <div class="segs">
            <div v-for="seg in segList" :key="seg" class="seg" @click="seekBySeg(seg)"></div>
          </div>
          <text class="time">{{ curText }} / {{ durText }}</text>
        </div>

        <!-- 播放控制按钮 -->
        <div class="btns">
          <div class="btn" @click="seekBack">
            <text class="btn-text">« 10s</text>
          </div>
          <div class="btn btn-main" @click="togglePlay">
            <text class="btn-text">{{ playing ? '❚❚ 暂停' : '▶ 播放' }}</text>
          </div>
          <div class="btn" @click="seekForward">
            <text class="btn-text">10s »</text>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import * as player from '../../services/player.js'
import { getVideoDetail, getPlayUrl } from '../../services/bili.js'

// 逻辑坐标 960x266 (profile 已验证); 进度条 24 个点击分段
var SEG_COUNT = 24
var POLL_MS = 500
var BAR_HIDE_MS = 5000
var SEEK_STEP_MS = 10000

function pad2(n) { return n < 10 ? '0' + n : '' + n }

function fmtMs(ms) {
  var total = Math.floor(ms / 1000)
  var h = Math.floor(total / 3600)
  var m = Math.floor((total % 3600) / 60)
  var s = total % 60
  if (h > 0) return h + ':' + pad2(m) + ':' + pad2(s)
  return m + ':' + pad2(s)
}

export default {
  name: 'player',
  data: function () {
    return {
      bvid: '',
      pageNo: 1,
      titleText: '',
      statusText: '加载中…',
      playing: false,
      barVisible: true,
      curMs: 0,
      durMs: 0,
      generation: 0,
      opened: false,
      segList: (function () {
        var a = []
        for (var i = 0; i < SEG_COUNT; i++) a.push(i)
        return a
      })()
    }
  },
  computed: {
    fillPct: function () {
      if (!this.durMs) return 0
      var pct = (this.curMs / this.durMs) * 100
      if (pct < 0) pct = 0
      if (pct > 100) pct = 100
      return pct
    },
    fillStyle: function () {
      return { width: this.fillPct + '%' }
    },
    // 进度条圆点: left 百分比, 用负 margin 自行居中
    thumbStyle: function () {
      return { left: this.fillPct + '%' }
    },
    curText: function () { return fmtMs(this.curMs) },
    durText: function () { return fmtMs(this.durMs) }
  },
  methods: {
    onShow: function () {
      var opts = (this.$page && this.$page.options) || {}
      this.bvid = opts.bvid || ''
      this.pageNo = parseInt(opts.page || '1', 10) || 1
      this.titleText = opts.title || ''
      this.loadAndPlay()
    },

    onHide: function () {
      // 进入后台(含系统输入法等)暂停, 回前台后手动恢复
      this.stopPolling()
      this.cancelHideBar()
      if (this.opened && this.playing) {
        try { player.pause() } catch (e) {}
        this.playing = false
      }
      this.pushState('paused-hide')
    },

    loadAndPlay: function () {
      var self = this
      var gen = ++this.generation
      if (!player.isSupported()) {
        this.statusText = '当前固件不支持视频播放 (缺少 gstplayer 模块)'
        return
      }
      // 原生状态订阅 (与 onUnload 的 offState 严格成对)
      player.onState(this.onNativeState)
      this.prepareAndOpen(gen)
    },

    prepareAndOpen: function (gen) {
      var self = this
      var useDirectUrl = function (url) {
        if (gen !== self.generation) return
        self.openStream(url)
      }
      var onErr = function (e) {
        if (gen !== self.generation) return
        self.statusText = e && e.message ? e.message : String(e)
        self.playing = false
      }
      var directUrl = (this.$page && this.$page.options && this.$page.options.url) || ''
      if (directUrl !== '') {
        // 调试直达: miniapp_cli start <appid> --player --url <mp4> 的形态经 options 传入
        useDirectUrl(directUrl)
        return
      }
      if (!this.bvid) {
        // 测试入口: 无参数时播一段公网 MP4, 证明硬解/kmssink/进度/控制链路通
        this.titleText = '测试视频 Big Buck Bunny 10s'
        useDirectUrl('https://test-videos.co.uk/vids/bigbuckbunny/mp4/h264/360/Big_Buck_Bunny_360_10s_1MB.mp4')
        return
      }
      getVideoDetail(this.bvid).then(function (detail) {
        if (gen !== self.generation) return
        if (self.titleText === '' && detail.title) self.titleText = detail.title
        var page = detail.pages && detail.pages.length > 0
          ? detail.pages[Math.min(self.pageNo, detail.pages.length) - 1]
          : null
        var cid = page ? page.cid : 0
        if (!cid) throw new Error('未找到视频 cid')
        return getPlayUrl(self.bvid, cid).then(function (play) {
          if (gen !== self.generation) return
          if (play.duration > 0) self.durMs = play.duration
          self.openStream(play.url)
        })
      }).catch(onErr)
    },

    openStream: function (url) {
      try {
        // rect 为逻辑坐标 x,y,w,h; 原生层完成 LOGIC->PHYS 的 KMS 变换
        player.open(url, '0,44,960,126')
        this.opened = true
        this.statusText = '缓冲中…'
        player.start()
        this.playing = true
        this.startPolling()
        this.scheduleHideBar()
      } catch (e) {
        this.statusText = '打开失败: ' + (e && e.message ? e.message : String(e))
        this.playing = false
      }
    },

    // ---------- 原生状态 (stateChanged 信号转发) ----------
    onNativeState: function (state) {
      var self = this
      if (!self.$page) return
      var s = String(state || '').toLowerCase()
      console.log('[player]' + ' stateChanged: ' + s)
      if (s.indexOf('error') === 0 || s.indexOf('err') === 0) {
        self.statusText = '播放错误: ' + state
        self.playing = false
        self.showBar()
        return
      }
      if (s === 'eos' || s === 'ended' || s.indexOf('eos') >= 0) {
        self.statusText = '播放结束'
        self.playing = false
        self.stopPolling()
        self.showBar()
        return
      }
      if (s.indexOf('play') >= 0 || s.indexOf('playing') >= 0) {
        self.playing = true
        if (self.statusText !== '') self.statusText = ''
        self.startPolling()
        self.scheduleHideBar()
        return
      }
      if (s.indexOf('pause') >= 0) {
        self.playing = false
        self.showBar()
        return
      }
      // ready/opening 等过渡状态不改变界面文字；
      // duration/closed 等原生事件不显示，避免屏幕中间闪出英文
      if (s === 'ready' || s === 'buffering' || s === 'loading') {
        if (!self.playing) self.statusText = '加载中…'
      }
    },

    pushState: function () {},

    // ---------- 进度轮询 ----------
    startPolling: function () {
      if (this._pollTimer) return
      var self = this
      this._pollTimer = setInterval(function () {
        if (!self.opened) return
        var pos = player.getPosition()
        var dur = player.getDuration()
        if (dur > 0) self.durMs = dur
        if (pos >= 0) self.curMs = pos
      }, POLL_MS)
    },
    stopPolling: function () {
      if (this._pollTimer) {
        clearInterval(this._pollTimer)
        this._pollTimer = null
      }
    },

    // ---------- 控制条显隐 ----------
    showBar: function () {
      this.barVisible = true
      this.cancelHideBar()
      if (this.playing) this.scheduleHideBar()
    },
    toggleBar: function () {
      if (this.barVisible) {
        this.cancelHideBar()
        this.barVisible = false
      } else {
        this.showBar()
      }
    },
    scheduleHideBar: function () {
      this.cancelHideBar()
      if (!this.playing) return
      var self = this
      this._hideTimer = setTimeout(function () {
        self._hideTimer = null
        if (self.playing) self.barVisible = false
      }, BAR_HIDE_MS)
    },
    cancelHideBar: function () {
      if (this._hideTimer) {
        clearTimeout(this._hideTimer)
        this._hideTimer = null
      }
    },

    // ---------- 播放控制 ----------
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
        }
      } catch (e) {
        this.statusText = '控制失败: ' + (e && e.message ? e.message : String(e))
      }
    },

    seekBack: function () {
      this.seekBy(-SEEK_STEP_MS)
    },
    seekForward: function () {
      this.seekBy(SEEK_STEP_MS)
    },
    seekBy: function (deltaMs) {
      if (!this.opened) return
      this.showBar()
      var target = player.getPosition() + deltaMs
      var dur = this.durMs || player.getDuration()
      if (dur > 0 && target > dur) target = dur - 500
      if (target < 0) target = 0
      try {
        player.seek(target)
        this.curMs = target
        this.statusText = ''
      } catch (e) {
        console.log('[player] seekBy error: ' + (e && e.message ? e.message : e))
      }
    },

    // 进度条分段点击: segIndex 0..23 -> 跳到 (i+0.5)/SEGCOUNT 处
    seekBySeg: function (segIndex) {
      if (!this.opened) return
      this.showBar()
      var dur = this.durMs || player.getDuration()
      if (dur <= 0) return
      var target = Math.round(((segIndex + 0.5) / SEG_COUNT) * dur)
      try {
        player.seek(target)
        this.curMs = target
        this.statusText = ''
      } catch (e) {
        console.log('[player] seekBySeg error: ' + (e && e.message ? e.message : e))
      }
    },

    goBack: function () {
      this.$page.finish()
    },

    onUnload: function () {
      // stop 顺序: 递增 generation -> 停 timer/订阅 -> 关闭 native 管道
      this.generation++
      this.stopPolling()
      this.cancelHideBar()
      try { player.offState(this.onNativeState) } catch (e) {}
      if (this.opened) {
        try {
          player.close()
        } catch (e) {}
        this.opened = false
        this.playing = false
      }
    }
  }
}
</script>

<style scoped>
.page {
  width: 960px;
  height: 266px;
  background-color: #000000;
}
.stage {
  width: 960px;
  height: 266px;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
}
.top-bar {
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
  height: 44px;
  align-items: center;
  justify-content: center;
}
.status {
  font-size: 20px;
  color: #e6a23c;
}
.ctrl {
  width: 960px;
  height: 96px;
  background-color: rgba(0, 0, 0, 0.55);
  flex-direction: column;
  justify-content: center;
}
.seek {
  width: 920px;
  height: 34px;
  margin-left: 20px;
  flex-direction: row;
  align-items: center;
}
.track {
  position: absolute;
  left: 20px;
  top: 12px;
  width: 760px;
  height: 10px;
  border-radius: 5px;
  background-color: rgba(255, 255, 255, 0.25);
}
.fill {
  position: absolute;
  left: 0px;
  top: 0px;
  height: 10px;
  border-radius: 5px;
  background-color: #fb7299;
}
.thumb {
  position: absolute;
  top: -4px;
  width: 18px;
  height: 18px;
  margin-left: -9px;
  border-radius: 9px;
  background-color: #ffffff;
}
.segs {
  width: 760px;
  height: 34px;
  flex-direction: row;
}
.seg {
  width: 31.66px;
  height: 34px;
}
.time {
  font-size: 18px;
  color: #ffffff;
  width: 150px;
  margin-left: 10px;
}
.btns {
  width: 920px;
  height: 52px;
  margin-left: 20px;
  flex-direction: row;
  align-items: center;
  justify-content: center;
}
.btn {
  width: 150px;
  height: 42px;
  margin-left: 30px;
  margin-right: 30px;
  border-radius: 21px;
  background-color: rgba(255, 255, 255, 0.18);
  justify-content: center;
  align-items: center;
}
.btn-main {
  width: 200px;
  background-color: #fb7299;
}
.btn-text {
  font-size: 22px;
  color: #ffffff;
}
</style>
