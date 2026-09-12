<template>
  <div class="page">
    <div class="tabs">
      <div v-for="t in tabs" :key="t.key"
           :class="['tab', activeTab === t.key ? 'tab-active' : '']"
           @click="switchTab(t.key)">
        <text :class="['tab-text', activeTab === t.key ? 'tab-text-active' : '']">{{ t.label }}</text>
      </div>
    </div>

    <!-- 搜索 -->
    <div v-if="activeTab === 'search'" class="tabbody">
      <div class="search-bar">
        <div class="search-input" @click="openKeyboard">
          <text class="search-text">{{ keyword ? keyword : placeholder }}</text>
        </div>
        <div class="search-btn" @click="openKeyboard">
          <text class="search-btn-text">搜索</text>
        </div>
      </div>
      <text v-if="status !== ''" class="status">{{ status }}</text>
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

    <!-- 推荐 -->
    <div v-else-if="activeTab === 'recommend'" class="tabbody">
      <text v-if="recStatus !== ''" class="status">{{ recStatus }}</text>
      <scroller class="results-full" scroll-direction="vertical" :show-scrollbar="true">
        <div v-for="item in recResults" :key="item.bvid" class="item" @click="openVideo(item)">
          <image class="cover" :src="item.pic" resize="cover" :lazy-load="true"></image>
          <div class="meta">
            <text class="title">{{ item.title }}</text>
            <text class="up">{{ item.author }}</text>
            <text class="stat">▶{{ item.playText }}  {{ item.duration }}</text>
          </div>
        </div>
        <text v-if="recLoaded && recResults.length === 0 && !recLoading" class="empty">暂无推荐内容</text>
      </scroller>
    </div>

    <!-- 动态 -->
    <div v-else-if="activeTab === 'dynamic'" class="tabbody">
      <text v-if="dynStatus !== ''" class="status">{{ dynStatus }}</text>
      <div v-if="dynStatus !== '' && dynStatus.indexOf('未登录') >= 0" class="login-cta" @click="openLogin">
        <text class="login-cta-text">去登录</text>
      </div>
      <scroller v-if="dynStatus === '' || dynItems.length > 0" class="results-full" scroll-direction="vertical" :show-scrollbar="true">
        <div v-for="item in dynItems" :key="item.bvid" class="item" @click="openVideo(item)">
          <image class="cover" :src="item.pic" resize="cover" :lazy-load="true"></image>
          <div class="meta">
            <text class="title">{{ item.title }}</text>
            <text class="up">{{ item.author }} · {{ item.pubText }}</text>
            <text class="stat">▶{{ item.playText }}  {{ item.duration }}</text>
          </div>
        </div>
        <text v-if="dynHasMore" class="loadmore" @click="loadDynamic(dynOffset)">加载更多…</text>
        <text v-if="dynLoaded && dynItems.length === 0" class="empty">关注的 UP 主暂无视频动态</text>
      </scroller>
    </div>

    <!-- 我的 -->
    <div v-else-if="activeTab === 'mine'" class="tabbody center">
      <image v-if="myInfo.isLogin && myInfo.face" class="myface" :src="myInfo.face" resize="cover"></image>
      <text v-if="myInfo.isLogin" class="ph-title">{{ myInfo.uname }}</text>
      <text v-if="myInfo.isLogin" class="ph-desc">Lv{{ myInfo.level }} · 硬币 {{ myInfo.coin }} · B币 {{ myInfo.money }}</text>
      <div v-if="!myInfo.isLogin && myLoaded" class="login-cta" @click="openLogin">
        <text class="login-cta-text">扫码登录 / Cookie 导入</text>
      </div>
      <div v-if="myInfo.isLogin" class="login-cta" @click="logout">
        <text class="login-cta-text">退出登录</text>
      </div>
      <text v-if="myStatus !== ''" class="ph-desc2">{{ myStatus }}</text>
      <text class="ph-desc2">bilibilipan v{{ appVersion }}</text>
      <text class="ph-desc2">appid {{ appid }} · 词典笔 mini-app</text>
    </div>
  </div>
</template>

<script>
import { createIME } from '../../services/ime.js'
import { searchVideos, getPopular, getDynamicFeed, getMyInfo } from '../../services/bili.js'
import { afterPaint } from '../../base-page.js'
import { clearLogin, hasCookie } from '../../services/auth.js'
import pm from 'pm'

