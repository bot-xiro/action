<template>
  <div class="page">
    <div class="topbar">
      <div class="back" @click="goBack">
        <text class="back-text">‹ 返回</text>
      </div>
      <text class="title">{{ titleText }} 的评论</text>
      <text class="count" v-if="total > 0">{{ total }} 条</text>
    </div>

    <scroller class="list" scroll-direction="vertical" :show-scrollbar="true">
      <text v-if="status !== ''" class="status">{{ status }}</text>
      <!-- 未登录: 明确给出登录引导 (用户反馈: 未登录状态无法获取评论) -->
      <div v-if="!logged && !loading" class="gate">
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
            <!-- 图文混排: B 站表情 + emoji 转图片 (设备字体无 emoji 字形) -->
            <richtext class="reply-msg">
              <template v-for="(seg, si) in r.segs">
                <span v-if="seg.t === 0" :key="'s' + si">{{ seg.v }}</span>
                <image v-else :key="'e' + si" :src="seg.v" :style="{ width: seg.w + 'px', height: seg.h + 'px' }"></image>
              </template>
            </richtext>
            <!-- 赞/回复: 内置图标 (设备字体无 emoji 字形, 固定图标走打包文件) -->
            <div class="reply-meta">
              <image class="meta-icon" :src="likeIcon"></image>
              <text class="meta-text">{{ r.likeText }}</text>
              <image class="meta-icon reply-ic-gap" :src="replyIcon"></image>
              <text class="meta-text">{{ r.replyCount }}</text>
            </div>
          </div>
        </div>
      </div>
      <text v-if="logged && replies.length > 0 && hasMore" class="load-more" @click="loadMore">加载更多评论…</text>
      <text v-if="logged && !loading && replies.length === 0 && status === ''" class="empty">还没有评论, 抢首评</text>
    </scroller>

    <!-- 底部发评栏: 登录后可发 -->
    <div class="postbar">
      <div class="post-input" @click="openPostInput">
        <text class="post-input-text">{{ logged ? '说点什么…' : '登录后参与评论' }}</text>
      </div>
      <div class="post-btn" @click="openPostInput">
        <text class="post-btn-text">发送</text>
      </div>
    </div>
  </div>
</template>

<script>
// 评论页: 视频评论列表 (x/v2/reply, 匿名可读) + 发评论 (登录 Cookie + csrf).
// aid 由详情页 navTo 传入; 分页 pn 递增, 新评论插到当前列表尾部.
import { createIME } from '../../services/ime.js'
import { getReplies, addReply } from '../../services/bili.js'
import { hasCookie } from '../../services/auth.js'
import { afterPaint } from '../../base-page.js'

