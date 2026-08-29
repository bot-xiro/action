<template>
  <div class="page">
    <div class="header">
      <div class="back" @click="goBack">
        <text class="back-text">‹ 返回</text>
      </div>
      <text class="header-title">UP主主页</text>
    </div>

    <text v-if="upStatus !== ''" class="state">{{ upStatus }}</text>

    <div class="info-row" v-if="info && !infoCollapsed">
      <image class="face" :src="info.face" resize="cover"></image>
      <div class="info-col">
        <text class="name">{{ info.name }}</text>
        <text class="meta">{{ info.levelText }} · 粉丝 {{ fansText }}</text>
        <text class="sign">{{ info.sign !== '' ? info.sign : '这个人很神秘，什么都没有写' }}</text>
      </div>
    </div>

    <scroller class="results" :style="scrollerStyle" scroll-direction="vertical" :show-scrollbar="true"
              @scrollend="onScrollEnd">
      <div v-for="item in videos" :key="item.bvid" class="item" @click="openVideo(item)">
        <image class="cover" :src="item.pic" resize="cover" :lazy-load="true"></image>
        <div class="meta2">
          <text class="title">{{ item.title }}</text>
          <text class="stat">▶{{ item.playText }}  {{ item.duration }}</text>
        </div>
      </div>
      <text v-if="videosStatus !== ''" class="empty">{{ videosStatus }}</text>
    </scroller>
  </div>
</template>

<script>
import { getUpInfo, getUpFans, getUpVideos } from '../../services/bili.js'

export default {
  name: 'up',
  data() {
    return {
      mid: 0,
      name: '',
      info: null,
      fansText: '',
      videos: [],
      upStatus: '加载中…',
      videosStatus: '',
      generation: 0,
      // 往上滑时隐藏 UP 信息栏
      infoCollapsed: false
    }
  },
  computed: {
    // 页面 266px 固定: header 48 + 状态行(出现时约30) + info 96, 其余给列表
    scrollerStyle() {
      let h = 266 - 48
      if (this.info && !this.infoCollapsed) h -= 96
      if (this.upStatus !== '') h -= 30
      return 'width: 960px; height: ' + h + 'px'
    }
  },
  methods: {
    onShow() {
      const options = this.$page.options || {}
      const mid = parseInt(options.mid || '0', 10)
      if (!mid) {
        this.upStatus = '缺少 UP 主参数'
        return
      }
      if (mid === this.mid && this.info) return
      this.mid = mid
      this.name = options.name || ''
      this.load()
    },

    async load() {
      const gen = ++this.generation
      this.info = null
      this.videos = []
      this.upStatus = '加载中…'

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
        this.videosStatus = videos.length === 0 ? 'TA 还没有投稿视频' : ''
      } catch (err) {
        if (gen !== this.generation) return
        console.log('[bili] up videos error: ' + (err && err.message ? err.message : err))
        this.videosStatus = err && err.message ? err.message : String(err)
      }
    },

    // 往上滑 (contentOffset.y 增大) 隐藏 UP 信息栏; 滑回顶部附近恢复
    // 用 scrollend 而非 scroll: 拖动中切换高度会让 scroller 布局重排, 偏移归零造成闪烁
    onScrollEnd(e) {
      const y = (e && e.contentOffset && typeof e.contentOffset.y === 'number')
        ? e.contentOffset.y : 0
      if (!this.infoCollapsed && y > 80) {
        this.infoCollapsed = true
      } else if (this.infoCollapsed && y <= 40) {
        this.infoCollapsed = false
      }
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
  width: 120px;
  height: 36px;
  margin-left: 12px;
  border-radius: 18px;
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
  max-lines: 1;
  text-overflow: ellipsis;
  overflow: hidden;
}
.results {
  width: 960px;
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
  max-lines: 2;
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