export default {
  name: 'index',
  data() {
    return {
      tabs: [
        { key: 'recommend', label: '推荐' },
        { key: 'search', label: '搜索' },
        { key: 'dynamic', label: '动态' },
        { key: 'mine', label: '我的' }
      ],
      activeTab: 'recommend',
      // 搜索
      keyword: '',
      placeholder: '点击输入搜索内容',
      status: '',
      results: [],
      searched: false,
      loading: false,
      generation: 0,
      // 推荐
      recResults: [],
      recStatus: '',
      recLoaded: false,
      recLoading: false,
      recGeneration: 0,
      // 动态
      dynItems: [],
      dynStatus: '',
      dynOffset: '',
      dynHasMore: false,
      dynLoaded: false,
      dynLoading: false,
      dynGeneration: 0,
      // 我的
      myInfo: { isLogin: false, uname: '', face: '', mid: 0, level: 0, coin: 0, money: 0 },
      myStatus: '',
      myLoaded: false,
      myGeneration: 0,
      // 我的 (版本号运行时从包管理器读取, 不硬编码)
      appVersion: '',
      appid: '8001812345678901'
    }
  },
  mounted() {
    this.ime = createIME()
    // 版本号: 从包管理器读当前安装包信息 (haasui-docs jsapi/system/falcon/pm)
    try {
      const info = pm.getPackageInfo(this.appid)
      if (info && info.version) this.appVersion = info.version
    } catch (e) {
      console.log('[index] getPackageInfo failed: ' + (e && e.message ? e.message : e))
    }
    this.loadRecommend()
  },
  methods: {
    switchTab(key) {
      this.activeTab = key
      if (key === 'recommend' && !this.recLoaded && !this.recLoading) {
        this.loadRecommend()
      }
      if (key === 'dynamic' && !this.dynLoaded && !this.dynLoading) {
        this.loadDynamic('')
      }
      if (key === 'mine' && !this.myLoaded && !this.myLoading) {
        this.loadMine()
      }
    },

    openLogin() {
      $falcon.navTo('login', {})
    },

    // 动态视频流 (需登录; 未登录给出去登录入口)
    async loadDynamic(offset) {
      const gen = ++this.dynGeneration
      if (this.dynLoading) return
      this.dynLoading = true
      this.dynStatus = '加载中…'
      afterPaint(async () => {
        try {
          const r = await getDynamicFeed(offset)
          if (gen !== this.dynGeneration) return
          if (offset) {
            for (let i = 0; i < r.items.length; i++) this.dynItems.push(r.items[i])
          } else {
            this.dynItems = r.items
          }
          this.dynOffset = r.offset
          this.dynHasMore = r.hasMore
          this.dynLoaded = true
          this.dynStatus = r.items.length === 0 && !offset ? '暂无动态, 去关注一些 UP 主吧' : ''
        } catch (err) {
          if (gen !== this.dynGeneration) return
          const msg = err && err.message ? err.message : String(err)
          console.log('[bili] dynamic error: ' + msg)
          if (msg.indexOf('未登录') >= 0) {
            this.dynStatus = '未登录, 登录后可查看关注 UP 主的动态'
          } else {
            this.dynStatus = msg
          }
          if (!offset) this.dynItems = []
          this.dynLoaded = true
        } finally {
          if (gen === this.dynGeneration) this.dynLoading = false
        }
      })
    },

    // 我的: 登录态 + 账号信息
    async loadMine() {
      const gen = ++this.myGeneration
      this.myLoading = true
      this.myStatus = ''
      afterPaint(async () => {
        try {
          const info = await getMyInfo()
          if (gen !== this.myGeneration) return
          this.myInfo = info
          if (!info.isLogin) this.myStatus = '未登录'
        } catch (err) {
          if (gen !== this.myGeneration) return
          const msg = err && err.message ? err.message : String(err)
          this.myStatus = msg
          this.myInfo.isLogin = false
        } finally {
          if (gen === this.myGeneration) {
            this.myLoading = false
            this.myLoaded = true
          }
        }
      })
    },

    logout() {
      clearLogin()
      this.myInfo = { isLogin: false, uname: '', face: '', mid: 0, level: 0, coin: 0, money: 0 }
      this.myStatus = '已退出登录'
      // 动态缓存态作废, 下次进入重新按登录态加载
      this.dynLoaded = false
      this.dynItems = []
    },

    async loadRecommend() {
      const gen = ++this.recGeneration
      this.recLoading = true
      this.recStatus = '加载中…'
      // 先让首帧画出加载态再发请求: bilinet.httpGet 同步阻塞 JS 线程
      afterPaint(async () => {
        try {
          const videos = await getPopular(1)
          if (gen !== this.recGeneration) return
          this.recResults = videos
          this.recLoaded = true
          this.recStatus = ''
        } catch (err) {
          if (gen !== this.recGeneration) return
          console.log('[bili] recommend error: ' + (err && err.message ? err.message : err))
          this.recStatus = err && err.message ? err.message : String(err)
          this.recResults = []
          this.recLoaded = true
        } finally {
          if (gen === this.recGeneration) this.recLoading = false
        }
      })
    },

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
      // 先让首帧画出加载态再发请求: bilinet.httpGet 同步阻塞 JS 线程
      afterPaint(async () => {
        try {
          const videos = await searchVideos(keyword.trim(), 1)
          if (gen !== this.generation) return
          this.results = videos
          this.searched = true
          this.status = ''
        } catch (err) {
          if (gen !== this.generation) return
          console.log('[bili] search error: ' + (err && err.message ? err.message : err))
          this.status = err && err.message ? err.message : String(err)
          this.results = []
          this.searched = true
        } finally {
          if (gen === this.generation) this.loading = false
        }
      })
    },

    openVideo(item) {
      console.log('open video', item.bvid, item.title)
      $falcon.navTo('page', { bvid: item.bvid, title: item.title })
    },

    // 页面生命周期 (由 base-page.js 代理调用)
    onShow() {
      // 从登录页返回: 登录态可能已变化 (有 Cookie 但界面未登录 -> 刷新)
      try {
        if (hasCookie() && !this.myInfo.isLogin) {
          this.myLoaded = false
          this.dynLoaded = false
        }
      } catch (e) {}
    },

    onHide() {
      // 输入法弹出可能触发 onHide, 不能在这里销毁 IME 会话
    },
    onUnload() {
      this.generation++
      this.recGeneration++
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
.tabs {
  width: 960px;
  height: 44px;
  display: flex;
  flex-direction: row;
  background-color: #1f1f1f;
}
.tab {
  width: 240px;
  height: 44px;
  justify-content: center;
  align-items: center;
}
.tab-active {
  background-color: #2c2c2c;
  border-bottom-width: 3px;
  border-bottom-color: #fb7299;
}
.tab-text {
  font-size: 24px;
  color: #999999;
}
.tab-text-active {
  color: #ffffff;
}
.tabbody {
  width: 960px;
  height: 222px;
  display: flex;
  flex-direction: column;
}
.center {
  align-items: center;
}
.search-bar {
  width: 960px;
  height: 56px;
  display: flex;
  flex-direction: row;
  align-items: center;
  background-color: #1f1f1f;
}
.search-input {
  width: 760px;
  height: 44px;
  margin-left: 20px;
  background-color: #2c2c2c;
  border-radius: 22px;
  justify-content: center;
}
.search-text {
  font-size: 24px;
  color: #ffffff;
  margin-left: 24px;
}
.search-btn {
  width: 120px;
  height: 44px;
  margin-left: 20px;
  background-color: #fb7299;
  border-radius: 22px;
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
  margin-top: 4px;
  height: 30px;
}
.results {
  width: 960px;
  height: 130px;
}
.results-full {
  width: 960px;
  height: 220px;
}
.item {
  width: 920px;
  margin-left: 20px;
  margin-top: 10px;
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
.ph-title {
  font-size: 28px;
  color: #ffffff;
  margin-top: 50px;
}
.ph-desc {
  font-size: 22px;
  color: #999999;
  margin-top: 16px;
}
.ph-desc2 {
  font-size: 20px;
  color: #666666;
  margin-top: 10px;
}

.login-cta {
  margin-top: 18px;
  padding-left: 28px;
  padding-right: 28px;
  height: 46px;
  border-radius: 23px;
  background-color: #fb7299;
  justify-content: center;
  align-items: center;
}
.login-cta-text {
  font-size: 22px;
  color: #ffffff;
}
.myface {
  width: 72px;
  height: 72px;
  border-radius: 36px;
  margin-bottom: 10px;
}
.loadmore {
  font-size: 20px;
  color: #fb7299;
  text-align: center;
  margin-top: 12px;
  margin-bottom: 12px;
}
</style>