// 内置常用 emoji 映射: .vue 文件里的 require png 会被 aiot-cli 编译期处理成 images/<hash>.png,
// .js 文件 (services/) 里的 require 不会被处理, QuickJS 运行时无 require 会崩掉整个应用.
// key 为 twemoji 文件名 (不带 .png); B 站 emote (content.emote) 有映射的优先走 B 站 CDN 图.
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
  name: 'comment',
  data() {
    return {
      aid: 0,
      titleText: '',
      // 内置图标: CLI 编译时打包进应用 (require 本地文件)
      likeIcon: require('../../assets/icon/like.png'),
      replyIcon: require('../../assets/icon/reply.png'),
      replies: [],
      total: 0,
      pn: 1,
      hasMore: false,
      loading: false,
      logged: false,
      status: '加载中…',
      posting: false,
      ime: null,
      generation: 0
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
      this.logged = hasCookie()  // 模板不能直接调导入函数, 落到 data
      // 从登录页返回后已登录: 之前被门禁挡住, 现在补一次加载
      if (this.logged && !wasLogged && this.aid && this.replies.length === 0) {
        this.status = '加载中…'
        this.load(true)
        return
      }
      this.applyOptions((this.$page && this.$page.options) || {})
    },

    goLogin() {
      // 与本项目其它页面一致: 页面跳转走 $falcon.navTo($page 无此方法)
      $falcon.navTo('login', {})
    },

    onUnload() {
      if (this.ime) { try { this.ime.destroy() } catch (e) {} }
    },

    applyOptions(options) {
      const aid = parseInt(options.aid || '0', 10) || 0
      if (aid === this.aid && (this.replies.length > 0 || this.loading)) return
      this.aid = aid
      this.titleText = options.title || ''
      this.replies = []
      this.total = 0
      this.pn = 1
      this.hasMore = false
      this.generation++   // 作废在途请求
      this.loading = false
      // 未登录: 不发请求, 直接展示登录引导
      if (!this.logged) {
        this.status = ''
        return
      }
      this.status = '加载中…'
      this.load(true)
    },

    load(reset) {
      if (!this.aid || this.loading) return
      const gen = ++this.generation
      this.loading = true
      if (reset) this.status = '加载中…'
      // 同步 httpGet 延后到首帧之后
      afterPaint(async () => {
        try {
          const r = await getReplies(this.aid, this.pn, BUILTIN_EMOJI)
          if (gen !== this.generation) return
          if (reset) this.replies = []
          // r.replies 是替换后的新页数据 (pn 已在 loadMore 里递增前记录)
          this.appendPage(r)
          this.status = ''
        } catch (err) {
          if (gen !== this.generation) return
          console.log('[comment] load error: ' + (err && err.message ? err.message : err))
          this.status = err && err.message ? err.message : String(err)
        } finally {
          if (gen === this.generation) this.loading = false
        }
      })
    },

    appendPage(r) {
      // 同 rpid 去重后追加
      const seen = {}
      for (let i = 0; i < this.replies.length; i++) seen[this.replies[i].rpid] = true
      for (let i = 0; i < r.replies.length; i++) {
        const item = r.replies[i]
        if (!seen[item.rpid]) {
          this.replies.push(item)
          seen[item.rpid] = true
        }
      }
      this.total = r.total
      this.hasMore = this.replies.length < r.total && r.replies.length > 0
    },

    loadMore() {
      if (this.loading || !this.hasMore) return
      this.pn++
      this.load(false)
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
        this.status = '输入失败: ' + (err && err.message ? err.message : err)
      }
    },

    async postComment(message) {
      if (this.posting) return
      this.posting = true
      this.status = '发送中…'
      try {
        await addReply(this.aid, message)
        // 成功: 刷新第一页, 新评论通常出现在置顶/热评区, 简单起见回到第一页
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
  width: 100px;
  height: 34px;
  margin-left: 12px;
  border-radius: 17px;
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
  width: 620px;
  /* Falcon 不支持 max-lines, 必须用 lines: N 配合 text-overflow (此前长标题溢出) */
  lines: 1;
  text-overflow: ellipsis;
  overflow: hidden;
}
.count {
  font-size: 19px;
  color: #8a94a6;
}
.list {
  position: absolute;
  left: 0px;
  top: 44px;
  width: 960px;
  height: 178px;
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
  padding-top: 10px;
  padding-bottom: 10px;
  border-bottom-width: 1px;
  border-bottom-color: #262b33;
}
.face {
  width: 52px;
  height: 52px;
  border-radius: 26px;
  margin-right: 12px;
}
.reply-main {
  width: 850px;
  flex-direction: column;
}
.reply-head {
  flex-direction: row;
  align-items: center;
  margin-bottom: 4px;
}
.reply-author {
  font-size: 19px;
  color: #8a94a6;
  margin-right: 12px;
}
.reply-time {
  font-size: 17px;
  color: #5c6672;
}
.reply-msg {
  font-size: 21px;
  color: #e8edf3;
  /* richtext 支持 lines: 长评论限 4 行, 防止把可视区撑爆 */
  lines: 4;
  margin-top: 2px;
}
.reply-meta {
  flex-direction: row;
  align-items: center;
  margin-top: 4px;
}
.meta-icon {
  width: 22px;
  height: 22px;
  margin-right: 4px;
}
.reply-ic-gap {
  margin-left: 14px;
}
.meta-text {
  font-size: 17px;
  color: #6a7684;
}
.load-more {
  font-size: 20px;
  color: #fb7299;
  text-align: center;
  margin-top: 12px;
  margin-bottom: 12px;
}
.empty {
  font-size: 20px;
  color: #6a7684;
  margin-top: 20px;
  text-align: center;
}
/* 未登录门禁 */
.gate {
  width: 936px;
  height: 160px;
  flex-direction: column;
  justify-content: center;
  align-items: center;
}
.gate-text {
  font-size: 22px;
  color: #8a94a6;
  margin-bottom: 14px;
}
.gate-btn {
  width: 300px;
  height: 44px;
  border-radius: 22px;
  background-color: #fb7299;
  justify-content: center;
  align-items: center;
}
.gate-btn-text {
  font-size: 21px;
  color: #ffffff;
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
