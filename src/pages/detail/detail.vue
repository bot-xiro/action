<template>
    <div class="page">
        <!-- 加载中 -->
        <div v-if="loading" class="center">
            <text class="hint">加载中...</text>
        </div>

        <!-- 错误提示 -->
        <div v-else-if="error" class="center">
            <text class="hint">{{ error }}</text>
            <text class="retry" @click="loadDetail()">点击重试</text>
        </div>

        <!-- 内容：左右独立滚动（左栏滚到底固定，右栏继续滚；点击见 r-item 双保险绑定 + touch 兜底） -->
        <div v-else-if="video" class="body">
            <!-- 左栏：与封面同宽 260px（右侧分割线即照片右缘），内部垂直滚动 -->
            <div class="info-panel">
                <scroller class="info-scroller" scroll-direction="vertical" :show-scrollbar="false">
                    <div class="info-inner">
                        <image class="cover" :src="video.pic" resize="cover"></image>
                        <text class="title" :lines="1">{{ video.title }}</text>
                        <text class="meta">{{ video.owner.name }} · {{ formatCount(video.stat.view) }}播放 · {{ formatCount(video.stat.danmaku) }}弹幕</text>
                        <text class="bvid">{{ bvid }}</text>
                        <text class="play-btn" @click="openPlayer(video)">► 播放</text>
                        <!-- 简介：不限行数，自动换行完整显示（左栏可滚，滚到底固定） -->
                        <text class="desc">{{ video.desc }}</text>
                    </div>
                </scroller>
            </div>

            <!-- 右栏：Tab 切换 + 单 scroller（v-if 保证同一时刻仅渲染一个，规避框架多 scroller 点击失效） -->
            <div class="right-col"
                 @touchstart="swStart($event)"
                 @touchmove="swMove($event)"
                 @touchend="swEnd()">
                <!-- Tab 行 -->
                <div class="tab-row">
                    <text class="tab-item" :class="tab==='related' ? 'tab-active' : ''" @click="switchTab('related')">相关推荐</text>
                    <text class="tab-item" :class="tab==='comment' ? 'tab-active' : ''" @click="switchTab('comment')">评论区{{ commentTotal ? (' ' + formatCount(commentTotal)) : '' }}</text>
                </div>

                <!-- 相关推荐 -->
                <scroller v-if="tab==='related'" class="related-list" scroll-direction="vertical" :show-scrollbar="false" @scroll="onRelScroll">
                    <div class="related-inner">
                        <div v-for="v in topRelated()" :key="v.bvid" class="r-item"
                             @click="openVideo(v)"
                             @touchstart="rtStart(v, $event)"
                             @touchend="rtEnd(v)">
                            <image class="r-cover" :src="v.pic" resize="cover"></image>
                            <div class="r-info">
                                <text class="r-title" :lines="1" @click="openVideo(v)">{{ v.title }}</text>
                                <text class="r-meta">{{ v.owner.name }} · {{ formatCount(v.stat.view) }}播放</text>
                            </div>
                        </div>
                        <div v-if="!related.length" class="related-empty">
                            <text class="hint">暂无相关推荐</text>
                        </div>
                    </div>
                </scroller>

                <!-- 评论区（下拉刷新：顶部下拉超 -50px 触发 refresh） -->
                <scroller v-else class="comment-list" scroll-direction="vertical" :show-scrollbar="false" :over-scroll="60" @scroll="onCommentScroll" @scrolltolower="loadMoreReplies">
                    <div class="comment-refresh" v-if="commentRefreshState === 'pulling'">
                        <text class="hint">松手刷新评论</text>
                    </div>
                    <div class="comment-refresh" v-else-if="commentRefreshState === 'refreshing'">
                        <text class="hint">刷新中...</text>
                    </div>
                    <div class="comment-inner">
                        <div v-if="repliesLoading && !replies.length" class="center-col">
                            <text class="hint">评论加载中...</text>
                        </div>
                        <div v-else-if="!replies.length && repliesDone" class="center-col">
                            <text class="hint">暂无评论</text>
                        </div>
                        <div v-else>
                            <div v-for="c in replies" :key="c.rpid" class="c-item">
                                <image class="c-avatar" :src="c.member && c.member.avatar" resize="cover"></image>
                                <div class="c-body">
                                    <div class="c-header">
                                        <text class="c-uname">{{ c.member ? c.member.uname : '' }}</text>
                                        <text v-if="c._isTop" class="c-hot-badge">热</text>
                                    </div>
                                    <text class="c-msg" :lines="3">{{ c.content ? c.content.message : '' }}</text>
                                    <text class="c-meta">赞 {{ c.like }} · {{ c.rcount }} 条回复 · {{ fmtTime(c.ctime) }}</text>
                                </div>
                            </div>
                            <div v-if="repliesLoading" class="load-more">
                                <text class="hint">加载中...</text>
                            </div>
                            <div v-else-if="!repliesHasMore" class="load-more">
                                <text class="hint">没有更多评论了</text>
                            </div>
                        </div>
                    </div>
                </scroller>
            </div>
        </div>
    </div>
