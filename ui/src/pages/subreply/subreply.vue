<template>
  <div class="page">
    <div class="topbar">
      <div class="back" @click="goBack">
        <text class="back-text">‹ 返回</text>
      </div>
      <text class="title">全部回复 {{ total > 0 ? total : '' }}</text>
    </div>

    <!-- 父评论 (固定顶部, 点击 = 回复主评论) -->
    <div class="parent" @click="replyToParent">
      <image class="pface" :src="parentFace" resize="cover" v-if="parentFace"></image>
      <div class="pmain">
        <div class="phead">
          <text class="pauthor">{{ parentAuthor }}</text>
        </div>
        <richtext class="pmsg">
          <template v-for="(seg, si) in parentSegs">
            <span v-if="seg.t === 0" :key="'s' + si">{{ seg.v }}</span>
            <image v-else :key="'e' + si" :src="seg.v" :style="{ width: seg.w + 'px', height: seg.h + 'px' }"></image>
          </template>
        </richtext>
      </div>
    </div>

    <scroller class="list" scroll-direction="vertical" :show-scrollbar="true"
              :loadmoreoffset="100" @loadmore="loadMore" @scroll="onListScroll"
              @touchstart="onTouchStart" @touchmove="onTouchMove" @touchend="onTouchEnd">
      <text v-if="status !== ''" class="status">{{ status }}</text>
      <div v-for="r in replies" :key="r.rpid" class="reply">
        <image class="face" :src="r.face" resize="cover"></image>
        <div class="reply-main">
          <div class="reply-head">
            <text class="reply-author">{{ r.author }}</text>
            <text class="reply-time">{{ r.timeText }}</text>
          </div>
          <!-- :key 重建生效: Falcon 的 lines 样式创建后不随 class 更新 -->
          <richtext :key="'r' + r.rpid + (r.expanded ? 1 : 0)"
                    :class="['reply-msg', r.expanded ? 'reply-msg-open' : '']" @click="toggleReply(r)">
            <template v-for="(seg, si) in r.segs">
              <span v-if="seg.t === 0" :key="'s' + si">{{ seg.v }}</span>
              <image v-else :key="'e' + si" :src="seg.v" :style="{ width: seg.w + 'px', height: seg.h + 'px' }"></image>
            </template>
          </richtext>
          <div class="reply-meta">
            <text class="meta-text">赞 {{ r.likeText }}</text>
            <text class="meta-reply" @click="setTarget(r)">回复</text>
          </div>
        </div>
      </div>
      <text v-if="replies.length > 0 && hasMore" class="load-more" @click="loadMore">加载更多回复…</text>
      <text v-if="!loading && replies.length === 0 && status === ''" class="empty">还没有回复</text>
    </scroller>

    <!-- 底部发评栏: 回复目标提示 + IME 输入 -->
    <div class="postbar">
      <div class="post-input" @click="openPostInput">
        <text class="post-input-text">{{ logged ? inputHint : '登录后参与评论' }}</text>
      </div>
      <div class="post-btn" @click="openPostInput">
        <text class="post-btn-text">发送</text>
      </div>
    </div>
  </div>
</template>

<script>
// 楼中页: 某条主评论的子回复列表 (x/v2/reply/reply) + 回复子评论 (addReply root/parent).
// 由评论页「回复 N」navTo 传入: aid(oid), root(顶层 rpid), msg/author/face(父评论展示).
// 点某条回复的「回复」= 设置目标 (发评时 parent=该条 rpid); 点父评论 = 回复主楼 (parent=root).
import { createIME } from '../../services/ime.js'
import { getSubReplies, addReply, parseMessage } from '../../services/bili.js'
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

