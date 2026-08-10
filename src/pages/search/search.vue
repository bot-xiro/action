<template>
    <div class="page">
        <!-- 顶部：返回 + 输入框 + 搜索按钮 -->
        <div class="topbar">
            <text class="back" @click="goBack">‹</text>
            <div class="search-box" @click="openKeyboard">
                <text v-if="!keyword" class="placeholder">搜索视频 / UP主</text>
                <text v-else class="keyword-text">{{ keyword }}</text>
            </div>
            <text class="go-btn" @click="doSearch">搜索</text>
        </div>

        <!-- 未搜索时：展示搜索历史 -->
        <div v-if="!searched" class="history-wrap">
            <div class="history-head">
                <text class="history-title">搜索历史</text>
                <text class="history-clear" @click="clearHistory">清除</text>
            </div>
            <scroller class="history-list" scroll-direction="vertical" :show-scrollbar="false">
                <text v-for="(h, i) in history" :key="i" class="history-item" @click="searchByHistory(h)">{{ h }}</text>
                <text v-if="!history.length" class="empty">暂无搜索历史</text>
            </scroller>
        </div>

        <!-- 搜索结果列表 -->
        <div v-else class="result-wrap">
            <div v-if="loading" class="center">
                <text class="hint">搜索中...</text>
            </div>
            <div v-else-if="error" class="center">
                <text class="hint">{{ error }}</text>
                <text class="retry" @click="doSearch">点击重试</text>
            </div>
            <div v-else-if="!results.length" class="center">
                <text class="hint">没有找到相关视频</text>
            </div>
            <scroller v-else class="result-list" scroll-direction="vertical" :show-scrollbar="false">
                <div v-for="(v, i) in results" :key="i" class="r-item" @click="openResult(v)">
                    <image class="r-cover" :src="v.pic" resize="cover"></image>
                    <div class="r-info">
                        <text class="r-title" :lines="1">{{ stripHtml(v.title) }}</text>
                        <text class="r-meta">{{ v.author }} · {{ fmtPlay(v.play) }} · {{ v.duration }}</text>
                    </div>
                </div>
            </scroller>
        </div>

        <!-- 软键盘弹层（点击输入框弹出） -->
        <div v-if="keyboardShow" class="kbd-layer" @click="noop">
            <soft-keyboard :variant="variant" @input="onChar" @backspace="onBack" @clear="clearKeyword" @variant="toggleVariant" @search="doSearch"></soft-keyboard>
        </div>
    </div>
</template>

<style scoped>
.page {
    flex: 1;
    background-color: #ffffff;
    flex-direction: column;
}

/* ---- 顶部搜索栏 ---- */
.topbar {
    height: 48px;
    background-color: #fb7299;
    flex-direction: row;
    align-items: center;
    padding-left: 10px;
    padding-right: 10px;
}

.back {
    font-size: 30px;
    color: #ffffff;
    padding-top: 2px;
    padding-right: 12px;
}

.search-box {
    flex: 1;
    height: 32px;
    background-color: #ffffff;
    border-radius: 16px;
    justify-content: center;
    padding-left: 12px;
    padding-right: 12px;
}

.placeholder {
    font-size: 16px;
    color: #999999;
}

.keyword-text {
    font-size: 16px;
    color: #333333;
    lines: 1;
}

.go-btn {
    margin-left: 10px;
    font-size: 18px;
    color: #ffffff;
}

/* ---- 历史区 ---- */
.history-wrap {
    flex: 1;
    padding-top: 10px;
    padding-left: 12px;
    padding-right: 12px;
    flex-direction: column;
}

.history-head {
    flex-direction: row;
    justify-content: space-between;
    align-items: center;
    height: 30px;
}

.history-title {
    font-size: 16px;
    color: #333333;
    font-weight: bold;
}

.history-clear {
    font-size: 13px;
    color: #fb7299;
}

.history-list {
    flex: 1;
    margin-top: 6px;
}

.history-item {
    height: 36px;
    font-size: 16px;
    color: #555555;
    border-bottom-width: 1px;
    border-bottom-color: #f0f0f0;
    line-height: 36px;
}

/* ---- 结果区 ---- */
.result-wrap {
    flex: 1;
    flex-direction: column;
}

.result-list {
    flex: 1;
    padding-left: 10px;
    padding-right: 10px;
    margin-top: 6px;
}