</template>

<style scoped>
.page {
    flex: 1;
    background-color: #ffffff;
    flex-direction: column;
}

.body {
    flex: 1;
    flex-direction: row;
}

/* ---- 左栏：与封面同宽（260px，右侧 border 即分割线 = 照片右缘） ---- */
.info-panel {
    width: 260px;
    border-right-width: 1px;
    border-right-color: #eeeeee;
    background-color: #fafafa;
    flex-direction: column;
}

.info-scroller {
    flex: 1;
    flex-direction: column;
}

.info-inner {
    flex-direction: column;
    padding-left: 8px;
    padding-right: 8px;
    padding-top: 8px;
    padding-bottom: 8px;
}

.cover {
    width: 244px;
    height: 153px;
    background-color: #e0e0e0;
    border-radius: 8px;
    align-self: flex-start;
}

.title {
    margin-top: 8px;
    font-size: 15px;
    color: #333333;
    font-weight: bold;
    width: 244px;
    align-self: flex-start;
}

.meta {
    margin-top: 4px;
    font-size: 12px;
    color: #999999;
    width: 244px;
    align-self: flex-start;
}

.bvid {
    margin-top: 4px;
    font-size: 12px;
    color: #999999;
    width: 244px;
    align-self: flex-start;
}

.play-btn {
    margin-top: 8px;
    width: 244px;
    height: 32px;
    background-color: #fb7299;
    border-radius: 6px;
    text-align: center;
    line-height: 32px;
    font-size: 16px;
    color: #ffffff;
}

.desc {
    margin-top: 6px;
    font-size: 12px;
    color: #666666;
    /* 不限行数：自动换行完整显示 */
    width: 244px;
    align-self: flex-start;
}

/* ---- 右栏：Tab + 内容区 ---- */
.right-col {
    flex: 1;
    flex-direction: column;
    background-color: #ffffff;
}

.tab-row {
    height: 30px;
    flex-direction: row;
    align-items: center;
    padding-left: 10px;
    border-bottom-width: 1px;
    border-bottom-color: #f0f0f0;
}

.tab-item {
    font-size: 14px;
    color: #999999;
    margin-right: 18px;
    height: 29px;
    line-height: 29px;
}

.tab-active {
    color: #fb7299;
    font-weight: bold;
    border-bottom-width: 2px;
    border-bottom-color: #fb7299;
}

.related-list {
    flex: 1;
    padding-left: 8px;
    padding-right: 8px;
    flex-direction: column;
}

.comment-list {
    flex: 1;
    padding-left: 10px;
    padding-right: 10px;
    flex-direction: column;
}

.comment-inner {
    flex-direction: column;
}

.c-item {
    flex-direction: row;
    padding-top: 8px;
    padding-bottom: 8px;
    border-bottom-width: 1px;
    border-bottom-color: #f2f2f2;
}

.c-avatar {
    width: 28px;
    height: 28px;
    border-radius: 14px;
    background-color: #e0e0e0;
    margin-right: 8px;
}

.c-body {
    flex: 1;
    flex-direction: column;
}

.c-header {
    flex-direction: row;
    align-items: center;
}

.c-uname {
    font-size: 12px;
    color: #999999;
}

.c-hot-badge {
    margin-left: 6px;
    padding-left: 4px;
    padding-right: 4px;
    height: 14px;
    line-height: 14px;
    font-size: 10px;
    color: #ffffff;
    background-color: #fb7299;
    border-radius: 2px;
}

.c-msg {
    margin-top: 2px;
    font-size: 13px;
    color: #333333;
}

.c-meta {
    margin-top: 3px;
    font-size: 11px;
    color: #bbbbbb;
}

.center-col {
    flex: 1;
    align-items: center;
    justify-content: center;
    padding-top: 40px;
}

.load-more {
    height: 32px;
    align-items: center;
    justify-content: center;
}

.comment-refresh {
    height: 36px;
    align-items: center;
    justify-content: center;
    background-color: #fafafa;
}

.related-inner {
    flex-direction: column;
}

.related-empty {
    flex-direction: column;
    align-items: center;
    padding-top: 40px;
}

.r-item {
    height: 64px;
    margin-bottom: 6px;
    flex-direction: row;
    align-items: center;
    background-color: #f5f5f5;
    border-radius: 6px;
}

.r-cover {
    width: 100px;
    height: 56px;
    background-color: #e0e0e0;
    margin-right: 8px;
    border-radius: 4px;
}

.r-info {
    flex: 1;
    flex-direction: column;
    justify-content: center;
}