export default {
  name: 'subreply',
  data() {
    return {
      aid: 0,
      root: 0,
      parentAuthor: '',
      parentFace: '',
      parentSegs: [],
      total: 0,
      replies: [],
      pn: 1,
      hasMore: false,
      loading: false,
      logged: false,
      status: '加载中…',
      posting: false,
      ime: null,
      generation: 0,
      // 当前回复目标: null = 回复主楼; { rpid, author } = 回复某条子回复
      target: null
    }
  },
  computed: {
    inputHint() {
      return this.target ? '回复 @' + this.target.author : '回复主评论…'
    }
  },
  methods: {
    onShow() {
      if (this.$page && !this._newOptionsBound) {
        this._newOptionsBound = true
        const self = this
        this.$page.onNewOptions = function (options) { self.applyOptions(options) }
      }
      const wasLogged = this.logged
      this.logged = hasCookie()
      if (this.logged && !wasLogged && this.root && this.replies.length === 0) {
        this.status = '加载中…'
        this.load(true)
        return
      }
      this.applyOptions((this.$page && this.$page.options) || {})
    },

    onUnload() {
      if (this.ime) { try { this.ime.destroy() } catch (e) {} }
    },

    applyOptions(options) {
      const aid = parseInt(options.aid || '0', 10) || 0
      const root = parseInt(options.root || '0', 10) || 0
      if (root === this.root && (this.replies.length > 0 || this.loading)) return
      this.aid = aid
      this.root = root
      this.parentAuthor = options.author || ''
      this.parentFace = options.face || ''
      this.total = parseInt(options.count || '0', 10) || 0
      // 父评论内容: 用内置 emoji 解析成图文混排段
      this.parentSegs = parseParentSegs(options.msg || '')
      this.replies = []
      this.pn = 1
      this.hasMore = false
      this.target = null
      this.generation++
      this.loading = false
      this.status = '加载中…'
      this.load(true)
    },

    load(reset) {
      if (!this.root || this.loading) return
      const gen = ++this.generation
      this.loading = true
      if (reset) this.status = '加载中…'
      afterPaint(async () => {
        try {
          const r = await getSubReplies(this.aid, this.root, this.pn, BUILTIN_EMOJI)
          if (gen !== this.generation) return
          if (reset) this.replies = []
          for (let i = 0; i < r.replies.length; i++) {
            const item = r.replies[i]
            let dup = false
            for (let j = 0; j < this.replies.length; j++) {
              if (this.replies[j].rpid === item.rpid) { dup = true; break }
            }
            if (!dup) {
              item.expanded = false   // 推入时声明, 保证响应式 (点击展开用)
              this.replies.push(item)
            }
          }
          this.total = r.total
          this.hasMore = this.replies.length < r.total && r.replies.length > 0
          this.status = ''
        } catch (err) {
          if (gen !== this.generation) return
          console.log('[subreply] load error: ' + (err && err.message ? err.message : err))
          this.status = err && err.message ? err.message : String(err)
        } finally {
          if (gen === this.generation) this.loading = false
        }
      })
    },

    loadMore() {
      if (this.loading || !this.hasMore) return
      this.pn++
      this.load(false)
    },

    // ---------- 下拉刷新 (与 index/page 同款 touch 方案) ----------
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
      if (this.touchXY(e) - this._touchY0 > 55) this._pullArmed = true
    },
    onTouchEnd() {
      if (this._pullArmed && this._pullOk && (this._scrollY || 0) <= 2) {
        this._pullArmed = false
        if (!this.loading) {
          this.pn = 1
          this.load(true)
        }
        return
      }
      this._pullArmed = false
    },

    // 点某条子回复的「回复」→ 设为目标
    setTarget(r) {
      this.target = { rpid: r.rpid, author: r.author }
    },

    // 长评论收起/展开 (expanded 在推入时已声明, 响应式)
    toggleReply(r) {
      r.expanded = !r.expanded
    },

    // 点父评论 → 回复主楼 (parent = root)
    replyToParent() {
      this.target = null
    },

    async openPostInput() {
      if (!hasCookie()) {
        $falcon.navTo('login', {})
        return
      }
      if (this.ime == null) this.ime = createIME()
      try {
        const text = await this.ime.open({
          text: '',
          placeholder: this.inputHint,
          maxlength: 500,
          multiLinesEditVisible: false,
          enterButtonText: '发送',
          confirmText: '发送'
        })
        if (text === null || text.trim() === '') return
        await this.postReply(text.trim())
      } catch (err) {
        this.status = '输入失败: ' + (err && err.message ? err.message : err)
      }
    },

    async postReply(message) {
      if (this.posting) return
      this.posting = true
      this.status = '发送中…'
      const parent = this.target ? this.target.rpid : this.root
      try {
        await addReply(this.aid, message, this.root, parent)
        this.target = null
        this.pn = 1
        this.replies = []
        this.status = '✓ 已发送'
        this.load(true)
      } catch (err) {
        this.status = '发送失败: ' + (err && err.message ? err.message : err)
      } finally {
        this.posting = false
      }
    },

    goBack() {
      this.$page.finish()
    }
  }
}

