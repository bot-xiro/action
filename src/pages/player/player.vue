<template>
    <div class="page">
        <!-- 加载/错误覆盖层：铺满视口，加载期遮挡防止误触 -->
        <div v-if="!ready" class="overlay" @click="onScreenTap">
            <text class="hint">{{ error || '加载播放地址...' }}</text>
            <text v-if="loading" class="spinner">●</text>
        </div>

        <!-- 双平面播放器工作区：
             WebView UI 平面（本页）+ KMS 硬件叠加视频平面（kmssink, plane-id=75） -->
        <div v-else class="stage" @click="onScreenTap">
            <!-- holE 挖洞：绝对定位铺满整个 960×480 屏幕，使 WebView 画布在该区域完全
                 透明——底层 KMS Overlay 视频帧直接“透”上来，实现全屏沉浸画面。
                 注意：hole 尺寸必须与 JS 传入 gstPlayer.open 的 pos 参数一致（960×480），
                 否则挖洞区域与 kmssink render-rectangle 错位，画面会漏出一圈黑边。 -->
            <hole class="video-hole"></hole>

            <!-- 顶部控制栏（返回 + 标题）：absolute 悬浮，z-index 999 保证在 DOM 最顶层。
                 默认透明不可见（opacity 0 + pointer-events none），点击屏幕唤出。 -->
            <div class="ctrl-top" :class="{ 'ctrl-visible': controlsVisible }">
                <text class="back-btn" @click="closePlayer">‹ 返回</text>
                <text class="bar-title" :lines="1">{{ title }}</text>
            </div>

            <!-- 底部控制栏（播放/暂停 + 进度条）：同样绝对定位悬浮，半透明黑底，
                 与顶部栏共用同一显隐状态。 -->
            <div class="ctrl-bottom" :class="{ 'ctrl-visible': controlsVisible }">
                <div class="progress-row">
                    <div class="progress-track" @click="onProgressTap">
                        <div class="progress-fill" :style="{ width: progressPct() + '%' }"></div>
                        <div class="progress-thumb" :style="{ left: progressPct() + '%' }"></div>
                    </div>
                    <text class="time-text">{{ fmtTime(currentPosition) }} / {{ fmtTime(duration) }}</text>
                </div>
                <div class="btn-row">
                    <text class="play-btn" @click="onTogglePlay">{{ paused ? '▶ 播放' : '⏸ 暂停' }}</text>
                </div>
            </div>
        </div>
    </div>
</template>

<style scoped>
/* 外层容器：铺满 960×480 全屏，背景纯黑（视频之外的区域保持黑边效果） */
.page {
    flex: 1;
    background-color: #000000;
    flex-direction: column;
}

/* 加载/错误覆盖层 */
.overlay {
    position: absolute;
    top: 0;
    left: 0;
    width: 960px;
    height: 266px;
    align-items: center;
    justify-content: center;
    background-color: #000000;
    z-index: 10;
}

.hint {
    font-size: 22px;
    color: #cccccc;
    text-align: center;
    width: 80%;
}

.spinner {
    margin-top: 12px;
    font-size: 28px;
    color: #fb7299;
    animation-name: spin;
    animation-duration: 1000ms;
    animation-timing-function: linear;
    animation-iteration-count: infinite;
}

@keyframes spin {
    from { transform: rotate(0deg); }
    to { transform: rotate(360deg); }
}

/* ---- 播放器工作区：覆盖播放页视口 960×266（设备真实视口，见 cap_index_33.png
     物证），absolute 脱离文档流承载 hole 与悬浮控制栏 ---- */
.stage {
    position: absolute;
    top: 0;
    left: 0;
    width: 960px;
    height: 266px;          /* 视口高度：页面恒为 960×266 长条屏 */
    background-color: #000000;
    overflow: hidden;
}

/* 挖洞区域：覆盖整个视口（top:0 是页面坐标；页面视口 960×266 正好对应
   逻辑全屏 y=107~373 的视频条——物理竖条 266 宽居中，逻辑高 266。
   严禁再偏移 top:107px：那会在视口内显示成"下半截洞"，上半部成黑块）
   与 gstPlayer.open({ pos_x: 0, pos_y: 107, pos_w: 960, pos_h: 266 }) 对应：
   pos_y=107 是【逻辑全屏】坐标（视频在 480 高逻辑屏中垂直居中），
   页面在逻辑屏中的位置是 y=107 起，故洞用页面坐标 top:0、高 266。
   此区域 WebView 画布全透明，KMS 视频平面透出。 */
