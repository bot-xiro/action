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
            <text class="reply-msg">{{ r.message }}</text>
            <text class="reply-meta">👍 {{ r.likeText }} · 💬 {{ r.replyCount }}</text>
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

export default {
  name: 'comment',
  data() {
    return {
      aid: 0,
      titleText: '',
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
      this.$page.navTo({ page: 'login' })
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
          const r = await getReplies(this.aid, this.pn)
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
  max-lines: 1;
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
}
.reply-meta {
  font-size: 17px;
  color: #6a7684;
  margin-top: 4px;
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
