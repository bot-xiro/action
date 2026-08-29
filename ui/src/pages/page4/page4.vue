<template>
  <div class="page" :class="entering ? 'page-enter' : ''">
    <div class="header">
      <div class="back" @click="goBack">
        <text class="back-text">‹ 返回</text>
      </div>
      <text class="header-title">视频详情</text>
      <div class="homebtn" @click="goHome">
        <text class="home-text">⌂ 主页</text>
      </div>
    </div>

    <scroller class="content" scroll-direction="vertical" :show-scrollbar="true">
      <!-- 顶部信息: 即使详情接口失败, 也尽可能显示列表页带来的标题, 不再整页拦截 -->
      <div class="top" ref="topRef">
        <image v-if="detail && detail.pic" class="cover" :src="detail.pic" resize="cover"></image>
        <div class="info">
          <text class="title">{{ detail ? detail.title : fallbackTitle }}</text>
          <text class="author" @click="openUp">{{ detail ? (detail.author + ' › · ') : '' }}{{ detail ? detail.pubdateText : '' }}</text>
          <text v-if="detail" class="stat">播放 {{ detail.playText }} · 弹幕 {{ detail.danmakuText }} · {{ detail.duration }}</text>
          <text v-if="detail" class="stat">赞 {{ detail.likeText }} · 币 {{ detail.coinText }} · 藏 {{ detail.favText }} · 转 {{ detail.shareText }}</text>
        </div>
      </div>

      <text v-if="error !== ''" class="state-inline">{{ error }}</text>
      <!-- 加载状态放在顶部: 切换视频时立即给出反馈 -->
      <text v-if="loading" class="state-inline">加载中…</text>

      <!-- 简介 (放大) -->
      <div v-if="detail" class="section">
        <text class="sec-title">简介</text>
        <text class="desc">{{ detail.desc !== '' ? detail.desc : '暂无简介' }}</text>
      </div>

      <!-- 分 P / 合集切换 -->
      <div v-if="detail && detail.pages.length > 1" class="section">
        <text class="sec-title">分 P ({{ detail.pages.length }})</text>
        <scroller class="plist" scroll-direction="horizontal" :show-scrollbar="true">
          <text v-for="p in detail.pages" :key="p.page"
                :class="['pitem', currentPage === p.page ? 'pitem-active' : '']"
                @click="switchPage(p)">P{{ p.page }} {{ p.part }}</text>
        </scroller>
      </div>
      <div v-else-if="detail && detail.season && detail.season.episodes.length > 1" class="section">
        <text class="sec-title">合集 · {{ detail.season.title }}</text>
        <scroller class="plist" scroll-direction="horizontal" :show-scrollbar="true">
          <text v-for="e in detail.season.episodes" :key="e.bvid"
                :class="['pitem', e.bvid === detail.bvid ? 'pitem-active' : '']"
                @click="switchEpisode(e)">{{ e.title }}</text>
        </scroller>
      </div>

      <!-- 相关推荐 -->
      <div v-if="related.length > 0" class="section">
        <text class="sec-title">推荐</text>
        <div v-for="item in related" :key="item.bvid" class="ritem" @click="openVideo(item)">
          <image class="rcover" :src="item.pic" resize="cover" :lazy-load="true"></image>
          <div class="rmeta">
            <text class="rtitle">{{ item.title }}</text>
            <text class="rstat">{{ item.author }} · ▶{{ item.playText }} {{ item.duration }}</text>
          </div>
        </div>
      </div>
    </scroller>
  </div>
</template>

<script>
import { getVideoDetail, getRelatedVideos } from '../../services/bili.js'