.video-hole {
    position: absolute;
    top: 0;
    left: 0;
    width: 960px;
    height: 266px;
    z-index: 1;
}

/* ---- 悬浮控制栏通用样式：默认隐藏，显示时半透明黑底 ---- */
.ctrl-top,
.ctrl-bottom {
    position: absolute;
    left: 0;
    width: 960px;
    z-index: 999;               /* DOM 最顶层，保证盖在 hole 之上 */
    padding-left: 16px;
    padding-right: 16px;
    background-color: rgba(0, 0, 0, 0.5);   /* 半透明黑底：既保证可读性又不完全遮画面 */
    opacity: 0;                 /* 默认透明不可见，全屏沉浸播放 */
    pointer-events: none;       /* 不可点击，点击事件穿透到 stage 唤出控制栏 */
    transition: opacity 0.3s;   /* 淡入淡出（框架若不支持 transition 则退化为瞬间切换） */
}

.ctrl-top {
    top: 0;
    height: 60px;
    flex-direction: row;
    align-items: center;
}

/* 底部控制栏改两行布局：上=进度条行，下=播放按钮行 */
.ctrl-bottom {
    bottom: 0;
    height: 104px;
    flex-direction: column;
    justify-content: center;
}

/* 进度条行：轨道(可点击) + 时间文本 */
.progress-row {
    flex-direction: row;
    align-items: center;
    width: 928px;               /* 960 - 左右 padding 16*2 */
}

/* 轨道：相对定位承载 fill/thumb 绝对定位子元素 */
.progress-track {
    position: relative;
    flex: 1;
    height: 10px;
    border-radius: 5px;
    background-color: rgba(255, 255, 255, 0.3);
}

/* 已播放填充条：宽度由 JS 轮询进度动态设置 */
.progress-fill {
    position: absolute;
    left: 0;
    top: 0;
    bottom: 0;
    border-radius: 5px;
    background-color: #fb7299;
}

/* 进度圆点 */
.progress-thumb {
    position: absolute;
    top: -3px;
    width: 16px;
    height: 16px;
    border-radius: 8px;
    background-color: #ffffff;
}

.time-text {
    margin-left: 16px;
    font-size: 16px;
    color: #ffffff;
}

.btn-row {
    flex-direction: row;
    align-items: center;
    margin-top: 14px;
    width: 928px;
}

/* 唤出状态：完全不透明、可交互 */
.ctrl-visible {
    opacity: 1;
    pointer-events: auto;
}

.back-btn {
    font-size: 20px;
    color: #ffffff;
    padding-top: 8px;
    padding-right: 14px;
    padding-bottom: 8px;
    padding-left: 14px;
    border-radius: 4px;
    border-width: 1px;
    border-color: rgba(255, 255, 255, 0.35);
}

.bar-title {
    flex: 1;
    margin-left: 16px;
    font-size: 20px;
    color: #ffffff;
    lines: 1;
}

.play-btn {
    font-size: 20px;
    color: #ffffff;
    padding-top: 8px;
    padding-right: 18px;
    padding-bottom: 8px;
    padding-left: 18px;
    border-radius: 4px;
    background-color: #fb7299;  /* bilibili 粉红点缀 */
}
</style>

<script>
import api from '../../utils/api.js'
import { gstPlayer } from 'gstplayer'

const CONTROLS_HIDE_MS = 5000   // 控制栏无操作自动隐藏延时（5 秒）
const PROGRESS_POLL_MS = 500    // 进度轮询间隔（毫秒）
const TRACK_LEFT = 16           // 进度条轨道左缘（页面坐标，与 padding 一致）
const TRACK_W = 928             // 轨道可视宽度 = 960 - 2*16 padding

