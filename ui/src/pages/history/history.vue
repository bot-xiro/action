<template>
  <div class="page" :class="entering ? 'page-enter' : ''">
    <div class="topbar">
      <div class="back" @click="goBack">
        <text class="back-text">‹ 返回</text>
      </div>
      <text class="topbar-title">历史记录</text>
    </div>

    <scroller class="list" scroll-direction="vertical" :show-scrollbar="true"
              :loadmoreoffset="100" @loadmore="loadMore" @scroll="onListScroll"
              @touchstart="onTouchStart" @touchmove="onTouchMove" @touchend="onTouchEnd">
      <text v-if="status !== ''" class="state">{{ loading ? ('加载中' + dots) : status }}</text>
      <div v-if="!logged && loaded" class="gate">
        <text class="gate-text">历史记录需要登录后查看</text>
        <div class="gate-btn" @click="goLogin">
          <text class="gate-btn-text">去登录 (扫码 / 电脑同步)</text>
        </div>
      </div>
      <template v-else>
        <div v-for="(item, i) in items" :key="item.bvid || ('h' + i)" class="item" @click="openVideo(item)">
          <image class="cover" :src="item.pic" resize="cover" :lazy-load="true"></image>
          <div class="meta">
            <richtext class="title"><template v-for="(seg, si) in segsOf(item.title)"><span v-if="seg.t === 0" :key="'s' + si">{{ seg.v }}</span><image v-else :key="'e' + si" :src="seg.v" :style="{ width: seg.w + 'px', height: seg.h + 'px' }"></image></template></richtext>
            <text class="up">{{ item.author }}</text>
            <text class="stat">{{ item.progressText }}  {{ item.pubText }}</text>
          </div>
        </div>
        <text v-if="items.length === 0 && !loading && status === ''" class="empty">还没有观看记录</text>
        <text v-if="hasMore && items.length > 0" class="loadmore" @click="loadMore">上滑加载更多…</text>
      </template>
    </scroller>
  </div>
</template>

<script>
// 历史记录页: x/web-interface/history/search (wbi) + 无限滑动 + 下拉刷新
import { getHistoryList , parseMessage } from '../../services/bili.js'
import { hasCookie } from '../../services/auth.js'
import { afterPaint } from '../../base-page.js'

var LOAD_DELAY_MS = 340
var PULL_DY = 55

export default {
  name: 'history',
  data() {
    return {
      items: [],
      status: '加载中…',
      logged: false,
      loaded: false,
      loading: false,
      dots: '',   // 加载动画点 (词典笔不支持 CSS 动画, 用 JS 计时器)
      pn: 1,
      hasMore: false,
      generation: 0,
      entering: true
    }
  },
  methods: {
    startDots() {
      if (this._dotTimer) return
      const self = this
      this._dotTimer = setInterval(function () { self.dots = self.dots.length >= 3 ? '' : self.dots + '.' }, 400)
    },
    stopDots() {
      if (this._dotTimer) { clearInterval(this._dotTimer); this._dotTimer = null }
      this.dots = ''
    },

    // 标题分段: emoji -> CDN 图片 (设备字体没有 emoji 字形, 直接 text 会整片空白)
    segsOf(t) {
      const key = String(t == null ? '' : t)
      if (!this._segsCache) this._segsCache = {}
      let segs = this._segsCache[key]
      if (!segs) {
        try { segs = parseMessage(key, null, null) } catch (e) { segs = [{ t: 0, v: key }] }
        this._segsCache[key] = segs
      }
      return segs
    },
    onShow() {
      this.logged = hasCookie()
      if (!this.loaded) this.load(true)
      if (this.entering) {
        const self = this
        try {
          const p = this.$page
          if (p && p.setTimeout) p.setTimeout(function () { self.entering = false }, 60)
          else setTimeout(function () { self.entering = false }, 60)
        } catch (e) { self.entering = false }
      }
    },

    load(reset) {
      if (!this.logged) {
        this.status = ''
        this.loaded = true
        return
      }
      if (this.loading) return
      const gen = ++this.generation
      this.loading = true; this.startDots()
      if (reset) {
        this.pn = 1
        this.items = []
        this.hasMore = false
        this.status = '加载中…'
      }
      afterPaint(async () => {
        try {
          const r = await getHistoryList(this.pn)
          if (gen !== this.generation) return
          if (reset) this.items = []
          for (let i = 0; i < r.items.length; i++) this.items.push(r.items[i])
          this.hasMore = r.hasMore
          this.loaded = true
          this.status = ''
        } catch (err) {
          if (gen !== this.generation) return
          const msg = err && err.message ? err.message : String(err)
          console.log('[history] error: ' + msg)
          this.status = msg.indexOf('未登录') >= 0 ? '未登录' : msg
          this.loaded = true
        } finally {
          if (gen === this.generation) this.loading = false; this.stopDots()
        }
      }, reset ? LOAD_DELAY_MS : 30)
    },

    loadMore() {
      if (this.loading || !this.hasMore) return
      this.pn++
      this.load(false)
    },

    // ---------- 下拉刷新 ----------
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
        this.load(true)
        return
      }
      this._pullArmed = false
    },

    openVideo(item) {
      if (!item.bvid) return
      $falcon.navTo('page', { bvid: item.bvid, title: item.title })
    },

    goLogin() {
      $falcon.navTo('login', {})
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
  position: absolute;
  left: 0px;
  top: 0px;
  width: 960px;
  height: 266px;
  background-color: #141414;
  flex-direction: column;
  transition-property: transform;
  transition-duration: 260ms;
  transition-timing-function: ease-out;
}
.page-enter {
  transform: translateX(960px);
}
.topbar {
  width: 960px;
  height: 44px;
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
.topbar-title {
  font-size: 24px;
  color: #ffffff;
  margin-left: 24px;
}
.state {
  font-size: 20px;
  color: #999999;
  margin-left: 24px;
  margin-top: 6px;
  width: 100%;
  text-align: center;
}
.list {
  width: 960px;
  flex: 1;
  flex-direction: column;
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
  lines: 1; height: 32px;   /* 单行: 原 lines:2 配固定 112px 的 .meta, 播放量会被挤出卡片 */
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
.loadmore {
  font-size: 20px;
  color: #fb7299;
  text-align: center;
  margin-top: 12px;
  margin-bottom: 12px;
}
.gate {
  width: 960px;
  height: 140px;
  flex-direction: column;
  justify-content: center;
  align-items: center;
}
.gate-text {
  font-size: 20px;
  color: #8a94a6;
  margin-bottom: 14px;
}
.gate-btn {
  width: 320px;
  height: 46px;
  border-radius: 23px;
  background-color: #fb7299;
  justify-content: center;
  align-items: center;
}
.gate-btn-text {
  font-size: 20px;
  color: #ffffff;
}
</style>
