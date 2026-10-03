<template>
  <div class="fpage">
    <div class="ftop">
      <div class="fback" @click="back"><text class="fback-t">‹ 返回</text></div>
      <text class="ftitle">动态</text>
      <div class="cats">
        <div v-for="(c, ci) in cats" :key="'c' + ci"
             :class="['cat', cat === c.k ? 'cat-on' : '']" @click="setCat(c.k)">
          <text :class="['cat-t', cat === c.k ? 'cat-t-on' : '']">{{ c.n }}</text>
        </div>
      </div>
    </div>

    <scroller class="fscroll" scroll-direction="vertical" :show-scrollbar="true"
              :over-scroll="40" :loadmoreoffset="120" @loadmore="loadMore">
      <div class="fwrap">
        <div class="status" v-if="status !== ''">{{ status }}</div>

        <div class="dyn" v-for="(d, di) in shown" :key="d.id || ('d' + di)">
          <div class="dhead">
            <image v-if="d.face" class="dface" :src="d.face" resize="cover"></image>
            <div v-else class="dface dface-ph"><text class="dface-t">{{ d.author ? d.author.charAt(0) : '?' }}</text></div>
            <text class="dauthor">{{ d.author }}</text>
            <text class="dtime">{{ d.pubText }}</text>
            <text class="dbadge">{{ kindName(d.kind) }}</text>
          </div>

          <richtext :class="['dtext', d.expanded ? 'dtext-open' : '']" @click="toggle(d)">
            <template v-for="(seg, si) in d.segs">
              <span v-if="seg.t === 0" :key="'s' + si">{{ seg.v }}</span>
              <span v-else-if="seg.t === 2" :key="'h' + si" class="dhl">{{ seg.v }}</span>
              <image v-else :key="'e' + si" :src="seg.v" :style="{ width: seg.w + 'px', height: seg.h + 'px' }"></image>
            </template>
          </richtext>
          <div v-if="d.segs && d.segs.length > 0 && !d.expanded" class="dmore" @click="toggle(d)">
            <text class="dmore-t">展开全文 ▾</text>
          </div>

          <div class="pics" v-if="d.rows && d.rows.length">
            <div class="pic-row" v-for="(row, ri) in d.rows" :key="'r' + ri">
              <div class="pic-box" v-for="(p, pi) in row" :key="'p' + ri + '_' + pi"
                   :style="{ width: p.w + 'px', height: p.h + 'px' }" @click="openPic(p)">
                <image class="pic-img" :src="p.src" @click="openPic(p)"
                       :style="{ width: p.w + 'px', height: p.h + 'px' }" resize="cover"></image>
              </div>
            </div>
          </div>

          <div class="vcard" v-if="d.archive" @click="openVideo(d.archive)">
            <image class="vcover" :src="d.archive.cover" resize="cover"></image>
            <div class="vmeta">
              <text class="vtitle">{{ d.archive.title }}</text>
              <text class="vstat">{{ '▶' + d.archive.playText + '   ' + d.archive.duration }}</text>
            </div>
          </div>

          <div class="ocard" v-if="d.opus">
            <text class="otitle">{{ d.opus.title }}</text>
            <text class="osum" v-if="d.opus.summary">{{ d.opus.summary }}</text>
          </div>

          <div class="ostat" v-if="d.orig">
            <text class="olabel">{{ '转发 @' + d.orig.author + '：' }}</text>
            <richtext class="dtext">
              <template v-for="(seg, si) in d.orig.segs">
                <span v-if="seg.t === 0" :key="'os' + si">{{ seg.v }}</span>
                <span v-else-if="seg.t === 2" :key="'oh' + si" class="dhl">{{ seg.v }}</span>
                <image v-else :key="'oe' + si" :src="seg.v" :style="{ width: seg.w + 'px', height: seg.h + 'px' }"></image>
              </template>
            </richtext>
            <div class="pics" v-if="d.orig.rows && d.orig.rows.length">
              <div class="pic-row" v-for="(row, ri) in d.orig.rows" :key="'or' + ri">
                <div class="pic-box" v-for="(p, pi) in row" :key="'op' + ri + '_' + pi"
                     :style="{ width: p.w + 'px', height: p.h + 'px' }" @click="openPic(p)">
                  <image class="pic-img" :src="p.src"
                         :style="{ width: p.w + 'px', height: p.h + 'px' }" resize="cover"></image>
                </div>
              </div>
            </div>
            <div class="vcard" v-if="d.orig.archive" @click="openVideo(d.orig.archive)">
              <image class="vcover" :src="d.orig.archive.cover" resize="cover"></image>
              <div class="vmeta">
                <text class="vtitle">{{ d.orig.archive.title }}</text>
                <text class="vstat">{{ '▶' + d.orig.archive.playText + '   ' + d.orig.archive.duration }}</text>
              </div>
            </div>
          </div>

          <div class="dfoot">
            <text class="dfoot-t">{{ '赞 ' + d.stat.like }}</text>
            <text class="dfoot-t">{{ '评论 ' + d.stat.reply }}</text>
            <text class="dfoot-t">{{ '转发 ' + d.stat.forward }}</text>
          </div>
        </div>

        <div class="loadmore" v-if="hasMore" @click="loadMore">
          <text class="loadmore-t">{{ loading ? '加载中…' : '加载更多动态' }}</text>
        </div>
        <div class="empty" v-if="!loading && shown.length === 0">
          <text class="empty-t">{{ status !== '' ? status : '这个分类下暂时没有动态' }}</text>
        </div>
      </div>
    </scroller>

    <div v-if="viewer.on" class="iview">
      <image class="iview-img" :src="viewer.path" resize="cover"
             @touchstart="ivStart" @touchmove="ivMove" @touchend="ivEnd"></image>
      <div class="iview-bar">
        <div class="iview-btn" @click="ivZoom(0.6667)"><text class="iview-btn-t">−</text></div>
        <text class="iview-zoom">{{ viewer.zoomText }}</text>
        <div class="iview-btn" @click="ivZoom(1.5)"><text class="iview-btn-t">＋</text></div>
        <text class="iview-size">{{ viewer.w + '×' + viewer.h }}</text>
        <div class="iview-btn" @click="ivOne"><text class="iview-btn-t">1:1</text></div>
        <div class="iview-btn" @click="ivReset"><text class="iview-btn-t">复位</text></div>
        <div class="iview-btn iview-close" @click="ivClose"><text class="iview-btn-t">关闭</text></div>
      </div>
    </div>
  </div>