export default {
  name: 'page',
  data() {
    return {
      bvid: '',
      currentPage: 1,
      fallbackTitle: '',
      loading: true,
      error: '',
      detail: null,
      related: [],
      generation: 0,
      entering: true   // 页面进入动画: 首次渲染后翻转为 false
    }
  },
  methods: {
    beginLoad(options) {
      options = options || this.$page.options || {}
      const bvid = options.bvid || ''
      if (!bvid) {
        this.error = '缺少视频参数'
        return
      }
      if (bvid === this.bvid && (this.detail || this.loading)) return
      this.bvid = bvid
      this.fallbackTitle = options.title || ''
      // 进入新视频视为第 1 P (page 参数可通过 options.page 指定)
      this.currentPage = parseInt(options.page || '1', 10) || 1
      this.detail = null
      this.related = []
      this.error = ''
      this.loading = true   // 立即显示「加载中…」, 同页跳转时旧内容立刻清空
      this.load()
      // 同页 navTo 停在原滚动位置 (推荐区) -> 复位到顶部
      this.scrollTop()
    },

    scrollTop() {
      const page = this.$page
      try {
        if (page && page.$dom && page.$dom.scrollToElement && this.$refs.topRef) {
          page.$dom.scrollToElement(this.$refs.topRef, { offset: 0 })
        }
      } catch (e) {}
    },

    onShow() {
      // 固件的自动 Page 只桥接 onShow/onHide/onUnload 等固定生命周期,
      // 同页 navTo 的 onNewOptions 只发到 Page 实例 -> 显式挂钩到实例方法
      if (this.$page && !this._newOptionsBound) {
        this._newOptionsBound = true
        const self = this
        this.$page.onNewOptions = function (options) { self.onNewOptions(options) }
      }
      this.beginLoad()
      // 进入动画: 等到首帧绘制完成后再翻转 entering, CSS transition 从右滑入
      if (this.entering) {
        const self2 = this
        try {
          setTimeout(function () { self2.entering = false }, 60)
        } catch (e) { self2.entering = false }
      }
    },

    // 同一页面被 navTo 重新打开 (详情页点相关推荐) 会走 onNewOptions 而不是 onShow
    onNewOptions(options) {
      console.log('[page] onNewOptions bvid=' + (options && options.bvid))
      this.bvid = ''  // 放开与 beginLoad 的去重门槛
      this.beginLoad(options)
    },

    async load() {
      const gen = ++this.generation
      this.loading = true
      this.error = ''
      try {
        const d = await getVideoDetail(this.bvid)
        if (gen !== this.generation) return
        this.detail = d
        // 分 P 详情: 若指定 page, 需要选中的分 P 标题覆盖展示
        if (d.pages.length > 1 && this.currentPage >= 1 && this.currentPage <= d.pages.length) {
          const p = d.pages[this.currentPage - 1]
          if (p && p.part) this.detail.title = d.title + '（' + p.part + '）'
        }
      } catch (err) {
        if (gen !== this.generation) return
        console.log('[bili] detail error: ' + (err && err.message ? err.message : err))
        this.error = err && err.message ? err.message : String(err)
      } finally {
        if (gen === this.generation) this.loading = false
      }
      // 推荐失败容忍, 与主详情并行
      try {
        const rel = await getRelatedVideos(this.bvid)
        if (gen !== this.generation) return
        this.related = rel
      } catch (e) {}
    },

    // 分 P: 同稿件内部切换, 不重新请求接口 (数据已在 pages 中)
    switchPage(p) {
      this.currentPage = p.page
      if (this.detail && p.part) this.detail.title = this.detail.title.replace(/（[^（]*）$/, '') + '（' + p.part + '）'
    },

    // 合集: 不同稿件, 重新拉详情
    switchEpisode(e) {
      if (e.bvid === this.bvid) return
      this.bvid = e.bvid
      this.currentPage = 1
      this.fallbackTitle = e.title
      this.detail = null
      this.related = []
      this.load()
    },

    // 推荐视频跳转到"下一份"详情页副本, 实现真正的页面叠加 (同名页只替换)
    openVideo(item) {
      $falcon.navTo('page5', { bvid: item.bvid, title: item.title })
    },

    openUp() {
      if (this.detail && this.detail.mid) {
        $falcon.navTo('up', { mid: String(this.detail.mid), name: this.detail.author })
      }
    },

    goBack() {
      this.$page.finish()
    },

    // 一键回主页: navTo 已存在的 index 页面 -> 框架把它暂时提到前台 (onNewOptions 会刷新刚发布的内容)
    goHome() {
      $falcon.navTo('index', {})
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
  height: 44px;
  display: flex;
  flex-direction: row;
  align-items: center;
  background-color: #1f1f1f;
}
.back {
  width: 120px;
  height: 34px;
  margin-left: 12px;
  border-radius: 17px;
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
.homebtn {
  width: 110px;
  height: 34px;
  margin-left: 560px;
  border-radius: 17px;
  background-color: #fb7299;
  justify-content: center;
  align-items: center;
}
.home-text {
  font-size: 22px;
  color: #ffffff;
}
.content {
  width: 960px;
  height: 222px;
  display: flex;
  flex-direction: column;
}
.top {
  width: 920px;
  margin-left: 20px;
  margin-top: 12px;
  display: flex;
  flex-direction: row;
}
.cover {
  width: 320px;
  height: 180px;
  border-radius: 12px;
  background-color: #2c2c2c;
}
.info {
  width: 580px;
  margin-left: 20px;
  display: flex;
  flex-direction: column;
}
.title {
  font-size: 24px;
  color: #ffffff;
  max-lines: 2;
  text-overflow: ellipsis;
  overflow: hidden;
}
.author {
  font-size: 20px;
  color: #fb7299;
  margin-top: 8px;
}
.stat {
  font-size: 18px;
  color: #888888;
  margin-top: 6px;
}
.state-inline {
  font-size: 20px;
  color: #e6a23c;
  margin-left: 20px;
  margin-top: 10px;
}
.section {
  width: 920px;
  margin-left: 20px;
  margin-top: 12px;
  display: flex;
  flex-direction: column;
}
.sec-title {
  font-size: 20px;
  color: #fb7299;
}
.desc {
  font-size: 20px;
  color: #cccccc;
  margin-top: 8px;
  line-height: 30px;
}
.plist {
  width: 920px;
  height: 52px;
  margin-top: 8px;
  display: flex;
  flex-direction: row;
}
.pitem {
  font-size: 20px;
  color: #ffffff;
  background-color: #2c2c2c;
  border-radius: 20px;
  padding-left: 16px;
  padding-right: 16px;
  padding-top: 8px;
  padding-bottom: 8px;
  margin-right: 10px;
  max-lines: 1;
  text-overflow: ellipsis;
  overflow: hidden;
}
.pitem-active {
  background-color: #fb7299;
}
.ritem {
  width: 920px;
  margin-top: 10px;
  display: flex;
  flex-direction: row;
  background-color: #1f1f1f;
  border-radius: 12px;
}
.rcover {
  width: 160px;
  height: 96px;
  border-top-left-radius: 12px;
  border-bottom-left-radius: 12px;
}
.rmeta {
  width: 740px;
  height: 96px;
  display: flex;
  flex-direction: column;
}
.rtitle {
  font-size: 20px;
  color: #ffffff;
  margin-left: 16px;
  margin-top: 8px;
  margin-right: 12px;
  max-lines: 2;
  text-overflow: ellipsis;
  overflow: hidden;
}
.rstat {
  font-size: 18px;
  color: #888888;
  margin-left: 16px;
  margin-top: 8px;
}
</style>