export default {
    name: 'player',
    data() {
        return {
            bvid: '',
            cid: '',
            title: '视频播放',
            playUrl: '',
            ready: false,
            loading: true,
            paused: false,
            error: '',
            controlsVisible: false,   // 控制栏显隐状态（默认隐藏）
            duration: 0,               // 总时长（毫秒，getDuration）
            currentPosition: 0,        // 当前播放位置（毫秒，getPosition 轮询）
            progressTimer: null,       // 进度轮询定时器句柄
            mPlayer: null,
            stateCb: null,
            hideTimer: null           // 自动隐藏定时器句柄
        }
    },
    mounted() {
        var opt = this.$page.loadOptions || {}
        this.bvid = opt.bvid || ''
        this.cid = opt.cid || ''
        this.title = opt.title || '视频播放'
        console.warn('[player] mounted bvid=' + this.bvid + ' cid=' + this.cid)

        // 注册播放器状态事件（stateChanged.on/off）
        this.stateCb = (state) => {
            console.warn('[player] stateChanged: ' + state)
            if (state === 'playing') {
                this.paused = false
                // 进入播放后控制栏保持可见 5 秒再自动隐藏（首次唤出便于用户发现）
                this.showControls()
                // 播放中启动 500ms 进度轮询（获时长/位置驱动进度条）
                this.startProgressPolling()
            } else if (state === 'paused') {
                this.paused = true
                // 暂停时控制栏常驻，便于用户再次点击播放
                this.showControls()
                if (this.hideTimer) {
                    clearTimeout(this.hideTimer)
                    this.hideTimer = null
                }
                // 暂停时停止轮询（位置定格，节省 CPU）
                this.stopProgressPolling()
            } else if (state === 'ended') {
                this.error = '播放结束'
                this.ready = false
                this.loading = false
                this.stopProgressPolling()
            } else if (state && state.indexOf('error:') === 0) {
                this.error = '播放错误: ' + state.substring(6)
                this.ready = false
                this.loading = false
                this.stopProgressPolling()
            }
        }
        try {
            gstPlayer.stateChanged.on(this.stateCb)
        } catch (e) {
            console.warn('[player] stateChanged.on error: ' + e.message)
        }

        this.loadPlayUrl()
    },
    beforeDestroy() {
        console.warn('[player] beforeDestroy')
        this.stopProgressPolling()
        if (this.hideTimer) {
            clearTimeout(this.hideTimer)
            this.hideTimer = null
        }
        if (this.stateCb) {
            try { gstPlayer.stateChanged.off(this.stateCb) } catch (e) { }
        }
        if (this.mPlayer) {
            try { this.mPlayer.close() } catch (e) { }
            this.mPlayer = null
        }
    },
    methods: {
        async loadPlayUrl() {
            if (!this.bvid || !this.cid) {
                this.error = '缺少播放参数 ' + JSON.stringify(this.$page.loadOptions)
                this.loading = false
                return
            }
            try {
                var data = await api.getPlayUrl(this.bvid, this.cid, 64, 1)
                if (data && data.durl && data.durl.length > 0) {
                    this.playUrl = data.durl[0].url
                    console.warn('[player] playUrl qn=' + (data.quality || '?') + ' len=' + this.playUrl.length + ' : ' + this.playUrl.substring(0, 80) + '...')
                    this.tryPlay(this.playUrl)
                } else {
                    this.error = '未获取到播放地址'
                    this.loading = false
                    console.warn('[player] no durl in response: ' + JSON.stringify(data).substring(0, 200))
                }
            } catch (e) {
                var msg = (e && e.message) ? e.message : JSON.stringify(e)
                this.error = '获取播放地址失败: ' + msg
                this.loading = false
                console.warn('[player] getPlayUrl error: ' + msg)
            }
        },
        tryPlay(url) {
            // gstplayer 为单例，直接方法调用；open/start 为同步方法。
            // 注意：KMS 双平面模式下 pos 传逻辑坐标。
            // 【画面尺寸不变原则】：不传 960×480 全屏（会把视频放大），
            // 保持 960×266 视口尺寸（视频等比 fit，画面不变大），
            // pos_y=107 使物理竖条 x=107 居中 → 逻辑 y=107~373 垂直居中，
            // 画面既不偏上也不偏下。
            this.mPlayer = gstPlayer
            try {
                var ok = this.mPlayer.open({
                    uri: url,
                    audio: true,
                    pos_x: 0,
                    pos_y: 107,
                    pos_w: 960,
                    pos_h: 266,
                    // fill 在 KMS 模式下无意义（几何由 render-rectangle 决定），不传
                    loop: 0
                })
                console.warn('[player] open ret: ' + ok)
                this.mPlayer.start()
                this.ready = true
                this.loading = false
                console.warn('[player] play OK')
                // 播放开始后先唤出控制栏，5 秒自动隐藏
                this.showControls()
            } catch (e) {
                var msg = (e && e.message) ? e.message : JSON.stringify(e)
                this.error = '播放失败: ' + msg
                this.loading = false
                console.warn('[player] play error: ' + msg)
            }
        },
        // ---- 控制栏显隐逻辑 ----
        showControls() {
            this.controlsVisible = true
            // 回复 5 秒自动隐藏计时（暂停时由 stateChanged 分支常驻，不在此重启）
            if (!this.paused && this.hideTimer === null) {
                var self = this
                this.hideTimer = setTimeout(function () {
                    self.controlsVisible = false
                    self.hideTimer = null
                    console.warn('[player] controls auto-hide')
                }, CONTROLS_HIDE_MS)
            }
        },
        onScreenTap() {
            // 任意点击屏幕：唤出控制栏并重置自动隐藏计时（限已就绪状态）
            if (!this.ready) return
            if (this.hideTimer) {
                clearTimeout(this.hideTimer)
                this.hideTimer = null
            }
            this.showControls()
        },
        onTogglePlay() {
            // 防御：控制栏隐藏/未渲染时不响应（框架若不支持 pointer-events 则防误触）
            if (!this.controlsVisible || !this.mPlayer) return
            this.togglePlay()
        },
        togglePlay() {
            if (!this.mPlayer) return
            this.paused = !this.paused
            if (this.paused) {
                try { this.mPlayer.pause() } catch (e) { }
            } else {
                try { this.mPlayer.resume() } catch (e) { }
            }
            console.warn('[player] ' + (this.paused ? 'paused' : 'resumed'))
        },
        // ---- 进度条：轮询 / 点击跳转 ----
        // 轮询用 setTimeout 递归（与 hideTimer 同款机制，WebView 子集兼容性好），
        // 每 500ms 从原生拉取一次时长/位置刷新进度条。
        startProgressPolling() {
            if (this.progressTimer || !this.mPlayer) return
            var self = this
            var tick = function () {
                self.pollProgress()
                self.progressTimer = setTimeout(tick, PROGRESS_POLL_MS)
            }
            tick()   // 立即首查，避免进度条 500ms 空白
        },
        stopProgressPolling() {
            if (this.progressTimer) {
                clearTimeout(this.progressTimer)
                this.progressTimer = null
            }
        },
        pollProgress() {
            if (!this.mPlayer) return
            try {
                var pos = this.mPlayer.getPosition()
                var dur = this.mPlayer.getDuration()
                if (typeof pos === 'number' && pos >= 0) this.currentPosition = Math.round(pos)
                if (typeof dur === 'number' && dur > 0) this.duration = Math.round(dur)
            } catch (e) {
                // 查询失败静默：pipeline 未就绪等场景，下轮再试
            }
        },
        progressPct() {
            if (!this.duration) return 0
            var pct = this.currentPosition * 100 / this.duration
            if (pct < 0) pct = 0
            if (pct > 100) pct = 100
            return pct
        },
        fmtTime(ms) {
            ms = Math.max(0, Math.floor(ms || 0))
            var s = Math.floor(ms / 1000)
            var m = Math.floor(s / 60)
            var h = Math.floor(m / 60)
            s = s % 60
            m = m % 60
            function pad(n) { return n < 10 ? '0' + n : '' + n }
            return (h > 0 ? pad(h) + ':' : '') + pad(m) + ':' + pad(s)
        },
        onProgressTap(e) {
            // 点击进度条跳转：事件坐标格式因 WebView 子集而异，
            // 优先 offsetX（相对轨道自身），退化到 pageX/clientX（页面坐标减轨道左缘）。
            // 若事件对象无任何坐标字段，降级为不跳转（打印事件键便于真机适配）。
            if (!this.controlsVisible || !this.mPlayer || !this.duration) return
            var x = -1
            if (e) {
                if (typeof e.offsetX === 'number') x = e.offsetX
                else if (typeof e.layerX === 'number') x = e.layerX
                else if (typeof e.pageX === 'number') x = e.pageX - TRACK_LEFT
                else if (typeof e.clientX === 'number') x = e.clientX - TRACK_LEFT
            }
            if (x < 0) {
                console.warn('[player] progress tap: no coords, event keys=' + (e ? Object.keys(e).join(',') : 'null'))
                return
            }
            var pct = x / TRACK_W
            if (pct < 0) pct = 0
            if (pct > 1) pct = 1
            var target = Math.round(this.duration * pct)
            try {
                this.mPlayer.seek(target)
                this.currentPosition = target   // 点击后立即更新进度条，不等下轮轮询
                console.warn('[player] seek ' + target + 'ms (pct=' + Math.round(pct * 100) + '%)')
            } catch (err) {
                console.warn('[player] seek error: ' + err.message)
            }
        },
        goPlayerBack() {
            if (!this.controlsVisible) return
            this.closePlayer()
        },
        closePlayer() {
            if (this.mPlayer) {
                try { this.mPlayer.close() } catch (e) { }
                this.mPlayer = null
            }
            $falcon.closePage()
        }
    }
}
</script>