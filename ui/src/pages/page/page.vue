<template>
  <div class="page" :class="entering ? 'page-enter' : ''">
    <!-- 左栏: 封面 (不放播放器也不放播放条, 点封面进播放器页; 播放按钮在右栏详情 tab) -->
    <div class="left">
      <image v-if="coverSrc" class="cover" :src="coverSrc" resize="cover" @click="openPlayer"></image>
      <div v-else class="cover cover-ph"></div>
      <text v-if="detail" class="dur">{{ detail.duration }}</text>
      <!-- 返回按钮: 左上角悬浮于封面上 (0.9.5 需求: 返回按钮放左上角) -->
      <div class="backbtn" @click="goBack">
        <text class="backbtn-text">‹ 返回</text>
      </div>
    </div>

    <!-- 右栏: 详情 / 评论 同页 tab 切换; 左右滑动切换 (touch 事件冒泡自内部 scroller) -->
    <div class="right"
         @touchstart="onTouchStart" @touchmove="onTouchMove" @touchend="onTouchEnd">
      <div class="tabbar">
        <div :class="['tab', tab === 'detail' ? 'tab-on' : '']" @click="switchTab('detail')">
          <text :class="['tab-text', tab === 'detail' ? 'tab-text-on' : '']">详情</text>
        </div>
        <div :class="['tab', tab === 'comment' ? 'tab-on' : '']" @click="switchTab('comment')">
          <text :class="['tab-text', tab === 'comment' ? 'tab-text-on' : '']">评论{{ total > 0 ? ' ' + total : '' }}</text>
        </div>
        <div class="tab-spacer"></div>
        <div class="mini-btn" @click="goHome">
          <text class="mini-text">⌂</text>
        </div>
      </div>

      <!-- ============ 详情 tab ============ -->
      <scroller v-if="tab === 'detail'" class="detail-scroll" scroll-direction="vertical" :show-scrollbar="true"
                @scroll="onListScroll">
        <div ref="topRef"></div>
        <!-- 标题: 默认 2 行截断 (...), 点击展开/收起.
             :key 强制换元素重建 —— Falcon text 的 lines 样式创建后不随 class 更新
             (0.9.4 简介点了要切 tab 再回来才展开的根因), 只能重建生效 -->
        <text :key="'t' + (titleExpanded ? 1 : 0)"
              :class="['title', titleExpanded ? 'title-open' : '']" @click="toggleTitle">{{ detail ? detail.title : fallbackTitle }}</text>
        <text class="author" @click="openUp">{{ detail ? (detail.author + ' › · ') : '' }}{{ detail ? detail.pubdateText : '' }}</text>
        <text v-if="detail" class="stat">播放 {{ detail.playText }} · 弹幕 {{ detail.danmakuText }} · {{ detail.duration }}</text>
        <text v-if="detail" class="stat">赞 {{ detail.likeText }} · 币 {{ detail.coinText }} · 藏 {{ detail.favText }} · 转 {{ detail.shareText }}</text>
        <div v-if="detail" class="btnrow">
          <div class="playbtn" @click="openPlayer">
            <text class="play-text">▶ 播放</text>
          </div>
          <text v-if="actStatus !== ''" class="act-status">{{ actStatus }}</text>
        </div>
        <!-- 交互行: 点赞/投币/收藏/三连/稍后再看 (状态高亮; 均可再点取消, 投币除外) -->
        <div v-if="detail" class="actrow">
          <div :class="['act-btn', detail.reqLike ? 'act-on' : '']" @click="doLike">
            <text :class="['act-text', detail.reqLike ? 'act-text-on' : '']">{{ detail.reqLike ? '已赞' : '点赞' }}</text>
          </div>
          <div :class="['act-btn', detail.reqCoin ? 'act-on' : '']" @click="openCoinPicker">
            <text :class="['act-text', detail.reqCoin ? 'act-text-on' : '']">{{ detail.reqCoin ? '已币' : '投币' }}</text>
          </div>
          <div :class="['act-btn', detail.reqFav ? 'act-on' : '']" @click="onFavTap">
            <text :class="['act-text', detail.reqFav ? 'act-text-on' : '']">{{ detail.reqFav ? '已藏' : '收藏' }}</text>
          </div>
          <div class="act-btn act-triple" @click="doTriple">
            <text class="act-text">三连</text>
          </div>
          <div :class="['act-btn', detail.reqToview ? 'act-on' : '']" @click="doToview">
            <text :class="['act-text', detail.reqToview ? 'act-text-on' : '']">{{ detail.reqToview ? '已加' : '稍后看' }}</text>
          </div>
        </div>

        <text v-if="error !== ''" class="state-inline">{{ error }}</text>
        <text v-if="loading" class="state-inline">加载中…</text>

        <div v-if="detail" class="section">
          <text class="sec-title">简介</text>
          <!-- 简介: 超 3 行收起 (...), 点击展开 (:key 重建生效, 同上) -->
          <text :key="'d' + (descExpanded ? 1 : 0)"
                :class="['desc', descExpanded ? 'desc-open' : '']" @click="toggleDesc">{{ detail.desc !== '' ? detail.desc : '暂无简介' }}</text>
        </div>

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
        <text class="pull-hint">↓ 下拉刷新 · 左右滑动切「详情/评论」</text>
      </scroller>

      <!-- ============ 评论 tab ============ -->
      <div v-else class="cwrap">
        <!-- 排序切换: 热度 / 最新 -->
        <div class="sortbar">
          <div :class="['sort-item', sortMode === 'hot' ? 'sort-on' : '']" @click="switchSort('hot')">
            <text :class="['sort-text', sortMode === 'hot' ? 'sort-text-on' : '']">热度</text>
          </div>
          <div :class="['sort-item', sortMode === 'time' ? 'sort-on' : '']" @click="switchSort('time')">
            <text :class="['sort-text', sortMode === 'time' ? 'sort-text-on' : '']">最新</text>
          </div>
        </div>
        <scroller class="clist" scroll-direction="vertical" :show-scrollbar="true"
                  :loadmoreoffset="100" @loadmore="onCommentsLoadmore" @scroll="onListScroll">
          <text v-if="cStatus !== ''" class="c-status">{{ cStatus }}</text>
          <!-- 未登录: 登录引导 -->
          <div v-if="!logged && !cLoading" class="gate">
            <text class="gate-text">评论需要登录后查看</text>
            <div class="gate-btn" @click="goLogin">
              <text class="gate-btn-text">去登录 (扫码 / 电脑同步)</text>
            </div>
          </div>
          <div v-else>
            <div v-for="r in replies" :key="r.rpid" class="reply">
              <image class="face" :src="r.face" resize="cover"></image>
              <div class="reply-main">
                <div class="reply-head">
                  <text class="reply-author">{{ r.author }}</text>
                  <text class="reply-time">{{ r.timeText }}</text>
                </div>
                <!-- 图文混排: B 站表情 + emoji 转图片 (设备字体无 emoji 字形); 超 3 行收起, 点击展开.
                     :key 重建生效 (lines 不随 class 更新) -->
                <richtext :key="'r' + r.rpid + (r.expanded ? 1 : 0)"
                          :class="['reply-msg', r.expanded ? 'reply-msg-open' : '']" @click="toggleReply(r)">
                  <template v-for="(seg, si) in r.segs">
                    <span v-if="seg.t === 0" :key="'s' + si">{{ seg.v }}</span>
                    <image v-else :key="'e' + si" :src="seg.v" :style="{ width: seg.w + 'px', height: seg.h + 'px' }"></image>
                  </template>
                </richtext>
                <div class="reply-meta">
                  <text class="meta-text">赞 {{ r.likeText }}</text>
                  <text class="meta-reply" @click="openSubReply(r)">回复 {{ r.replyCount }}</text>
                </div>
              </div>
            </div>
          </div>
          <text v-if="logged && replies.length > 0 && hasMore" class="load-more" @click="loadMore">加载更多评论…</text>
          <text v-if="logged && !cLoading && replies.length === 0 && cStatus === ''" class="empty">还没有评论, 抢首评</text>
        </scroller>
        <!-- 底部发评栏 -->
        <div class="postbar">
          <div class="post-input" @click="openPostInput">
            <text class="post-input-text">{{ logged ? '说点什么…' : '登录后参与评论' }}</text>
          </div>
          <div class="post-btn" @click="openPostInput">
            <text class="post-btn-text">发送</text>
          </div>
        </div>
      </div>
    </div>

    <!-- 选择器浮层: 收藏夹 / 投币数量 (遮罩不绑点击, 只能点「取消」关闭, 防误触) -->
    <div v-if="pickerMode !== ''" class="picker-mask">
      <div class="picker">
        <text class="picker-title">{{ pickerMode === 'fav' ? '选择收藏夹' : '投币数量' }}</text>
        <div v-if="pickerMode === 'coin'" class="picker-row">
          <div class="picker-coin" @click="pickCoin(1)">
            <text class="picker-coin-text">投 1 币</text>
          </div>
          <div class="picker-coin" @click="pickCoin(2)">
            <text class="picker-coin-text">投 2 币</text>
          </div>
        </div>
        <scroller v-else class="picker-list" scroll-direction="vertical" :show-scrollbar="true">
          <text v-if="favLoading" class="picker-state">加载收藏夹…</text>
          <text v-else-if="favFolders.length === 0" class="picker-state">没有可用收藏夹 (可在网页端创建)</text>
          <div v-for="f in favFolders" :key="f.id" class="picker-item" @click="pickFolder(f)">
            <text class="picker-item-title">{{ f.title }}</text>
            <text class="picker-item-sub">{{ f.mediaCount }} 个</text>
          </div>
        </scroller>
        <div class="picker-cancel" @click="closePicker">
          <text class="picker-cancel-text">取消</text>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