</template>

<script>
import { getDynamicFeed } from '../../services/bili.js'
import { log } from '../../services/log.js'
import { bigUrl, fitZoom, clampZoom, clampCenter, VIEW_W, VIEW_H } from '../../services/imageview.js'
import { imageviewer } from 'imageviewer'

const CATS = [
  { k: 'all', n: '全部' },
  { k: 'av', n: '投稿' },
  { k: 'draw', n: '图文' },
  { k: 'word', n: '文字' },
  { k: 'forward', n: '转发' },
  { k: 'opus', n: '专栏' }
]
const KIND_NAME = { av: '投稿', draw: '图文', word: '文字', opus: '专栏', forward: '转发', live: '直播', other: '动态' }

function chunk(arr, n) {
  const out = []
  for (let i = 0; i < arr.length; i += n) out.push(arr.slice(i, i + n))
  return out
}

export default {
  data() {
    return {
      cats: CATS,
      cat: 'all',
      items: [],
      offset: '',
      hasMore: false,
      loading: false,
      status: '加载中…',
      viewer: { on: false, path: '', zoom: 1, cx: 0, cy: 0, w: 0, h: 0, zoomText: '100%' }
    }
  },
  computed: {
    // 分类筛选: 投稿 / 图文 / 文字 / 转发 / 专栏
    shown() {
      if (this.cat === 'all') return this.items
      const out = []
      for (let i = 0; i < this.items.length; i++) {
        if (this.items[i].kind === this.cat) out.push(this.items[i])
      }
      return out
    }
  },
  methods: {
    // 生命周期: BasePage 只把 onShow/onHide/onUnload 转发给页面根组件
    onShow() {
      if (this.$page && !this._newOptionsBound) {
        this._newOptionsBound = true
        const self = this
        this.$page.onNewOptions = function () { self.load(true) }
      }
      if (this._started) return
      this._started = true
      this.load(true)
    },
    back() { try { this.$page.finish() } catch (e) {} },
    kindName(k) { return KIND_NAME[k] || '动态' },
    setCat(k) {
      if (this.cat === k) return
      this.cat = k
      try { log('动态页', '切换分类 ' + k) } catch (e) {}
    },
    async load(reset) {
      if (this.loading) return
      if (!reset && !this.hasMore) return
      this.loading = true
      if (reset) this.status = '加载中…'
      const gen = ++this._gen
      try {
        const r = await getDynamicFeed(reset ? '' : this.offset)
        if (gen !== this._gen) return
        const add = r.items || []
        for (let i = 0; i < add.length; i++) {
          add[i].rows = chunk(add[i].pics || [], 3)
          if (add[i].orig) add[i].orig.rows = chunk(add[i].orig.pics || [], 3)
          add[i].expanded = false
        }
        if (reset) this.items = []
        for (let i = 0; i < add.length; i++) this.items.push(add[i])
        this.offset = r.offset || ''
        this.hasMore = !!r.hasMore
        this.status = this.items.length === 0 ? '关注的 UP 主暂无动态' : ''
        let nd = 0
        for (let i = 0; i < this.items.length; i++) { if (this.items[i].kind === 'draw') nd++ }
        try { log('动态页', '加载完成 ' + this.items.length + ' 条 (图文 ' + nd + ' / offset=' + this.offset + ')') } catch (e) {}
      } catch (e) {
        if (gen !== this._gen) return
        this.status = (e && e.message) ? e.message : String(e)
        try { log('动态页', '加载失败 ' + this.status) } catch (e2) {}
      } finally {
        if (gen === this._gen) this.loading = false
      }
    },
    loadMore() { if (this.loading || !this.hasMore) return; this.load(false) },
    toggle(d) { d.expanded = !d.expanded },
    openVideo(a) {
      if (!a || !a.bvid) return
      try { $falcon.navTo('page', { bvid: a.bvid, title: a.title }) } catch (e) {}
    },
    openPic(p) { if (p && p.full) this.ivOpen(p.full) },
    ivOpen(url) {
      try {
        const info = imageviewer.open(bigUrl(url))
        const o = typeof info === 'string' ? JSON.parse(info) : info
        if (!o || o.ret !== 0) { this.status = '打开图片失败'; return }
        this.viewer.w = o.width || 0
        this.viewer.h = o.height || 0
        this.viewer.zoom = fitZoom(this.viewer.w, this.viewer.h, VIEW_W, VIEW_H)
        this.viewer.cx = this.viewer.w / 2
        this.viewer.cy = this.viewer.h / 2
        this.viewer.on = true
        this.ivRender()
      } catch (e) { this.status = '打开图片失败: ' + ((e && e.message) ? e.message : e) }
    },
    ivRender() {
      try {
        const vw = VIEW_W / this.viewer.zoom
        const vh = VIEW_H / this.viewer.zoom
        this.viewer.cx = clampCenter(this.viewer.cx, vw, this.viewer.w)
        this.viewer.cy = clampCenter(this.viewer.cy, vh, this.viewer.h)
        const path = imageviewer.view(this.viewer.cx, this.viewer.cy, this.viewer.zoom, VIEW_W, VIEW_H)
        if (path) this.viewer.path = String(path)
        this.viewer.zoomText = Math.round(this.viewer.zoom * 100) + '%'
      } catch (e) {}
    },
    ivZoom(f) {
      this.viewer.zoom = clampZoom(this.viewer.zoom * f, this.viewer.w, this.viewer.h)
      this.ivRender()
    },
    ivOne() {
      this.viewer.zoom = clampZoom(1, this.viewer.w, this.viewer.h)
      this.ivRender()
    },
    ivReset() {
      this.viewer.zoom = fitZoom(this.viewer.w, this.viewer.h, VIEW_W, VIEW_H)
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
.fpage { position: absolute; left: 0px; top: 0px; width: 960px; height: 266px; background-color: #14161a; }
.ftop { position: absolute; left: 0px; top: 0px; width: 960px; height: 44px; flex-direction: row; align-items: center; background-color: #1b1e24; }
.fback { padding-left: 16px; padding-right: 14px; height: 40px; justify-content: center; }
.fback-t { font-size: 21px; color: #cfd5de; }
.ftitle { font-size: 19px; color: #e6eaf0; margin-right: 16px; }
.cats { flex-direction: row; flex: 1; }
.cat { padding-left: 12px; padding-right: 12px; height: 28px; border-radius: 6px; margin-right: 8px; background-color: #232830; justify-content: center; }
.cat-on { background-color: #fb7299; }
.cat-t { font-size: 17px; color: #aab2bd; }
.cat-t-on { color: #ffffff; }
.fscroll { position: absolute; left: 0px; top: 44px; width: 960px; height: 222px; }
.fwrap { padding-left: 20px; padding-right: 20px; padding-bottom: 12px; }
.status { font-size: 17px; color: #8a93a0; text-align: center; padding-top: 14px; padding-bottom: 6px; }
.dyn { width: 920px; margin-top: 10px; padding-left: 12px; padding-right: 12px; padding-top: 10px; padding-bottom: 10px; background-color: #1f1f1f; border-radius: 12px; }
.dhead { flex-direction: row; align-items: center; }
.dface { width: 40px; height: 40px; border-radius: 20px; margin-right: 10px; background-color: #232830; }
.dface-ph { justify-content: center; align-items: center; }
.dface-t { font-size: 18px; color: #7c8592; }
.dauthor { font-size: 18px; color: #8fb8ff; }
.dtime { font-size: 15px; color: #7c8592; margin-left: 10px; }
.dbadge { font-size: 15px; color: #ffffff; background-color: #fb7299; padding-left: 8px; padding-right: 8px; padding-top: 2px; padding-bottom: 2px; border-radius: 6px; margin-left: 10px; }
.dtext { font-size: 19px; color: #dfe4ea; lines: 3; margin-top: 4px; }
.dtext-open { lines: 99; }
.dhl { color: #8fb8ff; }
.dmore { padding-top: 6px; padding-bottom: 6px; }
.dmore-t { font-size: 16px; color: #8fb8ff; }
.pics { margin-top: 6px; }
.pic-row { flex-direction: row; }
.pic-box { margin-right: 6px; margin-bottom: 6px; border-radius: 8px; background-color: #232830; }
.pic-img { border-radius: 8px; }
.vcard { flex-direction: row; margin-top: 6px; padding: 8px; background-color: #262b33; border-radius: 8px; }
.vcover { width: 160px; height: 100px; border-radius: 6px; margin-right: 10px; }
.vmeta { flex: 1; }
.vtitle { font-size: 18px; color: #ffffff; lines: 2; }
.vstat { font-size: 16px; color: #888888; margin-top: 6px; }
.ocard { margin-top: 6px; padding: 8px; background-color: #262b33; border-radius: 8px; }
.otitle { font-size: 18px; color: #ffffff; lines: 2; }
.osum { font-size: 17px; color: #aab2bd; lines: 2; margin-top: 4px; }
.ostat { margin-top: 6px; padding: 8px; background-color: #1a1d22; border-radius: 8px; }
.olabel { font-size: 17px; color: #8fb8ff; }
.dfoot { flex-direction: row; margin-top: 8px; }
.dfoot-t { font-size: 16px; color: #9aa3af; margin-right: 20px; }
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
.iview-size { font-size: 16px; color: #9aa3af; margin-right: 12px; }
</style>
