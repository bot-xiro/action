<template>
  <div class="cpage">
    <!-- 顶栏: 返回 + 标题 + 评论数 -->
    <div class="ctop">
      <div class="cback" @click="back"><text class="cback-t">‹ 返回</text></div>
      <text class="ctitle">{{ title || '评论' }}</text>
      <text class="ccount">{{ total > 0 ? total : '' }}</text>
    </div>

    <scroller class="cscroll" scroll-direction="vertical" :show-scrollbar="true"
             :over-scroll="40"
             @scroll="onScroll">
      <div class="cwrap">
        <div class="status" v-if="status !== ''">{{ status }}</div>
        <div class="sortbar">
          <div :class="['sort-item', sortMode === 'hot' ? 'sort-on' : '']" @click="setSort('hot')">
            <text :class="['sort-text', sortMode === 'hot' ? 'sort-text-on' : '']">热度</text>
          </div>
          <div :class="['sort-item', sortMode === 'time' ? 'sort-on' : '']" @click="setSort('time')">
            <text :class="['sort-text', sortMode === 'time' ? 'sort-text-on' : '']">最新</text>
          </div>
        </div>

        <div class="reply" v-for="(r, ri) in replies" :key="r.rpid">
          <image v-if="r.face" class="face" :src="r.face" resize="cover" @click="openUser(r)"></image>
          <div class="rbody">
            <div class="rhead">
              <text class="rauthor" @click="openUser(r)">{{ r.author }}</text>
              <text v-if="r.pinned" class="tag tag-pin">置顶</text>
              <text v-if="r.isUp" class="tag tag-up">UP主</text>
              <text class="rtime">{{ r.timeText }}</text>
            </div>
            <div class="rwrap">
              <richtext :class="['rmsg', r.expanded ? 'rmsg-open' : '']" @click="toggle(r)">
                <template v-for="(seg, si) in r.segs">
                  <span v-if="seg.t === 0" :key="'s' + si">{{ seg.v }}</span>
                  <image v-else :key="'e' + si" :src="seg.v" :style="{ width: seg.w + 'px', height: seg.h + 'px' }"></image>
                </template>
              </richtext>
              <text v-if="!r.expanded && r.long" class="rmore" @click="toggle(r)">…</text>
            </div>
            <div v-if="r.pics && r.pics.length" class="pics">
              <image v-for="(pic, pi) in r.pics" :key="'p' + pi" class="pic" :src="pic.src"
                     :style="{ width: pic.w + 'px', height: pic.h + 'px' }" resize="cover"></image>
            </div>
            <div class="rmeta">
              <text :class="['mtext', r.liked ? 'mliked' : '']" @click="like(r)">赞 {{ r.likeText }}{{ r.liked ? ' ✓' : '' }}</text>
              <text class="mreply" @click="openSub(r)">回复 {{ r.replyCount }}</text>
              <text v-if="r.pics && r.pics.length" class="mpic" @click="openPic(r)">图 {{ r.pics.length }}</text>
            </div>
          </div>
        </div>

        <div class="loadmore" v-if="replies.length > 0" @click="loadMore">
          <text class="loadmore-t">{{ loading ? '加载中…' : '加载更多评论' }}</text>
        </div>
        <div class="empty" v-if="!loading && replies.length === 0">
          <text class="empty-t">{{ status || '还没有评论' }}</text>
        </div>
      </div>
    </scroller>


    <!-- 图片查看器 (独立 so) -->
    <div v-if="viewer.on" class="iview">
      <image class="iview-img" :src="viewer.path" resize="cover"
             @touchstart="ivStart" @touchmove="ivMove" @touchend="ivEnd"></image>
      <div class="iview-bar">
        <div class="iview-btn" @click="ivZoom(0.5)"><text class="iview-btn-t">−</text></div>
        <text class="iview-zoom">{{ viewer.zoomText }}</text>
        <div class="iview-btn" @click="ivZoom(2)"><text class="iview-btn-t">＋</text></div>
        <div class="iview-btn" @click="ivReset"><text class="iview-btn-t">复位</text></div>
        <div class="iview-btn iview-close" @click="ivClose"><text class="iview-btn-t">关闭</text></div>
      </div>
    </div>
  </div>
</template>

<script>
import { getReplies, likeReply, addReply } from '../../services/bili.js'
import { imageviewer } from 'imageviewer'

const BUILTIN_EMOJI = {}
var PULL_DY = 55