// 详情页 v3: 左栏封面 (左上角返回按钮) + 右栏「详情/评论」同页 tab.
// 0.9.5: 交互行 (赞/投币/收藏/三连/稍后再看) · 评论无限滑动 + 下拉刷新 · 左右滑动切 tab ·
//        长文本展开 :key 重建修复 (lines 样式不随 class 更新) · 先进页面画完进入动画再发请求.
// nextPage: 相关推荐点击后的跳转目标页副本名 (page->page2->...->page12->page 轮换栈)。
import { createIME } from '../../services/ime.js'
import {
  getVideoDetail, getRelatedVideos, getReplies, addReply,
  likeVideo, addCoin, dealFav, addToViewLater, delToViewLater, isInToView, getFavFolders,
  getInteractState
} from '../../services/bili.js'
import { hasCookie } from '../../services/auth.js'
import { afterPaint } from '../../base-page.js'

// 内置常用 emoji 映射: .vue 里的 require png 会被 aiot-cli 编译成 images/<hash>.png
// (services/*.js 里的 require 不会被编译, QuickJS 无 require 会崩, 见 0.8.7 黑屏教训)
const BUILTIN_EMOJI = {
  '1f197': require('../../assets/emoji/1f197.png'),
  '1f338': require('../../assets/emoji/1f338.png'),
  '1f339': require('../../assets/emoji/1f339.png'),
  '1f349': require('../../assets/emoji/1f349.png'),
  '1f34b': require('../../assets/emoji/1f34b.png'),
  '1f35a': require('../../assets/emoji/1f35a.png'),
  '1f37a': require('../../assets/emoji/1f37a.png'),
  '1f381': require('../../assets/emoji/1f381.png'),
  '1f382': require('../../assets/emoji/1f382.png'),
  '1f389': require('../../assets/emoji/1f389.png'),
  '1f414': require('../../assets/emoji/1f414.png'),
  '1f42e': require('../../assets/emoji/1f42e.png'),
  '1f431': require('../../assets/emoji/1f431.png'),
  '1f436': require('../../assets/emoji/1f436.png'),
  '1f437': require('../../assets/emoji/1f437.png'),
  '1f440': require('../../assets/emoji/1f440.png'),
  '1f446': require('../../assets/emoji/1f446.png'),
  '1f448': require('../../assets/emoji/1f448.png'),
  '1f449': require('../../assets/emoji/1f449.png'),
  '1f44d': require('../../assets/emoji/1f44d.png'),
  '1f44e': require('../../assets/emoji/1f44e.png'),
  '1f44f': require('../../assets/emoji/1f44f.png'),
  '1f451': require('../../assets/emoji/1f451.png'),
  '1f47b': require('../../assets/emoji/1f47b.png'),
  '1f480': require('../../assets/emoji/1f480.png'),
  '1f494': require('../../assets/emoji/1f494.png'),
  '1f495': require('../../assets/emoji/1f495.png'),
  '1f496': require('../../assets/emoji/1f496.png'),
  '1f497': require('../../assets/emoji/1f497.png'),
  '1f498': require('../../assets/emoji/1f498.png'),
  '1f4a9': require('../../assets/emoji/1f4a9.png'),
  '1f4aa': require('../../assets/emoji/1f4aa.png'),
  '1f4ac': require('../../assets/emoji/1f4ac.png'),
  '1f4af': require('../../assets/emoji/1f4af.png'),
  '1f525': require('../../assets/emoji/1f525.png'),
  '1f600': require('../../assets/emoji/1f600.png'),
  '1f602': require('../../assets/emoji/1f602.png'),
  '1f604': require('../../assets/emoji/1f604.png'),
  '1f605': require('../../assets/emoji/1f605.png'),
  '1f606': require('../../assets/emoji/1f606.png'),
  '1f607': require('../../assets/emoji/1f607.png'),
  '1f609': require('../../assets/emoji/1f609.png'),
  '1f60a': require('../../assets/emoji/1f60a.png'),
  '1f60d': require('../../assets/emoji/1f60d.png'),
  '1f60f': require('../../assets/emoji/1f60f.png'),
  '1f612': require('../../assets/emoji/1f612.png'),
  '1f618': require('../../assets/emoji/1f618.png'),
  '1f61c': require('../../assets/emoji/1f61c.png'),
  '1f621': require('../../assets/emoji/1f621.png'),
  '1f622': require('../../assets/emoji/1f622.png'),
  '1f629': require('../../assets/emoji/1f629.png'),
  '1f62a': require('../../assets/emoji/1f62a.png'),
  '1f62d': require('../../assets/emoji/1f62d.png'),
  '1f631': require('../../assets/emoji/1f631.png'),
  '1f633': require('../../assets/emoji/1f633.png'),
  '1f634': require('../../assets/emoji/1f634.png'),
  '1f644': require('../../assets/emoji/1f644.png'),
  '1f64f': require('../../assets/emoji/1f64f.png'),
  '1f914': require('../../assets/emoji/1f914.png'),
  '1f917': require('../../assets/emoji/1f917.png'),
  '1f91d': require('../../assets/emoji/1f91d.png'),
  '1f921': require('../../assets/emoji/1f921.png'),
  '1f923': require('../../assets/emoji/1f923.png'),
  '1f92c': require('../../assets/emoji/1f92c.png'),
  '1f970': require('../../assets/emoji/1f970.png'),
  '1f973': require('../../assets/emoji/1f973.png'),
  '1f976': require('../../assets/emoji/1f976.png'),
  '1f97a': require('../../assets/emoji/1f97a.png'),
  '2615': require('../../assets/emoji/2615.png'),
  '2705': require('../../assets/emoji/2705.png'),
  '2728': require('../../assets/emoji/2728.png'),
  '274c': require('../../assets/emoji/274c.png'),
  '2753': require('../../assets/emoji/2753.png'),
  '2764': require('../../assets/emoji/2764.png'),
}