.r-title {
    font-size: 14px;
    color: #333333;
    lines: 1;
}

.r-meta {
    margin-top: 3px;
    font-size: 12px;
    color: #999999;
}

/* ---- 通用 ---- */
.center {
    flex: 1;
    align-items: center;
    justify-content: center;
}

.hint {
    font-size: 18px;
    color: #999999;
}

.retry {
    margin-top: 12px;
    font-size: 18px;
    color: #fb7299;
    text-decoration: underline;
}
</style>

<script>
import api from '../../utils/api.js'

export default {
    name: 'detail',
    data() {
        return {
            bvid: '',
            video: null,
            related: [],
            loading: true,
            error: '',
            // 评论区数据（新版 /x/v2/reply/wbi/main 游标分页）
            tab: 'related',
            replies: [],
            replyCursor: '',
            repliesHasMore: true,
            repliesLoading: false,
            repliesDone: false,
            commentTotal: 0,
            // 评论区下拉刷新状态
            commentRefreshState: '',
            refreshLock: false,
            // 横滑切换快照
            swatch: null,
            // 右栏滚动偏移（点击判定）
            relScrollTop: 0
        }
    },
    mounted() {
        var opt = this.$page.loadOptions || {}
        this.bvid = opt.bvid || ''
        console.warn('[detail] mounted bvid=' + this.bvid)
        this.loadDetail()
        // 初始化 tab
        this.tab = 'related'
    },
    methods: {
        loadDetail() {
            if (!this.bvid) {
                this.loading = false
                this.error = '缺少视频参数'
                return
            }
            this.loading = true
            this.error = ''
            api.getVideoInfo(this.bvid)
                .then(data => {
                    this.video = data
                    this.loading = false
                    console.warn('[detail] video loaded: ' + data.title)
                    // 设置评论总数
                    this.commentTotal = data.stat && data.stat.reply ? data.stat.reply : 0
                    // 预取播放地址
                    var prefetchCid = (data && (data.cid || (data.pages && data.pages[0] && data.pages[0].cid))) || ''
                    if (prefetchCid) {
                        api.getPlayUrl(this.bvid, prefetchCid, 64, 1)
                            .then(() => console.warn('[detail] playUrl prefetched: ' + this.bvid))
                            .catch(err => console.warn('[detail] playUrl prefetch skip: ' + (err && err.message ? err.message : '')))
                    }
                })
                .catch(err => {
                    console.warn('[detail] video error: ' + (err && err.message ? err.message : JSON.stringify(err)))
                    this.loading = false
                    this.error = '加载失败: ' + (err && err.message ? err.message : JSON.stringify(err))
                })
            api.getRelated(this.bvid)
                .then(list => {
                    this.related = list || []
                })
                .catch(err => {
                    console.warn('[detail] related error: ' + (err && err.message ? err.message : JSON.stringify(err)))
                })
        },
        switchTab(t) {
            if (this.tab === t) return
            this.tab = t
            console.warn('[detail] switchTab: ' + t)
            if (t === 'comment' && !this.replies.length && !this.repliesLoading) {
                this.loadReplies(true)
            }
            // 重置 swatch
            this.swatch = null
        },
        swStart(e) {
            var t = e && e.changedTouches && e.changedTouches[0]
            this.swatch = t ? { x: t.pageX, y: t.pageY, t: Date.now() } : null
        },
        swMove(e) {
            var s = this.swatch, t = e && e.changedTouches && e.changedTouches[0]
            if (!s || !t) return
            var dx = t.pageX - s.x, dy = t.pageY - s.y
            if (Math.abs(dx) > 8 && Math.abs(dx) > Math.abs(dy) * 1.2) {
                if (dx < -60) this.switchTab('comment')
                else if (dx > 60) this.switchTab('related')
            }
        },
        swEnd() {
            this.swatch = null
        },
        loadReplies(reset) {
            if (!this.video || !this.video.aid) return
            if (reset) {
                this.replies = []
                this.replyPage = 0
                this.repliesHasMore = true
                this.repliesDone = false
            }
            if (!this.repliesHasMore || this.repliesLoading) return
            var pn = this.replyPage + 1
            this.repliesLoading = true
            console.warn('[detail] replies load cursor=' + (this.replyCursor || 'first') + ' aid=' + this.video.aid)
            api.getReplies(this.video.aid, this.replyCursor)
                .then(data => {
                    // 首页：置顶/热评合并到前部并打标，翻页只追加普通评论
                    var list = reset ? (data.tops || []).concat(data.replies || []) : (data.replies || [])
                    this.replies = reset ? list : this.replies.concat(list)
                    this.replyCursor = data.next || ''
                    this.repliesHasMore = !data.isEnd && list.length > 0 && !!this.replyCursor
                    this.repliesDone = true
                    this.repliesLoading = false
                    console.warn('[detail] replies loaded n=' + list.length + ' tops=' + ((data.tops || []).length) + ' next=' + (data.next || '') + ' end=' + data.isEnd + ' total=' + data.total)
                })
                .catch(err => {
                    console.warn('[detail] replies error: ' + (err && err.message ? err.message : JSON.stringify(err)))
                    this.repliesLoading = false
                    this.repliesDone = true
                })
        },
        loadMoreReplies() {
            if (this.tab === 'comment') {
                this.loadReplies(false)
            }
        },
        onCommentScroll(e) {
            if (!e || !e.contentOffset) return
            var y = e.contentOffset.y || 0
            // 顶部下拉越界即触发刷新（与 index 主页下拉刷新同一模式；refreshLock 防抖防连发）
            if (y < -60 && !this.refreshLock && !this.repliesLoading) {
                this.refreshLock = true
                console.warn('[detail] comment refresh trigger y=' + y)
                this.refreshComments()
                setTimeout(() => { this.refreshLock = false }, 800)
            }
        },
        refreshComments() {
            if (!this.video || !this.video.aid) return
            this.commentRefreshState = 'refreshing'
            console.warn('[detail] comment refresh start aid=' + this.video.aid)
            // 重置并重载第一页
            this.replies = []
            this.replyCursor = ''
            this.repliesHasMore = true
            this.repliesDone = false
            this.repliesLoading = true
            api.getReplies(this.video.aid, '')
                .then(data => {
                    var list = (data.tops || []).concat(data.replies || [])
                    this.replies = list
                    this.replyCursor = data.next || ''
                    this.repliesHasMore = !data.isEnd && list.length > 0 && !!this.replyCursor
                    this.repliesDone = true
                    this.repliesLoading = false
                    this.commentRefreshState = ''
                    console.warn('[detail] comment refresh done n=' + list.length + ' tops=' + ((data.tops || []).length))
                })
                .catch(err => {
                    console.warn('[detail] comment refresh error: ' + (err && err.message ? err.message : JSON.stringify(err)))
                    this.repliesLoading = false
                    this.commentRefreshState = ''
                })
        },
        rtStart(v, e) {
            var t = e && e.changedTouches && e.changedTouches[0]
            this.rt = {
                v: v,
                t: Date.now(),
                scrollTop: this.relScrollTop,
                x: t ? t.pageX : 0,
                y: t ? t.pageY : 0
            }
            console.warn('[detail] r-item touchstart: ' + v.bvid + ' keys=' + (e ? Object.keys(e).join(',') : 'none'))
        },
        rtEnd(v) {
            var rt = this.rt
            this.rt = null
            var scrolled = rt ? (this.relScrollTop !== rt.scrollTop) : false
            var dx = rt && v ? (rt.x - (v && v.pageX || rt.x)) : 0
            var dy = rt && v ? (rt.y - (v && v.pageY || rt.y)) : 0
            var moved = Math.abs(dx) > 20 || Math.abs(dy) > 20
            console.warn('[detail] r-item touchend: ' + v.bvid + (rt ? ' moved=' + moved + ' dx=' + dx + ' dy=' + dy + ' dt=' + (Date.now() - rt.t) : ''))
            if (rt && rt.v === v && !scrolled && !moved && Date.now() - rt.t < 400) {
                console.warn('[detail] open(touch): ' + v.bvid)
                this.gotoVideo(v.bvid)
            }
        },
        openPlayer(v) {
            console.warn('[detail] openPlayer: ' + v.bvid + ' cid=' + v.cid)
            $falcon.navTo('player', { bvid: v.bvid, cid: v.cid, title: v.title })
        },
        formatCount(count) {
            if (count >= 10000) {
                return (count / 10000).toFixed(1) + '万'
            }
            return String(count)
        },
        topRelated() {
            return (this.related || []).slice(0, 6)
        },
        fmtTime(ts) {
            if (!ts) return ''
            var now = Math.floor(Date.now() / 1000)
            var diff = now - ts
            if (diff < 3600) {
                return Math.max(1, Math.floor(diff / 60)) + '分钟前'
            }
            if (diff < 86400) {
                return Math.floor(diff / 3600) + '小时前'
            }
            if (diff < 86400 * 30) {
                return Math.floor(diff / 86400) + '天前'
            }
            var d = new Date(ts * 1000)
            return (d.getMonth() + 1) + '-' + d.getDate()
        },
        gotoVideo(bvid) {
            // 切换视频时重置评论区状态
            if (!bvid || bvid === this.bvid) return
            this.bvid = bvid
            this.video = null
            this.related = []
            this.commentTotal = 0
            this.replies = []
            this.replyPage = 0
            this.repliesHasMore = true
            this.tab = 'related'
            this.loading = true
            this.loadDetail()
        }
    }
}
</script>