export default {
  data() {
    return {
      aid: '',
      title: '',
      total: 0,
      pn: 1,
      sortMode: 'hot',
      replies: [],
      loading: false,
      status: '加载中…',
      draft: '',
      posting: false,
      scrollY: 0,
      viewer: { on: false, path: '', zoom: 1, cx: 0, cy: 0, w: 0, h: 0, zoomText: '100%' }
    }
  },
  created() {
    const o = this.$page.options || {}
    this.aid = o.aid || ''
    this.title = o.title ? ('评论 · ' + o.title) : '评论'
    this.total = Number(o.total || 0)
    log('评论页', '打开 aid=' + this.aid)
    this.load(true)
  },
  methods: {
    back() { try { this.$page.finish() } catch (e) {} },
    async load(reset, fresh) {
      if (!this.aid || this.loading) return
      this.loading = true
      if (reset) { this.pn = 1; this.status = '加载中…' }
      try {
        const r = await getReplies(this.aid, this.pn, BUILTIN_EMOJI, this.sortMode, fresh)
        if (reset) this.replies = []
        const seen = {}
        for (let i = 0; i < this.replies.length; i++) seen[this.replies[i].rpid] = true
        for (let i = 0; i < r.replies.length; i++) {
          const it = r.replies[i]
          if (seen[it.rpid]) continue
          it.expanded = false
          this.replies.push(it)
          seen[it.rpid] = true
        }
        this.total = r.total || this.total
        this.status = this.replies.length === 0 ? '还没有评论' : ''
        log('评论页', '加载完成 ' + this.replies.length + ' 条 (total=' + this.total + ')')
      } catch (e) {
        this.status = (e && e.message) ? e.message : String(e)
        log('评论页', '加载失败 ' + this.status)
      } finally {
        this.loading = false
      }
    },
    setSort(m) { if (m === this.sortMode) return; this.sortMode = m; this.load(true, true) },
    loadMore() { if (this.loading) return; this.pn = this.pn + 1; this.load(false) },
    onScroll(e) { try { const c = e && e.contentOffset; if (c) this.scrollY = c.y || 0 } catch (err) {} },
    toggle(r) { r.expanded = !r.expanded },
    async like(r) {
      if (this._likeBusy) return
      this._likeBusy = true
      const want = !r.liked
      try {
        await likeReply(this.aid, r.rpid, want)
        r.liked = want
        r.likeText = String(want ? (parseInt(r.likeText || '0', 10) || 0) + 1 : Math.max(0, (parseInt(r.likeText || '0', 10) || 0) - 1))
      } catch (e) { this.status = (e && e.message) ? e.message : '点赞失败' }
      this._likeBusy = false
    },
    openUser(r) { if (r.mid) { try { $falcon.navTo('up', { mid: String(r.mid), name: r.author }) } catch (e) {} } },
    openSub(r) { try { $falcon.navTo('subreply', { aid: this.aid, root: String(r.rpid), count: String(r.replyCount || 0), msg: r.message || '', author: r.author }) } catch (e) {} },
    openPic(r) { if (r.pics && r.pics.length) this.ivOpen(r.pics[0].src) },
    onInput(e) { try { this.draft = e.detail && e.detail.value !== undefined ? e.detail.value : (e.target && e.target.value) || '' } catch (err) {} },
    ivOpen(url) {
      try {
        const info = imageviewer.open(url)
        const o = typeof info === 'string' ? JSON.parse(info) : info
        if (!o || o.ret !== 0) { this.status = '打开图片失败'; return }
        this.viewer.on = true
        this.viewer.w = o.width || 0
        this.viewer.h = o.height || 0
        this.viewer.cx = this.viewer.w / 2
        this.viewer.cy = this.viewer.h / 2
        this.viewer.zoom = this.viewer.h > 0 ? Math.max(0.2, Math.min(4, 266 / this.viewer.h)) : 1
        this.ivRender()
      } catch (e) { this.status = '打开图片失败: ' + ((e && e.message) ? e.message : e) }
    },
    ivRender() {
      try {
        const path = imageviewer.view(this.viewer.cx, this.viewer.cy, this.viewer.zoom, 960, 266)
        if (path) this.viewer.path = String(path)
        this.viewer.zoomText = Math.round(this.viewer.zoom * 100) + '%'
      } catch (e) {}
    },
    ivZoom(f) { let z = this.viewer.zoom * f; if (z < 0.1) z = 0.1; if (z > 8) z = 8; this.viewer.zoom = z; this.ivRender() },
    ivReset() {
      this.viewer.zoom = this.viewer.h > 0 ? Math.max(0.2, Math.min(4, 266 / this.viewer.h)) : 1
      this.viewer.cx = this.viewer.w / 2
      this.viewer.cy = this.viewer.h / 2
      this.ivRender()
    },
    ivClose() { this.viewer.on = false; try { imageviewer.close() } catch (e) {} },
    txy(e) {
      try {
        const t = (e && e.changedTouches && e.changedTouches[0]) || (e && e.touches && e.touches[0])
        if (t && typeof t.pageY === 'number') return { x: t.pageX, y: t.pageY, ok: true }
      } catch (err) {}
      return { x: 0, y: 0, ok: false }
    },
    ivStart(e) { const p = this.txy(e); this._ix = p.ok ? p.x : null; this._iy = p.ok ? p.y : null },
    ivMove(e) {
      const p = this.txy(e)
      if (!p.ok || this._ix === null || this._ix === undefined) return
      const dx = p.x - this._ix, dy = p.y - this._iy
      if (Math.abs(dx) < 2 && Math.abs(dy) < 2) return
      this.viewer.cx -= dx / this.viewer.zoom
      this.viewer.cy -= dy / this.viewer.zoom
      this._ix = p.x; this._iy = p.y
      this.ivRender()
    },
    ivEnd() { this._ix = undefined; this._iy = undefined }
  }
}
</script>

