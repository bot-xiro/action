<template>
    <div class="page">
        <!-- 顶部标题栏 -->
        <div class="topbar">
            <text class="topbar-title">Bilibili 热门</text>
            <div class="topbar-right">
                <text class="topbar-status">{{ statusText }}</text>
                <text class="topbar-search" @click="openSearch">搜索</text>
            </div>
        </div>

        <!-- 加载中 -->
        <div v-if="loading" class="center">
            <text class="hint">加载中...</text>
        </div>

        <!-- 错误提示 -->
        <div v-else-if="error" class="center">
            <text class="hint">{{ error }}</text>
            <text class="retry" @click="loadPopular()">点击重试</text>
        </div>

        <!-- 推荐列表：垂直滚动（支持下拉刷新 + 触底加载） -->
        <scroller v-else class="list" scroll-direction="vertical" :show-scrollbar="false" :over-scroll="60" @scroll="onScroll" @scrolltolower="onLoadMore">
            <div class="list-inner">
                <!-- 视频列表项：左图右文 -->
                <div v-for="item in videos" :key="item.bvid" class="v-item" @click="openVideo(item)">
                    <image class="v-cover" :src="item.pic" resize="cover"></image>
                    <div class="v-info">
                        <text class="v-title" :lines="2">{{ item.title }}</text>
                        <text class="v-meta">{{ item.owner.name }} · {{ formatCount(item.stat.view) }}播放</text>
                    </div>
                </div>
                <!-- 底部加载状态 -->
                <div v-if="loadingMore" class="load-more">
                    <text class="load-text">加载中...</text>
                </div>
                <div v-else-if="!hasMore && videos.length > 0" class="load-more">
                    <text class="load-text">没有更多了</text>
                </div>
            </div>
        </scroller>
    </div>
</template>

<style scoped>
.page {
    flex: 1;
    background-color: #ffffff;
    flex-direction: column;
}

.topbar {
    height: 48px;
    background-color: #fb7299;
    flex-direction: row;
    align-items: center;
    justify-content: space-between;
    padding-left: 16px;
    padding-right: 16px;
}

.topbar-title {
    font-size: 28px;
    color: #ffffff;
    font-weight: bold;
}

.topbar-status {
    font-size: 20px;
    color: #ffffff;
    opacity: 0.9;
}

.topbar-right {
    flex-direction: row;
    align-items: center;
}

.topbar-search {
    margin-left: 14px;
    font-size: 20px;
    color: #ffffff;
    background-color: rgba(255, 255, 255, 0.25);
    border-radius: 4px;
    padding-left: 12px;
    padding-right: 12px;
    padding-top: 3px;
    padding-bottom: 3px;
}

.center {
    flex: 1;
    align-items: center;
    justify-content: center;
}

.hint {
    font-size: 24px;
    color: #666666;
}

.retry {
    margin-top: 16px;
    font-size: 24px;
    color: #fb7299;
    text-decoration: underline;
}

.list {
    flex: 1;
    flex-direction: column;
}

.list-inner {
    flex-direction: column;
}

/* ---- 列表项：左图右文 ---- */
.v-item {
    height: 112px;
    margin-top: 8px;
    margin-left: 12px;
    margin-right: 12px;
    padding-top: 8px;
    padding-bottom: 8px;
    padding-left: 8px;
    padding-right: 12px;
    background-color: #f5f5f5;
    border-radius: 8px;
    flex-direction: row;
    align-items: center;
    overflow: hidden;
}

.v-cover {
    width: 200px;
    height: 112px;
    background-color: #e0e0e0;
    border-radius: 4px;
}

.v-info {
    flex: 1;
    flex-direction: column;
    justify-content: center;
    margin-left: 12px;
}

.v-title {
    font-size: 18px;
    color: #333333;
    lines: 2;
}

.v-meta {
    margin-top: 4px;
    font-size: 14px;
    color: #999999;
    lines: 1;
}

/* ---- 底部加载状态 ---- */
.load-more {
    height: 36px;
    align-items: center;
    justify-content: center;
}

.load-text {
    font-size: 16px;
    color: #999999;
}
</style>

<script>
import api from '../../utils/api.js'