.r-item {
    height: 56px;
    margin-bottom: 6px;
    background-color: #f5f5f5;
    border-radius: 6px;
    flex-direction: row;
    align-items: center;
    overflow: hidden;
}

.r-cover {
    width: 90px;
    height: 50px;
    background-color: #e0e0e0;
    margin-left: 3px;
    border-radius: 4px;
}

.r-info {
    flex: 1;
    margin-left: 8px;
    margin-right: 8px;
    flex-direction: column;
    justify-content: center;
}

.r-title {
    font-size: 15px;
    color: #333333;
    lines: 1;
}

.r-meta {
    margin-top: 3px;
    font-size: 12px;
    color: #999999;
}

/* ---- 软键盘弹层 ---- */
.kbd-layer {
    position: absolute;
    left: 0;
    right: 0;
    bottom: 0;
}

/* ---- 通用 ---- */
.center {
    flex: 1;
    align-items: center;
    justify-content: center;
}

.hint {
    font-size: 16px;
    color: #999999;
}

.retry {
    margin-top: 10px;
    font-size: 16px;
    color: #fb7299;
    text-decoration: underline;
}
</style>

<script>
import api from '../../utils/api.js'
import storage from '../../utils/storage.js'
import softKeyboard from '../../components/SoftKeyboard.vue'

export default {
    name: 'search',
    components: {
        softKeyboard
    },
    data() {
        return {
            keyword: '',
            history: [],
            searched: false,
            results: [],
            loading: false,
            error: '',
            keyboardShow: false,
            variant: 0
        }
    },
    mounted() {
        this.history = ''
        // storage JSAPI 为异步（Promise），读取后回填
        var self = this
        storage.getHistory().then(function (list) {
            self.history = Array.isArray(list) ? list : ''
        }).catch(function () {
            self.history = ''
        })
    },
    methods: {
        noop() { },
        openKeyboard() {
            this.keyboardShow = true
        },
        onChar(c) {
            // 输入字符：追加到关键字（限制 30 字）
            if (this.keyword.length >= 30) return
            this.keyword = this.keyword + c
        },
        onBack() {
            this.keyword = this.keyword.substring(0, this.keyword.length - 1)
        },
        clearInput() {
            this.keyword = ''
        },
        toggleVariant() {
            this.variant = this.variant === 0 ? 1 : 0
        },
        doSearch() {
            if (!this.keyword || !this.keyword.trim()) return
            var kw = this.keyword.trim()
            this.searched = true
            this.keyboardShow = false   // 搜索即收起键盘
            this.loading = true
            this.error = ''
            api.searchVideo(kw, 1)
                .then(data => {
                    // 兼容两种返回结构：data.result 为数组，或缺省时 data.result.video
                    var result = (data && data.result) || (data && data.video) || []
                    var list = Array.isArray(result) ? result : (Array.isArray(result.video) ? result.video : [])
                    this.results = list
                    this.loading = false
                    this.searched = true
                    // 写历史（异步，不阻塞结果展示）
                    var self = this
                    storage.addHistory(kw).then(function (list) {
                        self.history = Array.isArray(list) ? list : ''
                    }).catch(function () { })
                    console.warn('[search] done kw=' + kw + ' hits=' + this.results.length)
                })
                .catch(err => {
                    this.loading = false
                    this.error = '搜索失败: ' + (err && err.message ? err.message : JSON.stringify(err))
                    console.warn('[search] error: ' + (err && err.message ? err.message : JSON.stringify(err)))
                })
        },
        searchByHistory(h) {
            this.keyword = h
            this.doSearch()
        },
        clearHistory() {
            var self = this
            storage.clearHistory().then(function () {
                self.history = ''
            }).catch(function () {
                self.history = ''
            })
        },
        openResult(v) {
            console.log('[search] open: ' + v.bvid)
            $falcon.navTo('detail', { bvid: v.bvid })
        },
        // ---- 显示辅助 ----
        stripHtml(s) {
            // 搜索结果 title 含 <em class="keyword"> 等标签，去除后展示
            return String(s || '').replace(/<[^>]*>/g, '')
        },
        fmtPlay(play) {
            // play 可能是数字（16334）或带万（1.2万）
            var v = play
            if (typeof v === 'number') {
                if (v >= 10000) return (v / 10000).toFixed(1) + '万'
                return String(v)
            }
            return String(v == null ? '' : v)
        }
    }
}
</script>