<template>
    <div class="page">
        <!-- 顶部标题栏 -->
        <div class="topbar">
            <text class="topbar-title">Bilibili 热门</text>
            <text class="topbar-status">{{ statusText }}</text>
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

        <!-- 推荐列表：横向滚动 -->
        <scroller v-else class="list" scroll-direction="horizontal" :show-scrollbar="false">
            <div class="list-inner">
                <div v-for="item in videos" :key="item.bvid" class="card" @click="openVideo(item)">
                    <image class="cover" :src="item.pic" resize="cover"></image>
                    <text class="title">{{ item.title }}</text>
                    <text class="meta">{{ item.owner.name }} · {{ formatCount(item.stat.view) }}</text>
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
    flex-direction: row;
}

.list-inner {
    flex-direction: row;
    align-items: flex-start;
}

.card {
    width: 260px;
    height: 218px;
    margin-left: 12px;
    margin-top: 10px;
    margin-right: 4px;
    background-color: #f5f5f5;
    border-radius: 8px;
    flex-direction: column;
    overflow: hidden;
}

.card:first-child {
    margin-left: 16px;
}

.cover {
    width: 260px;
    height: 146px;
    background-color: #e0e0e0;
}

.title {
    margin-top: 6px;
    margin-left: 8px;
    margin-right: 8px;
    font-size: 20px;
    color: #333333;
    lines: 1;
}

.meta {
    margin-top: 4px;
    margin-left: 8px;
    margin-right: 8px;
    font-size: 18px;
    color: #999999;
    lines: 1;
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
            statusText: '加载中'
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
            api.getPopular(1, 20)
                .then(data => {
                    this.videos = data.list || []
                    this.loading = false
                    this.statusText = '共 ' + this.videos.length + ' 条'
                    console.warn('[index] popular loaded: ' + this.videos.length)
                })
                .catch(err => {
                    console.warn('[index] popular error: ' + (err && err.message ? err.message : JSON.stringify(err)))
                    this.loading = false
                    this.error = '加载失败: ' + (err && err.message ? err.message : JSON.stringify(err))
                    this.statusText = '失败'
                })
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
        }
    }
}
</script>