// 进入动画 60+260ms, 数据加载排在动画之后: 同步 http 阻塞 JS 会把动画卡在起点
// (真机反馈「返回原页面动画卡顿」的根因之一 —— 动画 class 翻转的 timer 被请求堵住)
var LOAD_DELAY_MS = 340
var SWIPE_DX = 80          // 左右滑动切 tab 的最小横向位移
var SWIPE_DY_MAX = 50      // 超过此竖向位移视为滚动不切 tab
var PULL_DY = 55           // 顶部下拉刷新触发阈值

export default {
  name: 'page',
  props: {
    nextPage: { type: String, default: 'page2' }
  },
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
      entering: true,   // 页面进入动画: 首次渲染后翻转为 false
      // 长文本收起/展开: 标题默认 2 行, 简介默认 3 行, 点击切换 (:key 重建生效)
      titleExpanded: false,
      descExpanded: false,
      // 同页 tab: 'detail' | 'comment'
      tab: 'detail',
      // ---- 交互操作 (赞/币/藏/三连/稍后再看) ----
      actStatus: '',
      actBusy: false,
      favFolders: [],     // 收藏夹列表 (含 favoured 状态, 按当前 aid 拉取)
      favFoldersAid: 0,   // 缓存对应的 aid (0 = 未加载)
      favLoading: false,
      pickerMode: '',     // '' | 'fav'(收藏夹选择) | 'coin'(投币数量选择)
      // ---- 评论区状态 ----
      sortMode: 'hot',   // 'hot'=热度 / 'time'=最新
      replies: [],
      total: 0,
      pn: 1,
      hasMore: false,
      cLoading: false,
      cLoaded: false,
      logged: false,
      cStatus: '',
      posting: false,
      ime: null,
      cGeneration: 0
    }
  },
  computed: {
    coverSrc() {
      return this.detail && this.detail.pic ? this.detail.pic : ''
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
      this.currentPage = parseInt(options.page || '1', 10) || 1
      this.detail = null
      this.related = []
      this.error = ''
      this.loading = true
      // 切视频: 评论状态整体作废, 停在详情 tab; 长文本回到收起态
      this.resetComments()
      this.titleExpanded = false
      this.descExpanded = false
      this.tab = 'detail'
      this.actStatus = ''
      this.pickerMode = ''
      this.favFolders = []
      this.favFoldersAid = 0
      this.load()
      this.scrollTop()
    },

    resetComments() {
      this.cGeneration++
      this.replies = []
      this.total = 0
      this.pn = 1
      this.hasMore = false
      this.cLoading = false
      this.cLoaded = false
      this.cStatus = ''
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
      if (this.$page && !this._newOptionsBound) {
        this._newOptionsBound = true
        const self = this
        this.$page.onNewOptions = function (options) { self.onNewOptions(options) }
      }
      // 本页自带「左右滑动切 详情/评论」手势, 必须关掉框架的滑动返回
      // (真机实测: 横滑会触发系统左滑退出, 直接把应用退出到上一个 app)
      if (this.$page && this.$page.$npage && !this._backDisabled) {
        try {
          this.$page.$npage.setSupportBack(false)
          this._backDisabled = true
        } catch (e) {
          console.log('[page] setSupportBack(false) failed: ' + (e && e.message ? e.message : e))
        }
      }
      const wasLogged = this.logged
      this.logged = hasCookie()
      this.beginLoad()
      // 从登录页返回: 评论 tab 之前被门禁挡住, 补一次加载
      if (this.logged && !wasLogged && this.tab === 'comment' && this.detail && this.detail.aid && !this.cLoaded) {
        this.loadComments(true)
      }
      if (this.entering) {
        const self2 = this
        try {
          const p = this.$page
          if (p && p.setTimeout) p.setTimeout(function () { self2.entering = false }, 60)
          else setTimeout(function () { self2.entering = false }, 60)
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
      // 先进页面再加载: 等进入动画 (340ms) 画完再发同步阻塞请求
      afterPaint(async () => {
        try {
          const d = await getVideoDetail(this.bvid)
          if (gen !== this.generation) return
          this.detail = d
          if (d.pages.length > 1 && this.currentPage >= 1 && this.currentPage <= d.pages.length) {
            const p = d.pages[this.currentPage - 1]
            if (p && p.part) this.detail.title = d.title + '（' + p.part + '）'
          }
          // 交互状态 (赞/币/藏) 与稍后再看: view 的 req_user 对本应用恒为空,
          // 用专用状态接口异步补齐 (失败静默, 按钮退化为未操作态)
          if (this.logged && d.aid) {
            const self2 = this
            // 状态查询是同步阻塞请求, 延后一拍让详情先画出来
            afterPaint(function () {
            getInteractState(d.aid).then(function (st) {
              if (gen !== self2.generation || !self2.detail || self2.detail.aid !== d.aid) return
              self2.detail.reqLike = st.like
              self2.detail.reqCoin = st.coin
              self2.detail.reqFav = st.fav
            })
            isInToView(d.aid).then(function (inList) {
              if (gen === self2.generation && self2.detail && self2.detail.aid === d.aid) {
                self2.detail.reqToview = inList
              }
            })
            }, 30)
          }
        } catch (err) {
          if (gen !== this.generation) return
          console.log('[bili] detail error: ' + (err && err.message ? err.message : err))
          this.error = err && err.message ? err.message : String(err)
        } finally {
          if (gen === this.generation) this.loading = false
        }
        try {
          const rel = await getRelatedVideos(this.bvid)
          if (gen !== this.generation) return
          this.related = rel
        } catch (e) {}
      }, LOAD_DELAY_MS)
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
      this.titleExpanded = false
      this.descExpanded = false
      this.pickerMode = ''
      this.favFolders = []
      this.favFoldersAid = 0
      this.resetComments()
      this.load()
    },

    // 推荐视频跳转到"下一份"详情页副本, 实现真正的页面叠加 (同名页只替换)
    openVideo(item) {
      $falcon.navTo(this.nextPage || 'page2', { bvid: item.bvid, title: item.title })
    },

    openPlayer() {
      if (!this.detail) return
      $falcon.navTo('player', { bvid: this.bvid, page: String(this.currentPage), title: this.detail.title })
    },

    switchTab(t) {
      this.tab = t
      if (t === 'comment' && !this.cLoaded && !this.cLoading && this.detail && this.detail.aid && this.logged) {
        this.loadComments(true)
      }
    },

    // ---------- 左右滑动切 tab + 下拉刷新 (touch 事件, 冒泡自内部 scroller) ----------
    touchXY(e) {
      try {
        const t = (e && e.changedTouches && e.changedTouches[0]) ||
          (e && e.touches && e.touches[0]) || e
        if (t) {
          if (typeof t.pageX === 'number') return { x: t.pageX, y: t.pageY }
          if (typeof t.clientX === 'number') return { x: t.clientX, y: t.clientY }
          if (typeof t.x === 'number') return { x: t.x, y: t.y }
        }
      } catch (err) {}
      return { x: 0, y: 0 }
    },
    onListScroll(e) {
      try {
        const co = e && e.contentOffset
        this._scrollY = co && typeof co.y === 'number' ? co.y : (this._scrollY || 0)
      } catch (err) {}
    },
    onTouchStart(e) {
      const p = this.touchXY(e)
      this._tx0 = p.x
      this._ty0 = p.y
      this._pullArmed = false
      this._pullOk = (this._scrollY || 0) <= 2
    },
    onTouchMove(e) {
      if (!this._pullOk) return
      if ((this._scrollY || 0) > 2) { this._pullOk = false; return }
      const p = this.touchXY(e)
      if (p.y - this._ty0 > PULL_DY && Math.abs(p.x - this._tx0) < 40) this._pullArmed = true
    },
    onTouchEnd(e) {
      const p = this.touchXY(e)
      const dx = p.x - this._tx0
      const dy = p.y - this._ty0
      // 1) 左右滑动: 横向大幅 + 竖向小幅 → 切 tab
      if (Math.abs(dx) > SWIPE_DX && Math.abs(dy) < SWIPE_DY_MAX) {
        this._pullArmed = false
        this.switchTab(dx < 0 ? 'comment' : 'detail')
        return
      }
      // 2) 顶部下拉: 刷新当前 tab
      if (this._pullArmed && this._pullOk && (this._scrollY || 0) <= 2) {
        this._pullArmed = false
        this.refreshTab()
        return
      }
      this._pullArmed = false
    },
    refreshTab() {
      if (this.tab === 'detail') {
        if (this.loading) return
        // 保留展开态, 强拉最新详情 (交互后状态刷新也走这里)
        this.favFoldersAid = 0
        this.reloadDetail()
      } else {
        if (this.cLoading || !this.logged) return
        this.pn = 1
        this.loadComments(true)
      }
    },
    // 强拉详情刷新计数与 req_user 状态 (不动展开/合集等界面态)
    reloadDetail() {
      const gen = this.generation
      getVideoDetail(this.bvid, true).then((d) => {
        if (gen !== this.generation || !this.detail) return
        this.detail.likeText = d.likeText
        this.detail.coinText = d.coinText
        this.detail.favText = d.favText
        this.detail.shareText = d.shareText
        // 注意: 不要用 d.reqLike/reqCoin/reqFav 覆盖 —— view 的 req_user 对本应用
        // 恒为空对象, 覆盖会把刚点亮的按钮状态打回未操作态 (真机踩过)
      }).catch(() => {})
    },

    // ---------- 交互操作 (赞/投币/收藏/三连/稍后再看) ----------
    requireLogin() {
      if (this.logged) return true
      this.goLogin()
      return false
    },

    async doLike() {
      if (!this.detail || !this.detail.aid || this.actBusy) return
      if (!this.requireLogin()) return
      this.actBusy = true
      const want = !this.detail.reqLike
      this.actStatus = want ? '点赞中…' : '取消中…'
      try {
        await likeVideo(this.detail.aid, want)
        this.detail.reqLike = want
        this.actStatus = want ? '✓ 已点赞' : '已取消'
        this.reloadDetail()
      } catch (err) {
        this.actStatus = '操作失败: ' + (err && err.message ? err.message : String(err))
      } finally {
        this.actBusy = false
      }
    },

    // 投币: 打开数量选择器 (1 币 / 2 币)
    openCoinPicker() {
      if (!this.detail || !this.detail.aid || this.actBusy) return
      if (!this.requireLogin()) return
      if (this.detail.reqCoin) {
        this.actStatus = '该视频已投过币'
        return
      }
      this.actStatus = ''
      this.pickerMode = 'coin'
    },

    pickCoin(n) {
      this.closePicker()
      this.doCoin(n)
    },

    async doCoin(n) {
      if (!this.detail || !this.detail.aid || this.actBusy) return
      const num = n === 2 ? 2 : 1
      this.actBusy = true
      this.actStatus = '投币中…'
      try {
        const r = await addCoin(this.detail.aid, num, false)
        this.detail.reqCoin = true
        if (r && r.like) this.detail.reqLike = true
        this.actStatus = '✓ 已投 ' + num + ' 币'
        this.reloadDetail()
      } catch (err) {
        this.actStatus = '投币失败: ' + (err && err.message ? err.message : String(err))
      } finally {
        this.actBusy = false
      }
    },

    // 收藏: 未收藏 -> 弹收藏夹选择; 已收藏 -> 再点即取消
    onFavTap() {
      if (!this.detail || !this.detail.aid || this.actBusy) return
      if (!this.requireLogin()) return
      if (this.detail.reqFav) {
        this.doFavCancel()
        return
      }
      this.actStatus = ''
      this.pickerMode = 'fav'
      this.loadFavFolders()
    },

    closePicker() {
      this.pickerMode = ''
    },

    // 收藏夹列表 (含 favoured 状态), 按 aid 缓存
    loadFavFolders() {
      if (!this.detail || !this.detail.aid) return
      if (this.favFoldersAid === this.detail.aid && this.favFolders.length > 0) return
      const gen = this.generation
      const self = this
      this.favLoading = true
      getFavFolders(this.detail.aid).then(function (list) {
        if (gen !== self.generation) return
        self.favFolders = list
        self.favFoldersAid = self.detail ? self.detail.aid : 0
        self.favLoading = false
      }).catch(function (err) {
        if (gen !== self.generation) return
        self.favLoading = false
        self.favFolders = []
        self.actStatus = '收藏夹加载失败: ' + (err && err.message ? err.message : String(err))
      })
    },

    pickFolder(f) {
      this.closePicker()
      this.doFavAdd(f)
    },

    async doFavAdd(folder) {
      if (!this.detail || !this.detail.aid || this.actBusy) return
      this.actBusy = true
      this.actStatus = '收藏中…'
      try {
        await dealFav(this.detail.aid, folder.id, true)
        this.detail.reqFav = true
        this.favFoldersAid = 0   // 收藏状态变了, 缓存作废
        this.actStatus = '✓ 已收藏·' + folder.title
        this.reloadDetail()
      } catch (err) {
        this.actStatus = '收藏失败: ' + (err && err.message ? err.message : String(err))
      } finally {
        this.actBusy = false
      }
    },

    async doFavCancel() {
      if (!this.detail || !this.detail.aid || this.actBusy) return
      this.actBusy = true
      this.actStatus = '取消收藏中…'
      try {
        // 从 favoured 的收藏夹移除 (缓存没有就先拉一次)
        if (this.favFoldersAid !== this.detail.aid || this.favFolders.length === 0) {
          this.favFolders = await getFavFolders(this.detail.aid)
          this.favFoldersAid = this.detail.aid
        }
        let folder = null
        for (let i = 0; i < this.favFolders.length; i++) {
          if (this.favFolders[i].favoured) { folder = this.favFolders[i]; break }
        }
        if (!folder && this.favFolders.length > 0) folder = this.favFolders[0]
        if (!folder) throw new Error('找不到收藏夹')
        await dealFav(this.detail.aid, folder.id, false)
        this.detail.reqFav = false
        this.favFoldersAid = 0
        this.actStatus = '已取消收藏'
        this.reloadDetail()
      } catch (err) {
        this.actStatus = '取消失败: ' + (err && err.message ? err.message : String(err))
      } finally {
        this.actBusy = false
      }
    },

    // 三连: 点赞 + 投币(1) + 收藏 一次完成 (已做过的步骤跳过; 收藏用默认/已收藏的夹)
    async doTriple() {
      if (!this.detail || !this.detail.aid || this.actBusy) return
      if (!this.requireLogin()) return
      this.actBusy = true
      this.actStatus = '三连中…'
      const aid = this.detail.aid
      const gen = this.generation
      try {
        if (!this.detail.reqLike) {
          await likeVideo(aid, 1)
          this.detail.reqLike = true
        }
        if (!this.detail.reqCoin) {
          await addCoin(aid, 1, true)
          this.detail.reqCoin = true
        }
        if (!this.detail.reqFav) {
          const list = await getFavFolders(aid)
          let folder = null
          for (let i = 0; i < list.length; i++) {
            if (list[i].favoured) { folder = list[i]; break }
          }
          if (!folder && list.length > 0) folder = list[0]
          if (!folder) throw new Error('没有可用收藏夹')
          await dealFav(aid, folder.id, true)
          this.detail.reqFav = true
          this.favFoldersAid = 0
        }
        if (gen !== this.generation) return
        this.actStatus = '✓ 三连成功！'
        this.reloadDetail()
      } catch (err) {
        this.actStatus = '三连失败: ' + (err && err.message ? err.message : String(err))
      } finally {
        this.actBusy = false
      }
    },

    // 稍后再看: 未加入 -> 加入; 已加入 -> 再点即移出
    async doToview() {
      if (!this.detail || !this.detail.aid || this.actBusy) return
      if (!this.requireLogin()) return
      this.actBusy = true
      const want = !this.detail.reqToview
      this.actStatus = want ? '添加中…' : '移除中…'
      try {
        if (want) {
          await addToViewLater(this.detail.aid)
          this.detail.reqToview = true
          this.actStatus = '✓ 已加入稍后再看'
        } else {
          await delToViewLater(this.detail.aid)
          this.detail.reqToview = false
          this.actStatus = '已移出稍后再看'
        }
      } catch (err) {
        this.actStatus = (want ? '添加失败: ' : '移除失败: ') +
          (err && err.message ? err.message : String(err))
      } finally {
        this.actBusy = false
      }
    },

    // ---------- 长文本收起/展开 (:key 强制重建 text 才生效) ----------
    toggleTitle() {
      this.titleExpanded = !this.titleExpanded
    },
    toggleDesc() {
      this.descExpanded = !this.descExpanded
    },
    toggleReply(r) {
      // expanded 在 appendPage 推入时已声明, 是响应式字段; :key 变化重建 richtext
      r.expanded = !r.expanded
    },

    // ---------- 评论区 (内联) ----------
    loadComments(reset) {
      if (!this.detail || !this.detail.aid || this.cLoading) return
      const gen = ++this.cGeneration
      this.cLoading = true
      if (reset) this.cStatus = '加载中…'
      afterPaint(async () => {
        try {
          const r = await getReplies(this.detail.aid, this.pn, BUILTIN_EMOJI, this.sortMode)
          if (gen !== this.cGeneration) return
          if (reset) this.replies = []
          this.appendPage(r)
          this.cLoaded = true
          this.cStatus = ''
        } catch (err) {
          if (gen !== this.cGeneration) return
          console.log('[page] comments error: ' + (err && err.message ? err.message : err))
          this.cStatus = err && err.message ? err.message : String(err)
        } finally {
          if (gen === this.cGeneration) this.cLoading = false
        }
      })
    },

    appendPage(r) {
      const seen = {}
      for (let i = 0; i < this.replies.length; i++) seen[this.replies[i].rpid] = true
      for (let i = 0; i < r.replies.length; i++) {
        const item = r.replies[i]
        if (!seen[item.rpid]) {
          item.expanded = false   // 推入时声明, 保证响应式 (点击展开用)
          this.replies.push(item)
          seen[item.rpid] = true
        }
      }
      this.total = r.total
      this.hasMore = this.replies.length < r.total && r.replies.length > 0
    },

    loadMore() {
      if (this.cLoading || !this.hasMore) return
      this.pn++
      this.loadComments(false)
    },

    // 滚动到底自动续页 (无限滑动)
    onCommentsLoadmore() {
      this.loadMore()
    },

    switchSort(mode) {
      if (this.sortMode === mode || this.cLoading) return
      this.sortMode = mode
      this.replies = []
      this.total = 0
      this.pn = 1
      this.hasMore = false
      this.cGeneration++
      this.cStatus = '加载中…'
      this.loadComments(true)
    },

    openSubReply(r) {
      if (!this.detail) return
      $falcon.navTo('subreply', {
        aid: String(this.detail.aid),
        root: String(r.rpid),
        msg: r.message || '',
        author: r.author || '',
        face: r.face || '',
        count: String(r.replyCount || 0),
        title: this.detail.title || ''
      })
    },

    goLogin() {
      $falcon.navTo('login', {})
    },

    async openPostInput() {
      if (!hasCookie()) {
        this.goLogin()
        return
      }
      if (this.ime == null) this.ime = createIME()
      try {
        const text = await this.ime.open({
          text: '',
          placeholder: '说点什么…',
          maxlength: 500,
          multiLinesEditVisible: false,
          enterButtonText: '发送',
          confirmText: '发送'
        })
        if (text === null || text.trim() === '') return
        await this.postComment(text.trim())
      } catch (err) {
        this.cStatus = '输入失败: ' + (err && err.message ? err.message : err)
      }
    },

    async postComment(message) {
      if (this.posting || !this.detail || !this.detail.aid) return
      this.posting = true
      this.cStatus = '发送中…'
      try {
        await addReply(this.detail.aid, message)
        this.pn = 1
        this.replies = []
        this.cStatus = '✓ 已发送'
        this.loadComments(true)
      } catch (err) {
        this.cStatus = '发送失败: ' + (err && err.message ? err.message : err)
      } finally {
        this.posting = false
      }
    },

    openUp() {
      if (this.detail && this.detail.mid) {
        $falcon.navTo('up', { mid: String(this.detail.mid), name: this.detail.author })
      }
    },

    goBack() {
      this.$page.finish()
    },

    goHome() {
      $falcon.navTo('index', {})
    },

    onUnload() {
      this.generation++
      this.cGeneration++
      if (this.ime) { try { this.ime.destroy() } catch (e) {} }
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
  background-color: #141414;
  flex-direction: row;
  /* 进入动画: 只允许 transform (Falcon transition 不支持 opacity, 0.9.1 加 opacity:0 导致黑屏) */
  transition-property: transform;
  transition-duration: 260ms;
  transition-timing-function: ease-out;
}
/* ---------- 左栏: 封面 ---------- */
.left {
  width: 300px;
  height: 266px;
  flex-direction: column;
  background-color: #000000;
}
.cover {
  width: 300px;
  height: 266px;
}
.cover-ph {
  background-color: #1f1f1f;
}
.dur {
  position: absolute;
  right: 8px;
  top: 232px;
  font-size: 15px;
  color: #ffffff;
  background-color: rgba(0, 0, 0, 0.6);
  padding-left: 6px;
  padding-right: 6px;
}
/* 返回按钮: 左上角悬浮封面之上 (半透明, 不遮太多画面) */
.backbtn {
  position: absolute;
  left: 10px;
  top: 10px;
  width: 96px;
  height: 40px;
  border-radius: 20px;
  background-color: rgba(0, 0, 0, 0.55);
  justify-content: center;
  align-items: center;
}
.backbtn-text {
  font-size: 20px;
  color: #ffffff;
}
/* ---------- 右栏 ---------- */
.right {
  width: 660px;
  height: 266px;
  flex-direction: column;
  background-color: #16181c;
}
.tabbar {
  width: 660px;
  height: 36px;
  flex-direction: row;
  align-items: center;
  background-color: #21242b;
}
.tab {
  width: 96px;
  height: 36px;
  justify-content: center;
  align-items: center;
  margin-left: 8px;
}
.tab-on {
  border-bottom-width: 3px;
  border-bottom-color: #fb7299;
}
.tab-text {
  font-size: 19px;
  color: #8a94a6;
}
.tab-text-on {
  color: #fb7299;
}
.tab-spacer {
  flex: 1;
}
.mini-btn {
  width: 52px;
  height: 32px;
  border-radius: 16px;
  background-color: #37404a;
  justify-content: center;
  align-items: center;
  margin-right: 10px;
}
.mini-text {
  font-size: 20px;
  color: #ffffff;
}
/* ---------- 详情 tab ---------- */
.detail-scroll {
  width: 660px;
  height: 230px;
  flex-direction: column;
  padding-left: 14px;
  padding-right: 14px;
}
.title {
  font-size: 22px;
  color: #ffffff;
  margin-top: 8px;
  lines: 2;
  text-overflow: ellipsis;
  overflow: hidden;
}
/* lines: 0 = 不限行数 (Falcon 文档), 点击展开态 (:key 重建生效) */
.title-open {
  lines: 0;
}
.author {
  font-size: 18px;
  color: #fb7299;
  margin-top: 6px;
}
.stat {
  font-size: 16px;
  color: #888888;
  margin-top: 4px;
}
.btnrow {
  flex-direction: row;
  align-items: center;
  margin-top: 8px;
}
.playbtn {
  width: 160px;
  height: 42px;
  border-radius: 21px;
  background-color: #fb7299;
  justify-content: center;
  align-items: center;
}
.play-text {
  font-size: 20px;
  color: #ffffff;
}
.act-status {
  font-size: 16px;
  color: #e6a23c;
  margin-left: 14px;
  flex: 1;
}
/* 交互行: 赞/币/藏/三连/稍后看 —— 五键均分, 高度 42 好按 */
.actrow {
  flex-direction: row;
  margin-top: 8px;
}
.act-btn {
  flex: 1;
  height: 42px;
  border-radius: 21px;
  background-color: #2a2f38;
  justify-content: center;
  align-items: center;
  margin-left: 4px;
  margin-right: 4px;
}
.act-on {
  background-color: #fb7299;
}
.act-triple {
  background-color: #3d2a35;
}
.act-text {
  font-size: 18px;
  color: #c8d2de;
}
.act-text-on {
  color: #ffffff;
}
.state-inline {
  font-size: 18px;
  color: #e6a23c;
  margin-top: 8px;
}
.section {
  margin-top: 12px;
  flex-direction: column;
}
.sec-title {
  font-size: 18px;
  color: #ffffff;
  margin-bottom: 4px;
}
.desc {
  font-size: 16px;
  color: #a8b2c0;
  lines: 3;
  text-overflow: ellipsis;
}
.desc-open {
  lines: 0;
}
.plist {
  width: 632px;
  height: 44px;
  flex-direction: row;
}
.pitem {
  height: 38px;
  padding-left: 14px;
  padding-right: 14px;
  margin-right: 8px;
  border-radius: 19px;
  background-color: #2a2f38;
  color: #c8d2de;
  font-size: 16px;
  text-align: center;
}
.pitem-active {
  background-color: #fb7299;
  color: #ffffff;
}
.ritem {
  width: 632px;
  flex-direction: row;
  margin-top: 8px;
  background-color: #1f1f1f;
  border-radius: 10px;
}
.rcover {
  width: 150px;
  height: 94px;
  border-top-left-radius: 10px;
  border-bottom-left-radius: 10px;
}
.rmeta {
  width: 470px;
  height: 94px;
  flex-direction: column;
}
.rtitle {
  font-size: 17px;
  color: #ffffff;
  margin-left: 10px;
  margin-top: 6px;
  margin-right: 10px;
  lines: 2;
  text-overflow: ellipsis;
  overflow: hidden;
}
.rstat {
  font-size: 15px;
  color: #888888;
  margin-left: 10px;
  margin-top: 4px;
}
.pull-hint {
  font-size: 14px;
  color: #4a5563;
  text-align: center;
  margin-top: 14px;
  margin-bottom: 10px;
}
/* ---------- 评论 tab ---------- */
.cwrap {
  width: 660px;
  height: 230px;
  flex-direction: column;
}
.sortbar {
  width: 660px;
  height: 32px;
  flex-direction: row;
  align-items: center;
  background-color: #1a1d22;
}
.sort-item {
  width: 92px;
  height: 28px;
  justify-content: center;
  align-items: center;
  margin-left: 10px;
  border-radius: 14px;
}
.sort-on {
  background-color: #2c313a;
}
.sort-text {
  font-size: 17px;
  color: #8a94a6;
}
.sort-text-on {
  color: #fb7299;
}
.clist {
  width: 660px;
  height: 154px;
  flex-direction: column;
  padding-left: 12px;
  padding-right: 12px;
}
.c-status {
  font-size: 17px;
  color: #e6a23c;
  margin-top: 6px;
  margin-bottom: 6px;
}
.reply {
  flex-direction: row;
  padding-top: 8px;
  padding-bottom: 8px;
  border-bottom-width: 1px;
  border-bottom-color: #262b33;
}
.face {
  width: 44px;
  height: 44px;
  border-radius: 22px;
  margin-right: 10px;
}
.reply-main {
  width: 570px;
  flex-direction: column;
}
.reply-head {
  flex-direction: row;
  align-items: center;
  margin-bottom: 2px;
}
.reply-author {
  font-size: 17px;
  color: #8a94a6;
  margin-right: 12px;
}
.reply-time {
  font-size: 15px;
  color: #5c6672;
}
.reply-msg {
  font-size: 18px;
  color: #e8edf3;
  lines: 3;
  margin-top: 2px;
}
.reply-msg-open {
  lines: 0;
}
.reply-meta {
  flex-direction: row;
  align-items: center;
  margin-top: 3px;
}
.meta-text {
  font-size: 15px;
  color: #6a7684;
  padding-top: 6px;
  padding-bottom: 6px;
}
.meta-reply {
  font-size: 16px;
  color: #fb7299;
  margin-left: 16px;
  padding-top: 6px;
  padding-bottom: 6px;
  padding-right: 10px;
}
.load-more {
  font-size: 18px;
  color: #fb7299;
  text-align: center;
  padding-top: 10px;
  padding-bottom: 10px;
}
.empty {
  font-size: 17px;
  color: #6a7684;
  margin-top: 16px;
  text-align: center;
}
.gate {
  width: 636px;
  height: 110px;
  flex-direction: column;
  justify-content: center;
  align-items: center;
}
.gate-text {
  font-size: 19px;
  color: #8a94a6;
  margin-bottom: 12px;
}
.gate-btn {
  width: 300px;
  height: 42px;
  border-radius: 21px;
  background-color: #fb7299;
  justify-content: center;
  align-items: center;
}
.gate-btn-text {
  font-size: 18px;
  color: #ffffff;
}
.postbar {
  width: 660px;
  height: 44px;
  flex-direction: row;
  align-items: center;
  background-color: #21242b;
  padding-left: 12px;
  padding-right: 12px;
}
.post-input {
  width: 500px;
  height: 36px;
  border-radius: 18px;
  background-color: #2a2f38;
  justify-content: center;
  padding-left: 14px;
}
.post-input-text {
  font-size: 18px;
  color: #8a94a6;
}
.post-btn {
  width: 100px;
  height: 36px;
  border-radius: 18px;
  background-color: #fb7299;
  justify-content: center;
  align-items: center;
  margin-left: 10px;
}
.post-btn-text {
  font-size: 18px;
  color: #ffffff;
}
/* 进入动画: 从右滑入 (0.9.1 教训: 别用 opacity, transition 不支持会黑屏) */
.page-enter {
  transform: translateX(960px);
}
/* ---------- 选择器浮层 (收藏夹 / 投币数量) ---------- */
.picker-mask {
  position: absolute;
  left: 0px;
  top: 0px;
  width: 960px;
  height: 266px;
  background-color: rgba(0, 0, 0, 0.65);
  justify-content: center;
  align-items: center;
}
.picker {
  width: 520px;
  background-color: #21242b;
  border-radius: 14px;
  flex-direction: column;
  padding-top: 10px;
  padding-bottom: 10px;
  padding-left: 14px;
  padding-right: 14px;
}
.picker-title {
  font-size: 20px;
  color: #ffffff;
  margin-bottom: 8px;
  text-align: center;
}
.picker-list {
  width: 492px;
  height: 116px;
  flex-direction: column;
}
.picker-state {
  font-size: 17px;
  color: #8a94a6;
  margin-top: 10px;
  text-align: center;
}
.picker-item {
  height: 50px;
  flex-direction: row;
  align-items: center;
  border-bottom-width: 1px;
  border-bottom-color: #2c313a;
}
.picker-item-title {
  font-size: 19px;
  color: #e8edf3;
  flex: 1;
  lines: 1;
  text-overflow: ellipsis;
  overflow: hidden;
}
.picker-item-sub {
  font-size: 16px;
  color: #6a7684;
}
.picker-row {
  flex-direction: row;
  justify-content: center;
  margin-top: 8px;
  margin-bottom: 8px;
}
.picker-coin {
  width: 170px;
  height: 60px;
  border-radius: 30px;
  background-color: #fb7299;
  justify-content: center;
  align-items: center;
  margin-left: 12px;
  margin-right: 12px;
}
.picker-coin-text {
  font-size: 22px;
  color: #ffffff;
}
.picker-cancel {
  height: 44px;
  border-radius: 22px;
  background-color: #2c313a;
  justify-content: center;
  align-items: center;
  margin-top: 8px;
}
.picker-cancel-text {
  font-size: 19px;
  color: #c8d2de;
}
</style>
