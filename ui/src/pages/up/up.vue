<template>
  <div class="page" :class="entering ? 'page-enter' : ''">
    <div class="header">
      <div class="back" @click="goBack">
        <text class="back-text">‹ 返回</text>
      </div>
      <text class="header-title">UP主主页</text>
    </div>

    <!-- UP 信息栏内嵌为列表首项: 往上滑自然滚出, 滑回顶部自然恢复, 无事件依赖 -->
    <scroller class="results" scroll-direction="vertical" :show-scrollbar="true"
              :loadmoreoffset="100" @loadmore="loadMoreVideos" @scroll="onListScroll"
              @touchstart="onTouchStart" @touchmove="onTouchMove" @touchend="onTouchEnd">
      <text v-if="upStatus !== ''" class="state">{{ upStatus }}</text>

      <div class="info-row" v-if="info">
        <image class="face" :src="info.face" resize="cover"></image>
        <div class="info-col">
          <text class="name">{{ info.name }}</text>
          <text class="meta">{{ info.levelText }} · 粉丝 {{ fansText }}</text>
          <text class="sign">{{ info.sign !== '' ? info.sign : '这个人很神秘，什么都没有写' }}</text>
        </div>
      </div>

      <div v-for="item in videos" :key="item.bvid" class="item" @click="openVideo(item)">
        <image class="cover" :src="item.pic" resize="cover" :lazy-load="true"></image>
        <div class="meta2">
          <text class="title">{{ item.title }}</text>
          <text class="stat">▶{{ item.playText }}  {{ item.duration }}</text>
        </div>
      </div>
      <text v-if="videos.length > 0 && videosHasMore" class="empty" @click="loadMoreVideos">上滑加载更多…</text>
      <text v-if="videosStatus !== ''" class="empty">{{ videosStatus }}</text>
    </scroller>
  </div>
</template>

<script>
import { getUpInfo, getUpFans, getUpVideos } from '../../services/bili.js'
import { afterPaint } from '../../base-page.js'

// 进入动画 340ms 画完再发首条请求 (同步 http 阻塞 JS 会卡进入动画)
var LOAD_DELAY_MS = 340
var PULL_DY = 55

export default {
  name: 'up',
  data() {
    return {
      mid: 0,
      name: '',
      info: null,
      fansText: '',
      videos: [],
      videosPage: 1,
      videosHasMore: false,
      upStatus: '加载中…',
      videosStatus: '',
      generation: 0,
      entering: true   // 页面进入动画
    }
  },
  methods: {
    beginLoad(options) {
      options = options || this.$page.options || {}
      const mid = parseInt(options.mid || '0', 10)
      if (!mid) {
        this.upStatus = '缺少 UP 主参数'
        return
      }
      if (mid === this.mid && this.info) return
      this.mid = mid
      this.name = options.name || ''
      this.info = null
      this.videos = []
      this.videosPage = 1
      this.videosHasMore = false
      this.load()
    },

    onShow() {
      // 同页 navTo 的 onNewOptions 只发到 Page 实例, 需显式挂钩
      if (this.$page && !this._newOptionsBound) {
        this._newOptionsBound = true
        const self = this
        this.$page.onNewOptions = function (options) { self.onNewOptions(options) }
      }
      this.beginLoad()
      // 进入动画: 首帧后翻转折射滑入; timer 走 BasePage 托管, 页面卸载自动清理
      if (this.entering) {
        const self3 = this
        try {
          const p = this.$page
          if (p && p.setTimeout) p.setTimeout(function () { self3.entering = false }, 60)
          else setTimeout(function () { self3.entering = false }, 60)
        } catch (e) { self3.entering = false }
      }
    },

    // 同一页面被 navTo 重新打开时走 onNewOptions, 不会触发 onShow
    onNewOptions(options) {
      this.mid = 0  // 放开 beginLoad 的去重门槛
      this.beginLoad(options)
    },

    async load() {
      const gen = ++this.generation
      this.info = null
      this.videos = []
      this.upStatus = '加载中…'

      // 先进页面画完进入动画再发请求: bilinet.httpGet 同步阻塞 JS 线程
      afterPaint(async () => {
        // 基本信息 (失败直接报整体错误)
        try {
          const info = await getUpInfo(this.mid)
          if (gen !== this.generation) return
          this.info = info
          this.upStatus = ''
          // 粉丝数异步补充, 失败静默
          try {
            const fans = await getUpFans(this.mid)
            if (gen !== this.generation) return
            this.fansText = fans || ''
          } catch (e) {}
        } catch (err) {
          if (gen !== this.generation) return
          console.log('[bili] up info error: ' + (err && err.message ? err.message : err))
          this.upStatus = err && err.message ? err.message : String(err)
        }

        // 视频列表独立加载, 互不影响
        this.videosStatus = '加载视频…'
        try {
          const videos = await getUpVideos(this.mid, 1)
          if (gen !== this.generation) return
          this.videos = videos
          this.videosHasMore = videos.length >= 20
          this.videosStatus = videos.length === 0 ? 'TA 还没有投稿视频' : ''
        } catch (err) {
          if (gen !== this.generation) return
          console.log('[bili] up videos error: ' + (err && err.message ? err.message : err))
          this.videosStatus = err && err.message ? err.message : String(err)
        }
      }, LOAD_DELAY_MS)
    },

    // 投稿列表无限滑动
    loadMoreVideos() {
      if (!this.videosHasMore || this.videos.length === 0) return
      const gen = this.generation
      this.videosPage++
      getUpVideos(this.mid, this.videosPage).then((videos) => {
        if (gen !== this.generation) return
        const seen = {}
        for (let i = 0; i < this.videos.length; i++) seen[this.videos[i].bvid] = true
        for (let i = 0; i < videos.length; i++) {
          if (!seen[videos[i].bvid]) this.videos.push(videos[i])
        }
        this.videosHasMore = videos.length >= 20
      }).catch(() => {
        if (gen !== this.generation) return
        this.videosHasMore = false
      })
    },

    // ---------- 下拉刷新 (与 index 同款 touch 方案) ----------
    touchXY(e) {
      try {
        const t = (e && e.changedTouches && e.changedTouches[0]) ||
          (e && e.touches && e.touches[0]) || e
        if (t) {
          if (typeof t.pageY === 'number') return t.pageY
          if (typeof t.clientY === 'number') return t.clientY
          if (typeof t.y === 'number') return t.y
        }
      } catch (err) {}
      return 0
    },
    onListScroll(e) {
      try {
        const co = e && e.contentOffset
        this._scrollY = co && typeof co.y === 'number' ? co.y : (this._scrollY || 0)
      } catch (err) {}
    },
    onTouchStart(e) {
      this._touchY0 = this.touchXY(e)
      this._pullArmed = false
      this._pullOk = (this._scrollY || 0) <= 2
    },
    onTouchMove(e) {
      if (!this._pullOk) return
      if ((this._scrollY || 0) > 2) { this._pullOk = false; return }
      if (this.touchXY(e) - this._touchY0 > PULL_DY) this._pullArmed = true
    },
    onTouchEnd() {
      if (this._pullArmed && this._pullOk && (this._scrollY || 0) <= 2) {
        this._pullArmed = false
        this.videosPage = 1
        this.videosHasMore = false
        this.mid = 0
        this.beginLoad()
        return
      }
      this._pullArmed = false
    },

    openVideo(item) {
      $falcon.navTo('page', { bvid: item.bvid, title: item.title })
    },

    goBack() {
      this.$page.finish()
    },

    onUnload() {
      this.generation++
    }
  }
}
</script>

