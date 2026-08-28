<template>
  <div class="page">
    <div class="search-bar">
      <div class="search-input" @click="openKeyboard">
        <text class="search-text">{{ keyword ? keyword : placeholder }}</text>
      </div>
      <div class="search-btn" @click="openKeyboard">
        <text class="search-btn-text">搜索</text>
      </div>
    </div>

    <text v-if="status !== ''" class="status">{{ status }}</text>
    <text v-if="debugLog !== ''" class="debug">{{ debugLog }}</text>

    <scroller class="results" scroll-direction="vertical" :show-scrollbar="true">
      <div v-for="item in results" :key="item.bvid" class="item" @click="openVideo(item)">
        <image class="cover" :src="item.pic" resize="cover" :lazy-load="true"></image>
        <div class="meta">
          <text class="title">{{ item.title }}</text>
          <text class="up">{{ item.author }}</text>
          <text class="stat">▶{{ item.playText }}  {{ item.duration }}</text>
        </div>
      </div>
      <text v-if="searched && results.length === 0 && !loading" class="empty">没有找到相关视频</text>
    </scroller>
  </div>
</template>

<script>
import { createIME } from '../../services/ime.js'
import { searchVideos } from '../../services/bili.js'

export default {
  name: 'index',
  data() {
    return {
      keyword: '',
      placeholder: '点击输入搜索内容',
      status: '',
      results: [],
      debugLog: '',
      searched: false,
      loading: false,
      generation: 0
    }
  },
  mounted() {
    this.ime = createIME()
    this.ime.onDebug((msg) => {
      this.debugLog = msg
    })
  },
  methods: {
    async openKeyboard() {
      try {
        const text = await this.ime.open({
          text: this.keyword,
          placeholder: '输入视频关键词',
          maxlength: 64
        })
        if (text === null) return // 用户取消
        this.keyword = text
        if (text.trim() === '') {
          this.status = '请输入关键词'
          this.searched = false
          return
        }
        this.doSearch(text)
      } catch (err) {
        console.log('IME error', err)
        this.status = '输入法打开失败: ' + err
      }
    },

    async doSearch(keyword) {
      const gen = ++this.generation
      this.loading = true
      this.status = '搜索中…'
      try {
        const videos = await searchVideos(keyword.trim(), 1)
        if (gen !== this.generation) return
        this.results = videos
        this.searched = true
        this.status = videos.length > 0 ? '' : ''
      } catch (err) {
        if (gen !== this.generation) return
        this.status = err && err.message ? err.message : String(err)
        this.results = []
        this.searched = true
      } finally {
        if (gen === this.generation) this.loading = false
      }
    },

    openVideo(item) {
      // 后续: 跳转到播放页
      console.log('open video', item.bvid, item.title)
      $falcon.navTo('page', { bvid: item.bvid, title: item.title })
    },

    // 页面生命周期 (由 base-page.js 代理调用)
    onHide() {
      // 输入法弹出可能触发 onHide, 不能在这里销毁 IME 会话
    },
    onUnload() {
      if (this.ime) {
        this.ime.destroy()
        this.ime = null
      }
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
.search-bar {
  width: 960px;
  height: 64px;
  display: flex;
  flex-direction: row;
  align-items: center;
  background-color: #1f1f1f;
}
.search-input {
  width: 760px;
  height: 48px;
  margin-left: 20px;
  background-color: #2c2c2c;
  border-radius: 24px;
  justify-content: center;
}
.search-text {
  font-size: 24px;
  color: #ffffff;
  margin-left: 24px;
}
.search-btn {
  width: 120px;
  height: 48px;
  margin-left: 20px;
  background-color: #fb7299;
  border-radius: 24px;
  justify-content: center;
  align-items: center;
}
.search-btn-text {
  font-size: 24px;
  color: #ffffff;
}
.status {
  font-size: 22px;
  color: #999999;
  margin-left: 24px;
  margin-top: 8px;
  height: 32px;
}
.debug {
  font-size: 18px;
  color: #666666;
  margin-left: 24px;
}
.results {
  width: 960px;
  height: 160px;
}
.item {
  width: 920px;
  margin-left: 20px;
  margin-top: 12px;
  display: flex;
  flex-direction: row;
  background-color: #1f1f1f;
  border-radius: 12px;
}
.cover {
  width: 180px;
  height: 112px;
  border-top-left-radius: 12px;
  border-bottom-left-radius: 12px;
}
.meta {
  width: 720px;
  height: 112px;
  display: flex;
  flex-direction: column;
}
.title {
  font-size: 22px;
  color: #ffffff;
  margin-left: 16px;
  margin-top: 8px;
  margin-right: 16px;
  max-lines: 2;
  text-overflow: ellipsis;
  overflow: hidden;
}
.up {
  font-size: 20px;
  color: #fb7299;
  margin-left: 16px;
  margin-top: 4px;
}
.stat {
  font-size: 20px;
  color: #888888;
  margin-left: 16px;
  margin-top: 4px;
  margin-bottom: 8px;
}
.empty {
  font-size: 24px;
  color: #666666;
  text-align: center;
  margin-top: 40px;
}
</style>
