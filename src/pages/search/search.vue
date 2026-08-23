<template>
    <div class="page">
        <!-- 顶部：返回 + 输入框 + 搜索按钮 + 输入法按钮 -->
        <div class="topbar">
            <text class="back" @click="goBack">‹</text>
            <div class="search-box-wrapper">
                <textarea
                    ref="searchInput"
                    class="search-input"
                    v-model="keyword"
                    placeholder="搜索视频 / UP主"
                    :softInputEnable="true"
                    :single-line="true"
                    @input="onInput"
                    @focus="onFocus"
                    @blur="onBlur"
                    @confirm="doSearch"
                    style="height: 32px;"
                ></textarea>
            </div>
            <text class="go-btn" @click="doSearch">搜索</text>
            <!-- 单独按钮启动有道输入法 (appid: 8001666679481944) -->
            <text class="ime-btn" @click="openSystemIME">⌨</text>
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

.search-box-wrapper {
    flex: 1;
    height: 32px;
    background-color: #ffffff;
    border-radius: 16px;
    padding-left: 12px;
    padding-right: 12px;
}

.search-input {
    flex: 1;
    height: 32px;
    font-size: 16px;
    color: #333333;
    background-color: transparent;
    lines: 1;
}

.go-btn {
    margin-left: 10px;
    font-size: 18px;
    color: #ffffff;
}

.ime-btn {
    margin-left: 8px;
    font-size: 20px;
    color: #ffffff;
    width: 36px;
    height: 36px;
    line-height: 36px;
    text-align: center;
    background-color: rgba(255, 255, 255, 0.25);
    border-radius: 6px;
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

export default {
    name: 'search',
    data() {
        return {
            keyword: '',
            history: [],
            searched: false,
            results: [],
            loading: false,
            error: '',
            // 等待输入法回调的标志
            waitingForIME: false
        }
    },
    mounted() {
        this.history = []
        // storage JSAPI 为异步（Promise），读取后回填
        var self = this
        storage.getHistory().then(function (list) {
            self.history = Array.isArray(list) ? list : []
        }).catch(function () {
            self.history = []
        })
        // 监听输入法回调事件 (参考 loli tools_keyboard: confirmAndReturn/cancelAndReturn/textEditFinished)
        if (typeof $falcon !== 'undefined' && $falcon.on) {
            $falcon.on('textEditFinished', this.onImeResult.bind(this))
            $falcon.on('confirmAndReturn', this.onImeResult.bind(this))
            $falcon.on('cancelAndReturn', this.onImeResult.bind(this))
            $falcon.on('imeResult', this.onImeResult.bind(this))
            $falcon.on('inputResult', this.onImeResult.bind(this))
        }
    },
    methods: {
        // 打开有道输入法 App (appid: 8001666679481944) - 使用 navTo 回调机制
        openSystemIME() {
            if (this.waitingForIME) {
                console.warn('[search] already waiting for IME, ignoring')
                return
            }
            console.warn('[search] openSystemIME: navTo 有道输入法 (8001666679481944)')
            this.waitingForIME = true
            try {
                // 使用 navTo 调用有道输入法，带回调参数
                // 回调地址：falcon://当前appid/ime-callback
                var callbackUrl = 'falcon://8001812345678901/ime-callback'
                var params = {
                    // 标准回调参数
                    callback: callbackUrl,
                    returnUrl: callbackUrl,
                    // 输入法相关参数
                    action: 'input',
                    type: 'text',
                    hint: '搜索视频 / UP主',
                    defaultText: this.keyword,
                    maxLength: 30,
                    confirmText: '搜索'
                }
                var ret = $falcon.navTo('falcon://8001666679481944', params)
                console.warn('[search] navTo ret: ' + JSON.stringify(ret))
            } catch (e) {
                console.warn('[search] openSystemIME error: ' + (e && e.message ? e.message : String(e)))
                this.waitingForIME = false
            }
        },
        // 处理 navTo 回调 - 页面被重新激活时调用
        onNewOptions(options) {
            console.warn('[search] onNewOptions received: ' + JSON.stringify(options))
            this.waitingForIME = false
            if (options && (options.text || options.value || options.result)) {
                var text = options.text || options.value || options.result
                this.keyword = text
                this.doSearch()
            }
        },
        // 兼容：onLoad 也可能接收回调参数
        onLoad(options) {
            console.warn('[search] onLoad received: ' + JSON.stringify(options))
            if (options && (options.text || options.value || options.result)) {
                var text = options.text || options.value || options.result
                this.keyword = text
                this.doSearch()
            }
        },
        onInput(val) {
            console.warn('[search] onInput: ' + val)
            this.keyword = val
        },
        onFocus() {
            console.warn('[search] onFocus')
        },
        onBlur() {
            console.warn('[search] onBlur')
        },
        // 处理输入法回调 (参考 loli: confirmAndReturn/cancelAndReturn/textEditFinished)
        onImeResult(result) {
            console.warn('[search] onImeResult received: ' + JSON.stringify(result))
            this.waitingForIME = false
            if (result && (result.text || result.value || result.content)) {
                var text = result.text || result.value || result.content
                this.keyword = text
                this.doSearch()
            }
        },
        doSearch() {
            if (!this.keyword || !this.keyword.trim()) return
            var kw = this.keyword.trim()
            this.searched = true
            this.loading = true
            this.error = ''
            api.searchVideo(kw, 1)
                .then(data => {
                    var result = (data && data.result) || (data && data.video) || []
                    var list = Array.isArray(result) ? result : (Array.isArray(result.video) ? result.video : [])
                    this.results = list
                    this.loading = false
                    this.searched = true
                    var self = this
                    storage.addHistory(kw).then(function (list) {
                        self.history = Array.isArray(list) ? list : []
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
                self.history = []
            }).catch(function () {
                self.history = []
            })
        },
        openResult(v) {
            console.log('[search] open: ' + v.bvid)
            $falcon.navTo('detail', { bvid: v.bvid })
        },
        goBack() {
            try {
                this.$page.finish()
            } catch (e) {
                console.warn('[search] finish error: ' + (e ? e.message : e))
            }
        },
        // ---- 显示辅助 ----
        stripHtml(s) {
            return String(s || '').replace(/<[^>]*>/g, '')
        },
        fmtPlay(play) {
            var v = play
            if (typeof v === 'number') {
                if (v >= 10000) return (v / 10000).toFixed(1) + '万'
                return String(v)
            }
            return String(v == null ? '' : v)
        },
        beforeDestroy() {
            // 清理输入法回调事件监听
            if (typeof $falcon !== 'undefined' && $falcon.off) {
                $falcon.off('textEditFinished', this.onImeResult)
                $falcon.off('confirmAndReturn', this.onImeResult)
                $falcon.off('cancelAndReturn', this.onImeResult)
                $falcon.off('imeResult', this.onImeResult)
                $falcon.off('inputResult', this.onImeResult)
            }
        }
    }
}
</script>