<style scoped>
.page {
  width: 960px;
  height: 266px;
  background-color: #141414;
  display: flex;
  flex-direction: column;
  transition-property: transform;
  transition-duration: 260ms;
  transition-timing-function: ease-out;
}
.page-enter {
  transform: translateX(960px);
}
.header {
  width: 960px;
  height: 48px;
  display: flex;
  flex-direction: row;
  align-items: center;
  background-color: #1f1f1f;
}
.back {
  width: 132px;
  height: 40px;
  margin-left: 12px;
  border-radius: 20px;
  background-color: #2c2c2c;
  justify-content: center;
  align-items: center;
}
.back-text {
  font-size: 22px;
  color: #ffffff;
}
.header-title {
  font-size: 24px;
  color: #ffffff;
  margin-left: 24px;
}
.state {
  font-size: 22px;
  color: #999999;
  margin-left: 24px;
  margin-top: 8px;
}
.info-row {
  width: 960px;
  height: 96px;
  display: flex;
  flex-direction: row;
  align-items: center;
}
.face {
  width: 72px;
  height: 72px;
  margin-left: 20px;
  border-radius: 36px;
  background-color: #2c2c2c;
}
.info-col {
  width: 840px;
  height: 96px;
  margin-left: 16px;
  display: flex;
  flex-direction: column;
}
.name {
  font-size: 24px;
  color: #ffffff;
}
.meta {
  font-size: 18px;
  color: #fb7299;
  margin-top: 4px;
}
.sign {
  font-size: 18px;
  color: #888888;
  margin-top: 4px;
  /* Falcon 不支持 max-lines (0.8.7 教训), 限行用 lines: N */
  lines: 1;
  text-overflow: ellipsis;
  overflow: hidden;
}
.results {
  width: 960px;
  height: 218px;
}
.item {
  width: 920px;
  margin-left: 20px;
  margin-top: 8px;
  display: flex;
  flex-direction: row;
  background-color: #1f1f1f;
  border-radius: 12px;
}
.cover {
  width: 160px;
  height: 96px;
  border-top-left-radius: 12px;
  border-bottom-left-radius: 12px;
}
.meta2 {
  width: 740px;
  height: 96px;
  display: flex;
  flex-direction: column;
}
.title {
  font-size: 20px;
  color: #ffffff;
  margin-left: 16px;
  margin-top: 8px;
  margin-right: 12px;
  lines: 2;
  text-overflow: ellipsis;
  overflow: hidden;
}
.stat {
  font-size: 18px;
  color: #888888;
  margin-left: 16px;
  margin-top: 8px;
}
.empty {
  font-size: 22px;
  color: #666666;
  text-align: center;
  margin-top: 16px;
}
</style>
