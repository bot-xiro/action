<template>
  <div class="root">
    <!-- 加载/错误状态 -->
    <div v-if="!ready" class="center">
      <text class="hint">{{ error || '加载播放地址...' }}</text>
    </div>

    <!-- 播放区 -->
    <div v-else class="player-area">
      <hole ref="videoHole" class="video-hole"></hole>
    </div>

    <!-- 底部控制条 -->
    <div class="controls">
      <text class="ctrl-title" :lines="1">{{ title }}</text>
      <text class="ctrl-btn" @click="pauseVideo">{{ paused ? '▶ 播放' : '⏸ 暂停' }}</text>
      <text class="ctrl-btn" @click="closePlayer">✕ 关闭</text>
    </div>
  </div>
</template>

<style scoped>
.root {
  flex: 1;
  background-color: #000000;
  flex-direction: column;
}

/* 播放区：hole 覆盖全上部 */
.player-area {
  width: 960px;
  height: 200px;
  position: relative;
}

.video-hole {
  width: 960px;
  height: 200px;
}

/* 下部控制条 */
.controls {
  width: 960px;
  height: 66px;
  flex-direction: row;
  align-items: center;
  justify-content: space-between;
  padding-left: 16px;
  padding-right: 16px;
  background-color: #1a1a1a;
}

.ctrl-title {
  flex: 1;
  font-size: 20px;
  color: #ffffff;
  lines: 1;
}

.ctrl-btn {
  margin-left: 20px;
  font-size: 20px;
  color: #cccccc;
  padding: 6px 14px;
}
</style>

<script>
// 使用自研 gstplayer 模块（GStreamer + MPP 硬解）
var gstPlayerModule = null
try {
  gstPlayerModule = require('gstplayer').gstPlayer
} catch (e) {
  console.warn('[player] gstplayer module not available: ' + e.message)
}

import api from '../../utils/api.js'

export default {
  name: 'player',
  data() {
    return {
      bvid: '',
      cid: '',
      title: '',
      playUrl: '',
      ready: false,
      paused: false,
      error: '',
      mPlayer: null
    }
  },
  mounted() {
    var opt = this.$page.loadOptions || {}
    this.bvid = opt.bvid || ''
    this.cid = opt.cid || ''
    this.title = opt.title || '视频播放'
    console.warn('[player] mounted bvid=' + this.bvid + ' cid=' + this.cid)
    this.loadPlayUrl()
  },
  beforeDestroy() {
    if (this.mPlayer) {
      try { this.mPlayer.close() } catch (e) { }
      this.mPlayer = null
    }
  },
  methods: {
    // 获取播放地址 → 尝试直接播放
    async loadPlayUrl() {
      if (!this.bvid || !this.cid) {
        this.error = '缺少播放参数 ' + JSON.stringify(this.$page.loadOptions)
        return
      }
      try {
        var data = await api.getPlayUrl(this.bvid, this.cid, 64, 1)
        if (data && data.durl && data.durl.length > 0) {
          this.playUrl = data.durl[0].url
          console.warn('[player] playUrl: ' + this.playUrl.substring(0, 80) + '...')
          await this.tryPlay(this.playUrl)
        } else {
          this.error = '未获取到播放地址'
        }
      } catch (e) {
        this.error = '获取播放地址失败: ' + (e && e.message ? e.message : JSON.stringify(e))
        console.warn('[player] getPlayUrl error: ' + JSON.stringify(e))
      }
    },

    async tryPlay(url) {
      if (!gstPlayerModule) {
        this.error = 'gstplayer 不可用（需打包 native 模块）'
        return
      }

      this.mPlayer = new gstPlayerModule()
      var self = this

      // 方案：直接用网络 URL 播放
      try {
        await this.openAndStart(url)
        this.ready = true
        console.warn('[player] netUrl play OK')
        return
      } catch (e1) {
        console.warn('[player] netUrl failed: ' + e1.message)
        this.error = '播放失败: ' + e1.message
      }
    },

    async openAndStart(filename) {
      var self = this
      return new Promise((resolve, reject) => {
        self.mPlayer.open({
          filename: filename,
          decoder: 2,
          loop: 0,
          pos_x: 0,
          pos_y: 0,
          pos_w: 960,
          pos_h: 200,
          aoenable: 1,
        }).then(function () {
          self.mPlayer.start().then(resolve).catch(reject)
        }).catch(reject)
      })
    },

    pauseVideo() {
      if (!this.mPlayer) return
      this.paused = !this.paused
      if (this.paused) {
        try { this.mPlayer.pause() } catch (e) {}
      } else {
        try { this.mPlayer.start() } catch (e) {}
      }
      console.warn('[player] ' + (this.paused ? 'paused' : 'resumed'))
    },

    closePlayer() {
      if (this.mPlayer) {
        try { this.mPlayer.close() } catch (e) {}
        this.mPlayer = null
      }
      $falcon.closePage()
    }
  }
}
</script>