<style scoped>
.cpage { position: absolute; left: 0px; top: 0px; width: 960px; height: 266px; background-color: #14161a; }
.ctop { position: absolute; left: 0px; top: 0px; width: 960px; height: 44px; flex-direction: row; align-items: center; background-color: #1b1e24; }
.cback { padding-left: 16px; padding-right: 16px; height: 40px; justify-content: center; }
.cback-t { font-size: 21px; color: #cfd5de; }
.ctitle { font-size: 19px; color: #e6eaf0; flex: 1; lines: 1; overflow: hidden; }
.ccount { font-size: 17px; color: #fb7299; padding-right: 18px; }
.cscroll { position: absolute; left: 0px; top: 44px; width: 960px; height: 178px; }
.cwrap { padding-left: 14px; padding-right: 14px; padding-bottom: 10px; }
.status { font-size: 17px; color: #8a93a0; text-align: center; padding-top: 14px; padding-bottom: 8px; }
.sortbar { flex-direction: row; margin-top: 6px; margin-bottom: 6px; }
.sort-item { padding-left: 14px; padding-right: 14px; height: 28px; border-radius: 6px; margin-right: 10px; background-color: #232830; justify-content: center; }
.sort-on { background-color: #fb7299; }
.sort-text { font-size: 17px; color: #aab2bd; }
.sort-text-on { color: #ffffff; }
.reply { flex-direction: row; padding-top: 8px; padding-bottom: 8px; }
.face { width: 44px; height: 44px; border-radius: 22px; margin-right: 10px; background-color: #232830; }
.rbody { flex: 1; }
.rhead { flex-direction: row; align-items: center; }
.rauthor { font-size: 18px; color: #8fb8ff; lines: 1; }
.rtime { font-size: 15px; color: #7c8592; margin-left: 10px; }
.tag { font-size: 15px; padding-left: 8px; padding-right: 8px; padding-top: 2px; padding-bottom: 2px; border-radius: 6px; margin-left: 8px; justify-content: center; }
.tag-pin { background-color: #fb7299; color: #ffffff; }
.tag-up { background-color: #2f80ed; color: #ffffff; }
.rwrap { position: relative; margin-top: 2px; }
.rmsg { font-size: 19px; color: #dfe4ea; lines: 3; }
.rmsg-open { lines: 99; }
.rmore { position: absolute; right: 0px; bottom: 0px; font-size: 19px; color: #8fb8ff; background-color: #14161a; }
.pics { flex-direction: row; margin-top: 6px; }
.pic { margin-right: 8px; border-radius: 8px; }
.rmeta { flex-direction: row; align-items: center; margin-top: 4px; }
.mtext { font-size: 16px; color: #9aa3af; padding-top: 6px; padding-bottom: 6px; margin-right: 20px; }
.mliked { color: #fb7299; }
.mreply { font-size: 16px; color: #9aa3af; padding-top: 6px; padding-bottom: 6px; margin-right: 20px; }
.mpic { font-size: 16px; color: #fb7299; background-color: #2b2f36; padding-left: 12px; padding-right: 12px; padding-top: 3px; padding-bottom: 3px; border-radius: 6px; }
.loadmore { height: 40px; justify-content: center; }
.loadmore-t { font-size: 17px; color: #8fb8ff; }
.empty { margin-top: 20px; justify-content: center; }
.empty-t { font-size: 18px; color: #8a93a0; }
.iview { position: absolute; left: 0px; top: 0px; width: 960px; height: 266px; background-color: #000000; z-index: 200; }
.iview-img { position: absolute; left: 0px; top: 0px; width: 960px; height: 266px; }
.iview-bar { position: absolute; left: 0px; bottom: 0px; width: 960px; height: 44px; flex-direction: row; align-items: center; background-color: rgba(0,0,0,0.72); padding-left: 10px; }
.iview-btn { padding-left: 16px; padding-right: 16px; padding-top: 6px; padding-bottom: 6px; background-color: #2f3238; border-radius: 8px; margin-right: 10px; justify-content: center; }
.iview-close { background-color: #fb7299; }
.iview-btn-t { font-size: 19px; color: #ffffff; }
.iview-zoom { font-size: 19px; color: #fb7299; margin-right: 10px; }
</style>
