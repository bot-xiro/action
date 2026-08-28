<template>
  <div class="page">
    <div class="header">
      <div class="back" @click="goBack">
        <text class="back-text">‹ 返回</text>
      </div>
      <text class="header-title">视频详情</text>
    </div>

    <text v-if="loading" class="state">加载中…</text>
    <text v-else-if="error !== ''" class="state">{{ error }}</text>

    <div v-else class="content">
      <image class="cover" :src="detail.pic" resize="cover"></image>
      <div class="info">
        <text class="title">{{ detail.title }}</text>
        <text class="author" @click="openUp">{{ detail.author }} › · {{ detail.pubdateText }} · {{ detail.bvid }}</text>
        <text class="stat">播放 {{ detail.playText }} · 弹幕 {{ detail.danmakuText }} · {{ detail.duration }}</text>
        <text class="stat">点赞 {{ detail.likeText }} · 投币 {{ detail.coinText }} · 收藏 {{ detail.favText }} · 分享 {{ detail.shareText }}</text>
        <scroller class="desc" scroll-direction="vertical" :show-scrollbar="true">
          <text class="desc-text">{{ detail.desc !== '' ? detail.desc : '暂无简介' }}</text>
        </scroller>
      </div>
    </div>
  </div>
</template>

<script>
import { getVideoDetail } from '../../services/bili.js'

export default {
  name: 'page',
  data() {
    return {
      bvid: '',
      fallbackTitle: '',
      loading: true,
      error: '',
      detail: null,
      generation: 0
    }
  },
  methods: {
    // 页面生命周期 (由 base-page.js 代理调用)
    onShow() {
      const options = this.$page.options || {}
      const bvid = options.bvid || ''
      if (!bvid) {
        this.loading = false
        this.error = '缺少视频参数'
        return
      }
      if (bvid === this.bvid && (this.detail || this.loading)) return // 避免 IME 等触发 onShow 时重复拉取
      this.bvid = bvid
      this.fallbackTitle = options.title || ''
      this.load()
    },

    async load() {
      const gen = ++this.generation
      this.loading = true
      this.error = ''
      this.detail = null
      try {
        const d = await getVideoDetail(this.bvid)
        if (gen !== this.generation) return
        this.detail = d
      } catch (err) {
        if (gen !== this.generation) return
        console.log('[bili] detail error: ' + (err && err.message ? err.message : err))
        // 失败时仍然展示从列表页带过来的标题, 用户不至于面对空屏
        this.error = (err && err.message ? err.message : String(err))
          + (this.fallbackTitle !== '' ? ('　|　' + this.fallbackTitle) : '')
      } finally {
        if (gen === this.generation) this.loading = false
      }
    },

    goBack() {
      this.$page.finish()
    },

    openUp() {
      if (this.detail && this.detail.mid) {
        $falcon.navTo('up', { mid: String(this.detail.mid), name: this.detail.author })
      }
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
  font-size: 24px;
  color: #999999;
  margin-top: 70px;
  text-align: center;
}
.content {
  width: 960px;
  height: 218px;
  display: flex;
  flex-direction: row;
}
.cover {
  width: 320px;
  height: 180px;
  margin-left: 20px;
  margin-top: 18px;
  border-radius: 12px;
  background-color: #2c2c2c;
}
.info {
  width: 580px;
  height: 218px;
  margin-left: 20px;
  display: flex;
  flex-direction: column;
}
.title {
  font-size: 24px;
  color: #ffffff;
  margin-top: 14px;
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
.desc {
  width: 580px;
  height: 60px;
  margin-top: 8px;
}
.desc-text {
  font-size: 18px;
  color: #aaaaaa;
}
</style>
