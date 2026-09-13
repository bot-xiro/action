<template>
  <div class="page">
    <div class="tabs">
      <div v-for="t in tabs" :key="t.key"
           :class="['tab', activeTab === t.key ? 'tab-active' : '']"
           @click="switchTab(t.key)">
        <text :class="['tab-text', activeTab === t.key ? 'tab-text-active' : '']">{{ t.label }}</text>
      </div>
    </div>

    <!-- 推荐 (真·主页推荐流 rcmd, 无限滑动) -->
    <div v-if="activeTab === 'recommend'" class="tabbody">
      <text v-if="recStatus !== ''" class="status">{{ recStatus }}</text>
      <scroller class="list" scroll-direction="vertical" :show-scrollbar="true"
                :loadmoreoffset="100" @loadmore="loadMoreRecommend"
                @scroll="onListScroll" @touchstart="onListTouchStart" @touchmove="onListTouchMove" @touchend="onListTouchEnd">
        <div v-for="item in recResults" :key="item.bvid" class="item" @click="openVideo(item)">
          <image class="cover" :src="item.pic" resize="cover" :lazy-load="true"></image>
          <div class="meta">
            <text class="title">{{ item.title }}</text>
            <text class="up">{{ item.author }}</text>
            <text class="stat">▶{{ item.playText }}  {{ item.duration }}</text>
          </div>
        </div>
        <text v-if="recLoaded && recResults.length === 0 && !recLoading" class="empty">暂无推荐内容</text>
        <text v-if="recHasMore" class="loadmore" @click="loadMoreRecommend">上滑加载更多…</text>
      </scroller>
    </div>

    <!-- 热门 (x/web-interface/popular, 无限滑动) -->
    <div v-else-if="activeTab === 'hot'" class="tabbody">
      <text v-if="hotStatus !== ''" class="status">{{ hotStatus }}</text>
      <scroller class="list" scroll-direction="vertical" :show-scrollbar="true"
                :loadmoreoffset="100" @loadmore="loadMoreHot"
                @scroll="onListScroll" @touchstart="onListTouchStart" @touchmove="onListTouchMove" @touchend="onListTouchEnd">
        <div v-for="item in hotResults" :key="item.bvid" class="item" @click="openVideo(item)">
          <image class="cover" :src="item.pic" resize="cover" :lazy-load="true"></image>
          <div class="meta">
            <text class="title">{{ item.title }}</text>
            <text class="up">{{ item.author }}</text>
            <text class="stat">▶{{ item.playText }}  {{ item.duration }}</text>
          </div>
        </div>
        <text v-if="hotLoaded && hotResults.length === 0 && !hotLoading" class="empty">暂无热门内容</text>
        <text v-if="hotHasMore" class="loadmore" @click="loadMoreHot">上滑加载更多…</text>
      </scroller>
    </div>

    <!-- 搜索 -->
    <div v-else-if="activeTab === 'search'" class="tabbody">
      <div class="search-bar">
        <div class="search-input" @click="openKeyboard">
          <text class="search-text">{{ keyword ? keyword : placeholder }}</text>
        </div>
        <div class="search-btn" @click="openKeyboard">
          <text class="search-btn-text">搜索</text>
        </div>
      </div>
      <text v-if="status !== ''" class="status">{{ status }}</text>
      <scroller class="list" scroll-direction="vertical" :show-scrollbar="true"
                :loadmoreoffset="100" @loadmore="loadMoreSearch"
                @scroll="onListScroll" @touchstart="onListTouchStart" @touchmove="onListTouchMove" @touchend="onListTouchEnd">
        <!-- 搜索历史: 未出结果时显示, 点词直接搜 -->
        <div v-if="!searched" class="his-wrap">
          <div class="his-head">
            <text class="his-title">搜索历史</text>
            <text v-if="history.length > 0" class="his-clear" @click="clearHistory">清空</text>
          </div>
          <div class="his-chips">
            <div v-for="(kw, i) in history" :key="i" class="his-chip" @click="searchFromHistory(kw)">
              <text class="his-chip-text">{{ kw }}</text>
            </div>
          </div>
          <text v-if="history.length === 0" class="his-empty">还没有搜索记录</text>
        </div>
        <template v-else>
          <div v-for="item in results" :key="item.bvid" class="item" @click="openVideo(item)">
            <image class="cover" :src="item.pic" resize="cover" :lazy-load="true"></image>
            <div class="meta">
              <text class="title">{{ item.title }}</text>
              <text class="up">{{ item.author }}</text>
              <text class="stat">▶{{ item.playText }}  {{ item.duration }}</text>
            </div>
          </div>
          <text v-if="searched && results.length === 0 && !loading" class="empty">没有找到相关视频</text>
          <text v-if="searchHasMore && results.length > 0" class="loadmore" @click="loadMoreSearch">上滑加载更多…</text>
        </template>
      </scroller>
    </div>

    <!-- 动态 (视频 + 图文) -->
    <div v-else-if="activeTab === 'dynamic'" class="tabbody">
      <text v-if="dynStatus !== ''" class="status">{{ dynStatus }}</text>
      <div v-if="dynStatus !== '' && dynStatus.indexOf('未登录') >= 0" class="login-cta" @click="openLogin">
        <text class="login-cta-text">去登录</text>
      </div>
      <scroller v-if="dynStatus === '' || dynItems.length > 0" class="list"
                scroll-direction="vertical" :show-scrollbar="true"
                :loadmoreoffset="100" @loadmore="loadMoreDynamic"
                @scroll="onListScroll" @touchstart="onListTouchStart" @touchmove="onListTouchMove" @touchend="onListTouchEnd">
        <div v-for="(item, i) in dynItems" :key="item.bvid || ('draw' + i)" class="item"
             @click="item.type === 'video' ? openVideo(item) : null">
          <image class="cover" :src="item.pic" resize="cover" :lazy-load="true"></image>
          <div class="meta">
            <text class="title">{{ item.title }}</text>
            <text class="up">{{ item.author }} · {{ item.pubText }}</text>
            <text class="stat">{{ item.type === 'draw' ? ('图文 · ' + item.duration) : ('▶' + item.playText + '  ' + item.duration) }}</text>
          </div>
        </div>
        <text v-if="dynHasMore" class="loadmore" @click="loadMoreDynamic">上滑加载更多…</text>
        <text v-if="dynLoaded && dynItems.length === 0" class="empty">关注的 UP 主暂无动态</text>
      </scroller>
    </div>

    <!-- 我的 (内容高于可视区, 必须用 scroller 才能上下滑动) -->
    <div v-else-if="activeTab === 'mine'" class="tabbody">
      <scroller class="mine-scroll" scroll-direction="vertical" :show-scrollbar="true"
                @scroll="onListScroll" @touchstart="onListTouchStart" @touchmove="onListTouchMove" @touchend="onListTouchEnd">
        <div class="mine-inner">
          <image v-if="myInfo.isLogin && myInfo.face" class="myface" :src="myInfo.face" resize="cover"></image>
          <div v-else-if="myInfo.isLogin" class="myface-ph">
            <text class="myface-txt">{{ myInfo.uname ? myInfo.uname.charAt(0) : '?' }}</text>
          </div>
          <text v-if="myInfo.isLogin" class="ph-title">{{ myInfo.uname }}</text>
          <text v-if="myInfo.isLogin" class="ph-desc">UID {{ myInfo.mid }}</text>
          <!-- 等级 / 硬币 / B币: 三列统计, 数值在上标签在下 -->
          <div v-if="myInfo.isLogin" class="stat-row">
            <div class="stat-cell">
              <text class="stat-num">Lv{{ myInfo.level }}</text>
              <text class="stat-lab">等级</text>
            </div>
            <div class="stat-cell">
              <text class="stat-num">{{ myInfo.coin }}</text>
              <text class="stat-lab">硬币</text>
            </div>
            <div class="stat-cell">
              <text class="stat-num">{{ myInfo.money }}</text>
              <text class="stat-lab">B币</text>
            </div>
          </div>
          <!-- 历史记录 / 收藏 / 稍后再看 入口 -->
          <div v-if="myInfo.isLogin" class="entry-row">
            <div class="entry-btn" @click="openListPage('history')">
              <text class="entry-text">历史记录</text>
            </div>
            <div class="entry-btn" @click="openListPage('fav')">
              <text class="entry-text">收藏</text>
            </div>
            <div class="entry-btn" @click="openListPage('toview')">
              <text class="entry-text">稍后再看</text>
            </div>
          </div>
          <text v-if="myInfo.isLogin && myInfo.vip" class="ph-desc2">{{ myInfo.vip }}</text>
          <div v-if="!myInfo.isLogin && myLoaded" class="login-cta" @click="openLogin">
            <text class="login-cta-text">扫码登录 / Cookie 导入</text>
          </div>
          <div v-if="myInfo.isLogin" class="login-cta" @click="logout">
            <text class="login-cta-text">退出登录</text>
          </div>
          <text v-if="myStatus !== ''" class="ph-desc2">{{ myStatus }}</text>
          <text class="ph-desc2">bilibilipan v{{ appVersion }}</text>
          <text class="ph-desc2">appid {{ appid }} · 词典笔 mini-app</text>
          <text class="ph-desc2">{{ storeHint }}</text>
        </div>
      </scroller>
    </div>
  </div>