// 父评论内容解析 (无 emote 映射, 只做 unicode emoji -> 内置图): 复用 bili.js 的 parseMessage
function parseParentSegs(msg) {
  return parseMessage(msg, {}, BUILTIN_EMOJI)
}
</script>

<style scoped>
.page {
  position: absolute;
  left: 0px;
  top: 0px;
  width: 960px;
  height: 266px;
  background-color: #16181c;
}
.topbar {
  position: absolute;
  left: 0px;
  top: 0px;
  width: 960px;
  height: 44px;
  flex-direction: row;
  align-items: center;
  background-color: #21242b;
}
.back {
  width: 132px;
  height: 38px;
  margin-left: 12px;
  border-radius: 19px;
  background-color: #37404a;
  justify-content: center;
  align-items: center;
}
.back-text {
  font-size: 22px;
  color: #ffffff;
}
.title {
  font-size: 22px;
  color: #ffffff;
  margin-left: 14px;
  lines: 1;
  text-overflow: ellipsis;
  overflow: hidden;
}
/* 父评论卡片 */
.parent {
  position: absolute;
  left: 0px;
  top: 44px;
  width: 960px;
  height: 66px;
  flex-direction: row;
  align-items: center;
  padding-left: 12px;
  padding-right: 12px;
  background-color: #1a1d22;
  border-bottom-width: 1px;
  border-bottom-color: #262b33;
}
.pface {
  width: 40px;
  height: 40px;
  border-radius: 20px;
  margin-right: 10px;
}
.pmain {
  width: 880px;
  flex-direction: column;
}
.phead {
  flex-direction: row;
  align-items: center;
}
.pauthor {
  font-size: 18px;
  color: #8a94a6;
}
.pmsg {
  font-size: 19px;
  color: #c8d2de;
  lines: 1;
  text-overflow: ellipsis;
}
.list {
  position: absolute;
  left: 0px;
  top: 110px;
  width: 960px;
  height: 112px;
  padding-left: 12px;
  padding-right: 12px;
}
.status {
  font-size: 19px;
  color: #e6a23c;
  margin-top: 8px;
  margin-bottom: 8px;
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
  width: 870px;
  flex-direction: column;
}
.reply-head {
  flex-direction: row;
  align-items: center;
  margin-bottom: 2px;
}
.reply-author {
  font-size: 18px;
  color: #8a94a6;
  margin-right: 12px;
}
.reply-time {
  font-size: 16px;
  color: #5c6672;
}
.reply-msg {
  font-size: 20px;
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
  font-size: 16px;
  color: #6a7684;
  padding-top: 6px;
  padding-bottom: 6px;
}
.meta-reply {
  font-size: 16px;
  color: #fb7299;
  margin-left: 18px;
  padding-top: 6px;
  padding-bottom: 6px;
  padding-right: 12px;
}
.load-more {
  font-size: 19px;
  color: #fb7299;
  text-align: center;
  margin-top: 10px;
  margin-bottom: 10px;
}
.empty {
  font-size: 19px;
  color: #6a7684;
  margin-top: 16px;
  text-align: center;
}
.postbar {
  position: absolute;
  left: 0px;
  top: 222px;
  width: 960px;
  height: 44px;
  flex-direction: row;
  align-items: center;
  background-color: #21242b;
  padding-left: 12px;
  padding-right: 12px;
}
.post-input {
  width: 800px;
  height: 32px;
  border-radius: 16px;
  background-color: #2a2f38;
  justify-content: center;
  padding-left: 14px;
}
.post-input-text {
  font-size: 19px;
  color: #8a94a6;
}
.post-btn {
  width: 110px;
  height: 32px;
  border-radius: 16px;
  background-color: #fb7299;
  justify-content: center;
  align-items: center;
  margin-left: 10px;
}
.post-btn-text {
  font-size: 19px;
  color: #ffffff;
}
</style>
