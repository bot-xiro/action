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

            <!-- 右栏：相关推荐，独立垂直滚动 -->
            <div class="related-wrap">
                <text class="related-title">相关推荐</text>
                <scroller class="related-list" scroll-direction="vertical" :show-scrollbar="false">
                    <div class="related-inner">
                        <div v-for="v in topRelated()" :key="v.bvid" class="r-item"
                             @click="openVideo(v)"
                             @touchstart="rtStart(v)"
                             @touchmove="rtMove()"
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

/* ---- 右栏：相关推荐 ---- */
.related-wrap {
    flex: 1;
    flex-direction: column;
    background-color: #ffffff;
}

.related-title {
    height: 30px;
    font-size: 16px;
    color: #fb7299;
    font-weight: bold;
    padding-left: 10px;
    padding-top: 6px;
}

.related-list {
    flex: 1;
    padding-left: 8px;
    padding-right: 8px;
    flex-direction: column;
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
            // touch 模拟点击：记录按下的推荐项与时间，抬起时若未滑动且在 400ms 内视为点击
            rt: null
        }
    },
    mounted() {
        // 从 index 页 navTo 带过来的参数
        var opt = this.$page.loadOptions || {}
        this.bvid = opt.bvid || ''
        console.warn('[detail] mounted bvid=' + this.bvid)
        this.loadDetail()
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
                    // 预取播放地址（cid 已就绪）：缓存 10 分钟，点播放时秒开
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
            // 相关推荐（独立于主信息，失败不阻断）
            api.getRelated(this.bvid)
                .then(list => {
                    this.related = list || []
                    console.warn('[detail] related loaded: ' + this.related.length)
                })
                .catch(err => {
                    console.warn('[detail] related error: ' + (err && err.message ? err.message : JSON.stringify(err)))
                })
        },
        openVideo(v) {
            console.log('[detail] open(click): ' + v.bvid)
            $falcon.navTo('detail', { bvid: v.bvid })
        },
        // touch 模拟点击兜底：若 click 事件在深层 div 上不触发，则用 touch 时序模拟
        rtStart(v) {
            this.rt = { v: v, t: Date.now(), moved: false }
            console.warn('[detail] r-item touchstart: ' + v.bvid)
        },
        rtMove() {
            if (this.rt) {
                this.rt.moved = true
            }
        },
        rtEnd(v) {
            var rt = this.rt
            this.rt = null
            console.warn('[detail] r-item touchend: ' + v.bvid + (rt ? ' moved=' + rt.moved + ' dt=' + (Date.now() - rt.t) : ''))
            if (rt && rt.v === v && !rt.moved && Date.now() - rt.t < 400) {
                console.log('[detail] open(touch): ' + v.bvid)
                $falcon.navTo('detail', { bvid: v.bvid })
            }
        },
        openPlayer(v) {
            console.log('[detail] openPlayer: ' + v.bvid + ' cid=' + v.cid)
            $falcon.navTo('player', { bvid: v.bvid, cid: v.cid, title: v.title })
        },
        formatCount(count) {
            if (count >= 10000) {
                return (count / 10000).toFixed(1) + '万'
            }
            return String(count)
        },
        // 右列最多展示 6 条：整页单 scroller 高度可控（≈420px），左列信息不会被滚出屏
        topRelated() {
            return (this.related || []).slice(0, 6)
        }
    }
}
</script>