export default {
    name: 'index',
    data() {
        return {
            videos: [],
            loading: true,
            error: '',
            statusText: '加载中',
            page: 1,
            pageSize: 20,
            loadingMore: false,
            refreshing: false,
            hasMore: true,
            refreshLock: false
        }
    },
    mounted() {
        this.loadPopular()
    },
    methods: {
        loadPopular() {
            this.loading = true
            this.error = ''
            this.statusText = '请求中'
            this.page = 1
            api.getPopular(this.page, this.pageSize)
                .then(data => {
                    var list = (data && data.list) || []
                    this.videos = list
                    this.loading = false
                    this.hasMore = list.length >= this.pageSize
                    this.statusText = '共 ' + this.videos.length + ' 条'
                    console.warn('[index] popular loaded: ' + this.videos.length + ' page=' + this.page)
                })
                .catch(err => {
                    console.warn('[index] popular error: ' + (err && err.message ? err.message : JSON.stringify(err)))
                    this.loading = false
                    this.error = '加载失败: ' + (err && err.message ? err.message : JSON.stringify(err))
                    this.statusText = '失败'
                })
        },
        loadMore() {
            if (this.loadingMore || this.refreshing || !this.hasMore) {
                return
            }
            this.loadingMore = true
            this.statusText = '加载第 ' + (this.page + 1) + ' 页'
            var nextPage = this.page + 1
            console.warn('[index] loadMore page=' + nextPage)
            api.getPopular(nextPage, this.pageSize)
                .then(data => {
                    var list = (data && data.list) || []
                    // 按 bvid 去重追加
                    var existing = {}
                    for (var i = 0; i < this.videos.length; i++) {
                        existing[this.videos[i].bvid] = true
                    }
                    var appended = 0
                    for (var j = 0; j < list.length; j++) {
                        if (!existing[list[j].bvid]) {
                            this.videos.push(list[j])
                            existing[list[j].bvid] = true
                            appended++
                        }
                    }
                    this.page = nextPage
                    this.loadingMore = false
                    this.hasMore = list.length >= this.pageSize
                    this.statusText = '共 ' + this.videos.length + ' 条'
                    console.warn('[index] loadMore OK appended=' + appended + ' total=' + this.videos.length + ' hasMore=' + this.hasMore)
                })
                .catch(err => {
                    console.warn('[index] loadMore error: ' + (err && err.message ? err.message : JSON.stringify(err)))
                    this.loadingMore = false
                    this.statusText = '加载更多失败'
                })
        },
        refresh() {
            if (this.refreshing || this.refreshLock) {
                return
            }
            this.refreshLock = true
            this.refreshing = true
            this.statusText = '刷新中'
            console.warn('[index] refresh')
            api.getPopular(1, this.pageSize)
                .then(data => {
                    var list = (data && data.list) || []
                    this.videos = list
                    this.page = 1
                    this.hasMore = list.length >= this.pageSize
                    this.refreshing = false
                    this.statusText = '共 ' + this.videos.length + ' 条'
                    console.warn('[index] refresh OK count=' + this.videos.length)
                    // 防抖：500ms 后释放刷新锁
                    setTimeout(() => {
                        this.refreshLock = false
                    }, 500)
                })
                .catch(err => {
                    console.warn('[index] refresh error: ' + (err && err.message ? err.message : JSON.stringify(err)))
                    this.refreshing = false
                    this.statusText = '刷新失败'
                    setTimeout(() => {
                        this.refreshLock = false
                    }, 500)
                })
        },
        onScroll(e) {
            // contentOffset.y < 0 表示顶部下拉越界（over-scroll 回弹区域）
            if (!e || !e.contentOffset) {
                return
            }
            var y = e.contentOffset.y || 0
            if (y < -40 && !this.refreshing && !this.refreshLock) {
                this.refresh()
            }
        },
        onLoadMore() {
            this.loadMore()
        },
        formatCount(count) {
            if (count >= 10000) {
                return (count / 10000).toFixed(1) + '万'
            }
            return String(count)
        },
        openVideo(item) {
            console.log('[index] open: ' + item.bvid)
            $falcon.navTo('detail', { bvid: item.bvid })
        },
        openSearch() {
            console.log('[index] openSearch')
            $falcon.navTo('search', {})
        }
    }
}
</script>