</template>

<script>
import { createIME } from '../../services/ime.js'
import { searchVideos, getPopular, getRecommend, getDynamicFeed, getMyInfo } from '../../services/bili.js'
import { afterPaint } from '../../base-page.js'
import { clearLogin, hasCookie, saveProfile } from '../../services/auth.js'
import { log, logStatus } from '../../services/log.js'
import { storeStatus, addSearchHistory, getSearchHistory, clearSearchHistory } from '../../services/store.js'
import pm from 'pm'

export default {
  name: 'index',
  data() {
    return {
      tabs: [
        { key: 'recommend', label: '推荐' },
        { key: 'hot', label: '热门' },
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
      searchPage: 1,
      searchHasMore: false,
      history: [],
      // 推荐 (rcmd 真主页推荐流)
      recResults: [],
      recStatus: '',
      recLoaded: false,
      recLoading: false,
      recGeneration: 0,
      recPage: 1,
      recHasMore: false,
      // 热门
      hotResults: [],
      hotStatus: '',
      hotLoaded: false,
      hotLoading: false,
      hotGeneration: 0,
      hotPage: 1,
      hotHasMore: false,
      // 动态
      dynItems: [],
      dynStatus: '',
      dynOffset: '',
      dynHasMore: false,
      dynLoaded: false,
      dynLoading: false,
      dynGeneration: 0,
      // 我的
      myInfo: { isLogin: false, uname: '', face: '', mid: 0, level: 0, coin: 0, money: 0, vip: '' },
      myStatus: '',
      myLoaded: false,
      myGeneration: 0,
      myLoading: false,
      // 我的 (版本号运行时从包管理器读取, 不硬编码)
      appVersion: '',
      appid: '8001812345678901',
      storeHint: ''
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
    // 底部状态: 日志与数据库的落盘位置 (便于排查)
    try {
      this.storeHint = logStatus() + ' · ' + storeStatus()
    } catch (e) {
      this.storeHint = ''
    }
    log('页面', '首页挂载 ' + this.storeHint)
    this.loadRecommend()
  },
  methods: {
    switchTab(key) {
      this.activeTab = key
      if (key === 'recommend' && !this.recLoaded && !this.recLoading) {
        this.loadRecommend()
      }
      if (key === 'hot' && !this.hotLoaded && !this.hotLoading) {
        this.loadHot()
      }
      if (key === 'search') {
        // 每次进入刷新历史 (可能在别处搜过 / 首次进入拉取)
        try { this.history = getSearchHistory(12) } catch (e) { this.history = [] }
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

    openListPage(name) {
      $falcon.navTo(name, {})
    },

    // ================= 下拉刷新 (所有 tab 共用) =================
    // 框架无 <refresh> 组件文档, 用 touch 事件自制: 列表停在顶部时向下拖
    // 超过 50px 松手触发刷新. 滚动位置经 @scroll 记入非响应式实例字段,
    // 避免 mvvm 重绘 (scrollEventInterval 提醒).
    onListScroll(e) {
      try {
        const co = e && e.contentOffset
        this._scrollY = co && typeof co.y === 'number' ? co.y : (this._scrollY || 0)
      } catch (err) {}
    },
    touchY(e) {
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
    onListTouchStart(e) {
      this._touchY0 = this.touchY(e)
      this._pullArmed = false
      this._pullOk = (this._scrollY || 0) <= 2   // 只在列表顶部允许下拉
    },
    onListTouchMove(e) {
      if (!this._pullOk) return
      if ((this._scrollY || 0) > 2) { this._pullOk = false; return }  // 列表在滚动 → 不是下拉
      if (this.touchY(e) - this._touchY0 > 50) this._pullArmed = true
    },
    onListTouchEnd() {
      if (this._pullArmed && this._pullOk && (this._scrollY || 0) <= 2) this.refreshTab()
      this._pullArmed = false
    },
    refreshTab() {
      log('页面', '下拉刷新 ' + this.activeTab)
      switch (this.activeTab) {
        case 'recommend':
          if (this.recLoading) return
          this.recPage = 1; this.recHasMore = true; this.recLoaded = false
          this.loadRecommend()
          break
        case 'hot':
          if (this.hotLoading) return
          this.hotPage = 1; this.hotHasMore = true; this.hotLoaded = false
          this.loadHot()
          break
        case 'search':
          if (this.loading) return
          if (this.keyword) this.doSearch(this.keyword)
          break
        case 'dynamic':
          if (this.dynLoading) return
          this.dynOffset = ''
          this.loadDynamic('')
          break
        case 'mine':
          if (this.myLoading) return
          this.loadMine()
          break
      }
    },

    // ================= 推荐 (真·主页推荐流) =================
    async loadRecommend(append) {
      const gen = ++this.recGeneration
      if (this.recLoading && append) return
      this.recLoading = true
      this.recStatus = append ? '加载更多…' : '加载中…'
      // 先让首帧画出加载态再发请求: bilinet.httpGet 同步阻塞 JS 线程
      afterPaint(async () => {
        try {
          const videos = await getRecommend(this.recPage)
          if (gen !== this.recGeneration) return
          if (append) {
            const seen = {}
            for (let i = 0; i < this.recResults.length; i++) seen[this.recResults[i].bvid] = true
            let added = 0
            for (let i = 0; i < videos.length; i++) {
              if (!seen[videos[i].bvid]) { this.recResults.push(videos[i]); added++ }
            }
            // 整页重复 = 到底了 (rcmd 无 has_more, 用这个判停)
            this.recHasMore = videos.length > 0 && added > 0
          } else {
            this.recResults = videos
            this.recHasMore = videos.length > 0
          }
          this.recLoaded = true
          this.recStatus = videos.length === 0 && !append ? '暂无推荐内容' : ''
        } catch (err) {
          if (gen !== this.recGeneration) return
          console.log('[bili] recommend error: ' + (err && err.message ? err.message : err))
          this.recStatus = err && err.message ? err.message : String(err)
          if (!append) this.recResults = []
          this.recLoaded = true
        } finally {
          if (gen === this.recGeneration) this.recLoading = false
        }
      })
    },
    loadMoreRecommend() {
      if (this.recLoading || !this.recHasMore) return
      this.recPage++
      this.loadRecommend(true)
    },

    // ================= 热门 =================
    async loadHot(append) {
      const gen = ++this.hotGeneration
      this.hotLoading = true
      this.hotStatus = append ? '加载更多…' : '加载中…'
      afterPaint(async () => {
        try {
          const videos = await getPopular(this.hotPage)
          if (gen !== this.hotGeneration) return
          if (append) {
            const seen = {}
            for (let i = 0; i < this.hotResults.length; i++) seen[this.hotResults[i].bvid] = true
            for (let i = 0; i < videos.length; i++) {
              if (!seen[videos[i].bvid]) this.hotResults.push(videos[i])
            }
          } else {
            this.hotResults = videos
          }
          this.hotHasMore = videos.length >= 20
          this.hotLoaded = true
          this.hotStatus = videos.length === 0 && !append ? '暂无热门内容' : ''
        } catch (err) {
          if (gen !== this.hotGeneration) return
          console.log('[bili] hot error: ' + (err && err.message ? err.message : err))
          this.hotStatus = err && err.message ? err.message : String(err)
          if (!append) this.hotResults = []
          this.hotLoaded = true
        } finally {
          if (gen === this.hotGeneration) this.hotLoading = false
        }
      })
    },
    loadMoreHot() {
      if (this.hotLoading || !this.hotHasMore) return
      this.hotPage++
      this.loadHot(true)
    },

    // ================= 动态 (视频 + 图文, 需登录) =================
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
    loadMoreDynamic() {
      if (this.dynLoading || !this.dynHasMore) return
      this.loadDynamic(this.dynOffset)
    },

    // ================= 我的: 登录态 + 账号信息 =================
    async loadMine() {
      const gen = ++this.myGeneration
      this.myLoading = true
      this.myStatus = ''
      afterPaint(async () => {
        try {
          const info = await getMyInfo()
          if (gen !== this.myGeneration) return
          this.myInfo = info
          if (!info.isLogin) {
            this.myStatus = '未登录'
            log('我的', '未登录')
          } else {
            // 账号快照落库 (昵称/头像/等级/硬币/B币)
            saveProfile(info)
            log('我的', info.uname + ' uid=' + info.mid + ' Lv' + info.level +
              ' 硬币=' + info.coin + ' B币=' + info.money)
          }
        } catch (err) {
          if (gen !== this.myGeneration) return
          const msg = err && err.message ? err.message : String(err)
          this.myStatus = msg
          this.myInfo.isLogin = false
          log('我的', '获取失败: ' + msg)
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
      this.myInfo = { isLogin: false, uname: '', face: '', mid: 0, level: 0, coin: 0, money: 0, vip: '' }
      this.myStatus = '已退出登录'
      // 动态缓存态作废, 下次进入重新按登录态加载
      this.dynLoaded = false
      this.dynItems = []
    },

    // ================= 搜索 =================
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
      this.searchPage = 1
      // 先让首帧画出加载态再发请求: bilinet.httpGet 同步阻塞 JS 线程
      afterPaint(async () => {
        try {
          const videos = await searchVideos(keyword.trim(), 1)
          if (gen !== this.generation) return
          this.results = videos
          this.searched = true
          this.searchHasMore = videos.length >= 20
          this.status = ''
          // 记入搜索历史 (db 落盘, 失败静默)
          try {
            addSearchHistory(keyword.trim())
            this.history = getSearchHistory(12)
          } catch (e) {}
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

    loadMoreSearch() {
      if (this.loading || !this.searchHasMore || !this.searched) return
      this.searchPage++
      const gen = ++this.generation
      this.loading = true
      afterPaint(async () => {
        try {
          const videos = await searchVideos(this.keyword.trim(), this.searchPage)
          if (gen !== this.generation) return
          const seen = {}
          for (let i = 0; i < this.results.length; i++) seen[this.results[i].bvid] = true
          for (let i = 0; i < videos.length; i++) {
            if (!seen[videos[i].bvid]) this.results.push(videos[i])
          }
          this.searchHasMore = videos.length >= 20
        } catch (err) {
          if (gen !== this.generation) return
          this.searchHasMore = false
        } finally {
          if (gen === this.generation) this.loading = false
        }
      })
    },

    searchFromHistory(kw) {
      this.keyword = kw
      this.doSearch(kw)
    },

    clearHistory() {
      try { clearSearchHistory() } catch (e) {}
      this.history = []
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
      // 投币/收藏等交互可能改了硬币余额: 回首页后把「我的」标脏, 下次进入重拉
      if (this.myLoaded && hasCookie()) this.myLoaded = false
    },

    onHide() {
      // 输入法弹出可能触发 onHide, 不能在这里销毁 IME 会话
    },
    onUnload() {
      this.generation++
      this.recGeneration++
      this.hotGeneration++
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
  width: 192px;
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
/* 列表区吃满剩余高度 (搜索页有结果时下方不再留空白) */
.list {
  width: 960px;
  flex: 1;
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
  height: 46px;
  margin-left: 20px;
  background-color: #2c2c2c;
  border-radius: 23px;
  justify-content: center;
}
.search-text {
  font-size: 24px;
  color: #ffffff;
  margin-left: 24px;
}
.search-btn {
  width: 120px;
  height: 46px;
  margin-left: 20px;
  background-color: #fb7299;
  border-radius: 23px;
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
/* 搜索历史 */
.his-wrap {
  width: 920px;
  margin-left: 20px;
  margin-top: 10px;
  flex-direction: column;
}
.his-head {
  flex-direction: row;
  align-items: center;
}
.his-title {
  font-size: 22px;
  color: #ffffff;
}
.his-clear {
  font-size: 20px;
  color: #fb7299;
  margin-left: 24px;
  padding-top: 8px;
  padding-bottom: 8px;
  padding-left: 12px;
  padding-right: 12px;
}
.his-chips {
  flex-direction: row;
  flex-wrap: wrap;
  margin-top: 8px;
}
.his-chip {
  height: 44px;
  padding-left: 20px;
  padding-right: 20px;
  margin-right: 14px;
  margin-bottom: 12px;
  border-radius: 22px;
  background-color: #2c2c2c;
  justify-content: center;
  align-items: center;
}
.his-chip-text {
  font-size: 20px;
  color: #c8d2de;
}
.his-empty {
  font-size: 20px;
  color: #666666;
  margin-top: 12px;
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
  /* Falcon 不支持 max-lines, 必须用 lines: N 配合 text-overflow */
  lines: 2;
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
  margin-top: 8px;
}
.ph-desc {
  font-size: 22px;
  color: #999999;
  margin-top: 6px;
}
.ph-desc2 {
  font-size: 20px;
  color: #666666;
  margin-top: 10px;
}

.login-cta {
  margin-top: 18px;
  padding-left: 32px;
  padding-right: 32px;
  height: 50px;
  border-radius: 25px;
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
.myface-ph {
  width: 72px;
  height: 72px;
  border-radius: 36px;
  background-color: #fb7299;
  justify-content: center;
  align-items: center;
  margin-bottom: 10px;
}
.myface-txt {
  font-size: 34px;
  color: #ffffff;
}
/* 我的: 内容比可视区高, 需要整块可滚动 */
.mine-scroll {
  width: 960px;
  flex: 1;
  flex-direction: column;
}
.mine-inner {
  width: 960px;
  flex-direction: column;
  align-items: center;
  padding-top: 4px;
  padding-bottom: 20px;
}
.stat-row {
  flex-direction: row;
  margin-top: 16px;
  margin-bottom: 4px;
}
.stat-cell {
  width: 200px;
  flex-direction: column;
  align-items: center;
}
.stat-num {
  font-size: 26px;
  color: #ffffff;
}
.stat-lab {
  font-size: 20px;
  color: #999999;
  margin-top: 4px;
}
/* 我的页入口: 历史记录 / 收藏 / 稍后再看 */
.entry-row {
  flex-direction: row;
  margin-top: 16px;
  margin-bottom: 4px;
}
.entry-btn {
  width: 200px;
  height: 48px;
  border-radius: 24px;
  background-color: #2c2c2c;
  justify-content: center;
  align-items: center;
  margin-left: 12px;
  margin-right: 12px;
}
.entry-text {
  font-size: 22px;
  color: #ffffff;
}
.loadmore {
  font-size: 20px;
  color: #fb7299;
  text-align: center;
  margin-top: 12px;
  margin-bottom: 12px;
}
</style>
