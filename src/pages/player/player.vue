<template>
    <div class="page">
        <!-- 加载/错误覆盖层：铺满视口，加载期遮挡防止误触 -->
        <div v-if="!ready" class="overlay" @click="onScreenTap">
            <text class="hint">{{ error || '加载播放地址...' }}</text>
            <text v-if="loading" class="spinner">●</text>
        </div>

        <!-- 双平面播放器工作区：
             WebView UI 平面（本页）+ 原生视频窗口（waylandsink/kmssink，平面叠加） -->
        <div v-else class="stage" @click="onScreenTap" @touchstart="onPinchStart" @touchmove="onPinchMove" @touchend="onPinchEnd">
            <!-- holE 挖洞：绝对定位铺满页面视口 960×266，使 WebView 画布在该区域完全
                 透明——底层视频层帧直接"透"上来，实现沉浸画面。
                 注意：hole 尺寸必须与 JS 传入 gstPlayer.open 的 pos 参数一致（960×266），
                 否则挖洞区域与 render-rectangle 错位，画面会漏出一圈黑边。 -->
            <hole class="video-hole"></hole>

            <!-- 顶部控制栏（返回 + 标题）：absolute 悬浮，z-index 999 保证在 DOM 最顶层。
                 默认透明不可见（opacity 0 + pointer-events none），点击屏幕唤出。 -->
            <div class="ctrl-top" :class="{ 'ctrl-visible': controlsVisible }">
                <text class="back-btn" @click="closePlayer">‹ 返回</text>
                <text class="bar-title" :lines="1">{{ title }}</text>
            </div>

            <!-- 底部控制栏（按钮行 + 进度条）：绝对定位悬浮，半透明黑底，
                 与顶部栏共用同一显隐状态。
                 【布局】两行：上=按钮行（左回退 / 中播放暂停 / 右快进），下=进度条+时间。 -->
            <div class="ctrl-bottom" :class="{ 'ctrl-visible': controlsVisible }">
                <div class="btn-row">
                    <text class="seek-btn" @click="onSeekBack">回退</text>
                    <text class="mini-play-btn" @click="onTogglePlay">{{ paused ? '▶ 播放' : '⏸ 暂停' }}</text>
                    <text class="seek-btn" @click="onSeekForward">快进</text>
                </div>
                <div class="progress-row">
                    <div class="progress-track" @touchstart="onTrackTouch($event)" @click="onProbe('track', $event)">
                        <!-- 点击热区：40 等分段（每段 928/40=23.2px），
                             事件对象无坐标字段（实测 event keys=type），
                             无法算绝对点击位置，改用分段块直接定位 seek 百分比 -->
                        <div class="progress-hit" v-for="(i, idx) in PROGRESS_BLOCKS" :key="i"
                            :style="{ left: progressHitLeft(i) + '%' }" @click="onBlockTap(i)"
                            @touchstart="onTrackStart(i, $event)" @touchmove="onTrackMove($event)" @touchend="onTrackEnd(i, $event)"></div>
                        <div class="progress-fill" :style="{ width: progressPct() + '%' }" @touchstart="onTrackTouch($event)" @click="onProbe('fill', $event)"></div>
                        <div class="progress-thumb" :style="{ left: progressPct() + '%' }" @touchstart="onTrackTouch($event)" @click="onProbe('thumb', $event)"></div>
                    </div>
                    <text class="time-text" @touchstart="onTrackTouch($event)" @click="onProbe('time', $event)">{{ fmtTime(currentPosition) }} / {{ fmtTime(duration) }}</text>
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

/* 底部控制栏：两行——上=按钮行（回退/播放暂停/快进），下=进度条+时间 */
.ctrl-bottom {
    bottom: 0;
    height: 108px;
    flex-direction: column;
    justify-content: center;
}

/* 按钮行：回退 / 播放暂停 / 快进 均匀分布（进度条上方） */
.btn-row {
    flex-direction: row;
    align-items: center;
    justify-content: space-between;
    width: 928px;               /* 960 - 左右 padding 16*2 */
}

/* 快进/回退按钮：小字按钮 */
.seek-btn {
    font-size: 18px;
    color: #ffffff;
    padding-top: 6px;
    padding-right: 22px;
    padding-bottom: 6px;
    padding-left: 22px;
    border-radius: 4px;
    background-color: #2a2a2a;
    border-width: 1px;
    border-color: #444444;
}

/* 播放/暂停小按钮：bilibili 粉红，位于按钮行中间 */
.mini-play-btn {
    font-size: 18px;
    color: #ffffff;
    padding-top: 6px;
    padding-right: 26px;
    padding-bottom: 6px;
    padding-left: 26px;
    border-radius: 4px;
    background-color: #fb7299;  /* bilibili 粉红 */
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

/* 点击热区块：覆盖整条轨道，事件对象无坐标（实测只有 type），
   无法读取点击位置，故用等分块各自响应 seek 到对应百分比。
   【接触面积】宽 4% 且左右重叠 1.5%（共 40 块覆盖 40 段进度），
   高 40px 向上外扩 15px 覆盖整行——保证手指任意落点必命中热区
   （窄条 2.5%×10px 真机实测经常落空，事件冒泡到 stage 导致控制栏误隐藏） */
.progress-hit {
    position: absolute;
    top: -15px;
    height: 40px;
    width: 4%;                /* 40 段 × 4% = 160%，重叠覆盖消除间隙 */
    background-color: rgba(255, 255, 255, 0.01);   /* 近乎透明：既命中又不遮视觉 */
    z-index: 2;               /* 盖在 fill 之上，保证点击命中热区 */
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
</style>

<script>
import api from '../../utils/api.js'
import { gstPlayer } from 'gstplayer'

const CONTROLS_HIDE_MS = 5000   // 控制栏无操作自动隐藏延时（5 秒）
const PROGRESS_POLL_MS = 1000   // 进度轮询间隔（毫秒）：500→1000 减半 JS-C++ 跨调用，RK3562 上降低 UI 线程开销
const PROGRESS_BLOCKS = 40      // 轨道点击热区分段数（事件对象无坐标，分段定位）
const SEEK_STEP_MS = 10000       // 快进/回退单步时长（10 秒，毫秒）
const PINCH_MIN_SCALE = 0.5      // 双指缩放下限（相对初始矩形）
const PINCH_MAX_SCALE = 2.0      // 双指缩放上限（放大 2 倍后宽度超出屏幕，可平移查看细节）
const PINCH_THROTTLE_MS = 40     // touchmove→setRect 调用节流间隔（JS→C++ 跨调用，减少 UI 线程开销）

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
            btnTapJustOccurred: false, // 按钮点击标志：短路 onScreenTap 的 toggle（防按钮点击冒泡误隐藏）
            duration: 0,               // 总时长（毫秒，getDuration）
            currentPosition: 0,        // 当前播放位置（毫秒，getPosition 轮询）
            progressTimer: null,       // 进度轮询定时器句柄
            mPlayer: null,
            stateCb: null,
            hideTimer: null,           // 自动隐藏定时器句柄
            // 双指缩放：当前渲染矩形（逻辑坐标，与 open pos_* 同坐标系，初始即 open 参数）
            videoRect: { x: 0, y: 107, w: 960, h: 266 },
            pinch: null,               // 活跃捏合快照 {dist, x, y, w, h}；null=未捏合
            pinchTapGuard: false,      // 捏合结束后的下一个 click 吞噬标志
            track: null,               // 进度条触摸快照 {i, t, sliding}
            trackSlideGuard: false     // 滑动结束后的点击吞噬标志（防派生 click 误 toggle/误 seek）
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
                    console.warn('[player] playUrl qn=' + (data.quality || '?') + ' codecid=' + (data.video_codecid || '?') + '(7=H264) len=' + this.playUrl.length + ' : ' + this.playUrl.substring(0, 80) + '...')
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
            // 屏幕点击 toggle 控制栏：隐藏时点击唤出；显示时点击空白区域隐藏。
            // 按钮点击通过 btnTapJustOccurred 标志短路（框架事件冒泡，不支持 .stop 修饰符）
            if (!this.ready) return
            // 双指捏合刚结束：吞掉随之派生的一次 click，避免误toggle 控制栏
            if (this.pinchTapGuard) {
                this.pinchTapGuard = false
                return
            }
            // 进度条滑动刚结束：吞掉派生的 click（防误隐藏控制栏）
            if (this.trackSlideGuard) {
                this.trackSlideGuard = false
                return
            }
            if (this.btnTapJustOccurred) {
                this.btnTapJustOccurred = false
                return
            }
            if (this.controlsVisible) {
                this.hideControls()
            } else {
                this.showControls()
            }
        },
        hideControls() {
            if (this.hideTimer) {
                clearTimeout(this.hideTimer)
                this.hideTimer = null
            }
            this.controlsVisible = false
        },
        onTogglePlay() {
            // 防御：控制栏隐藏/未渲染时不响应（框架若不支持 pointer-events 则防误触）
            if (!this.controlsVisible || !this.mPlayer) return
            this.btnTapJustOccurred = true   // 按钮点击，短路 stage onClick 的隐藏 toggle
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
        progressHitLeft(i) {
            // 第 i 段热区左缘：以段中心 (i-0.5)*2.5% 为中心、宽 4% → 左缘 = 中心 - 2
            // （40 块 × 4% 相互重叠，消除窄条间隙，保证任意落点命中）
            var left = (i - 0.5) * 2.5 - 2
            if (left < 0) left = 0
            if (left > 96) left = 96   // 右缘 clamp：宽 4% → 96+4=100 不溢出轨道
            return left
        },
        onBlockTap(i) {
            // 分段热区点击：直接 seek 到该段对应的进度位置
            if (!this.controlsVisible || !this.mPlayer || !this.duration) return
            // 滑动刚结束：吞掉随之派生到起点块的 click（防跳回起点）
            if (this.trackSlideGuard) {
                this.trackSlideGuard = false
                return
            }
            this.btnTapJustOccurred = true   // 进度条点击，短路 stage onClick 的隐藏 toggle
            var pct = (i - 0.5) / PROGRESS_BLOCKS
            if (pct < 0) pct = 0
            if (pct > 1) pct = 1
            var target = Math.round(this.duration * pct)
            try {
                this.mPlayer.seek(target)
                this.currentPosition = target   // 点击后立即更新进度条，不等下轮轮询
                console.warn('[player] block seek block=' + i + '/' + PROGRESS_BLOCKS + ' target=' + target + 'ms')
            } catch (err) {
                console.warn('[player] seek error: ' + err.message)
            }
        },
        onTrackTouch(e) {
            // 探测触摸事件坐标：changedTouches[0] 是否携带 x/y（用于实现点击+拖动）
            var ct = e && e.changedTouches && e.changedTouches[0]
            console.warn('[player] track touch keys=' + (e ? Object.keys(e).join(',') : 'null')
                + (ct ? ' changedKeys=' + Object.keys(ct).join(',') : '') + (ct ? ' json=' + JSON.stringify(ct).substring(0, 300) : ''))
        },
        onProbe(where, e) {
            // 命中探测：确认哪种元素能收到 click 事件（透明空 div vs 实背景元素）
            var t = e && e.touches && e.touches[0]
            console.warn('[player] probe click on=' + where + ' keys=' + (e ? Object.keys(e).join(',') : 'null')
                + (t ? ' touchKeys=' + Object.keys(t).join(',') : ''))
        },
        // ---- 进度条触摸双保险（深层 div click 不触发 = 详情页同源问题,改用 touch 时序模拟） ----
        // 点击：touchstart→touchend 未移动(<400ms) → seek 对应段块
        // 滑动：框架对一次触摸会重派发 touchstart（详情页 r-item 日志可见 70ms 内双 touchstart）,
        //       若重派发到相邻块则按"滑动路径"即时 seek；无坐标无法做连续拖动，用块级跳变近似
        onTrackStart(i, e) {
            var now = Date.now()
            if (this.track && now - this.track.t < 600) {
                // 同一手势内的 touchstart 重派发：换块 → 滑动路径即时 seek
                if (this.track.i !== i) {
                    this.track.i = i
                    this.track.sliding = true
                    console.warn('[player] track slide block=' + i)
                    this.seekToBlock(i)
                }
                this.track.t = now   // 刷新时间（同块重派发不重置 sliding 标志）
                return
            }
            this.track = { i: i, t: now, sliding: false }
            var ts = e && e.touches
            var t0 = ts && ts[0]
            console.warn('[player] track start block=' + i + ' keys=' + (e ? Object.keys(e).join(',') : 'null')
                + (t0 ? ' touchKeys=' + Object.keys(t0).join(',') : ''))
        },
        onTrackMove(e) {
            var t = e && e.touches && e.touches[0]
            if (t && (typeof t.x === 'number' || (t.point && typeof t.point.x === 'number'))) {
                // 有坐标：未来可实现连续拖动（当前日志确认后再说）
            }
        },
        onTrackEnd(i, e) {
            var tk = this.track
            this.track = null
            if (!tk) return
            var dt = Date.now() - tk.t
            console.warn('[player] track end block=' + i + ' sliding=' + tk.sliding + ' dt=' + dt)
            if (tk.sliding) {
                // 滑动结束：吞掉随后可能派发的 click，防误 toggle/误跳回起点块
                this.trackSlideGuard = true
                var self = this
                setTimeout(function () {
                    if (self.trackSlideGuard) {
                        self.trackSlideGuard = false
                        console.warn('[player] track slide guard auto-reset')
                    }
                }, 500)
                return
            }
            if (dt < 600) {
                // 时序模拟点击：短时抬起 = 点击该段块（click 事件在深层 div 不可靠）
                this.onBlockTap(i)
            }
        },
        // 滑动 seek：直接定位到段块中心对应的进度位置
        seekToBlock(i) {
            if (!this.controlsVisible || !this.mPlayer || !this.duration) return
            if (this.trackSlideGuard) return  // 滑动 guard 期间不响应（防派生 click 抖动）
            var pct = (i - 0.5) / PROGRESS_BLOCKS
            if (pct < 0) pct = 0
            if (pct > 1) pct = 1
            var target = Math.round(this.duration * pct)
            try {
                this.mPlayer.seek(target)
                this.currentPosition = target   // 立即更新进度条，不等下轮轮询
                console.warn('[player] slide seek block=' + i + '/' + PROGRESS_BLOCKS + ' target=' + target + 'ms')
            } catch (err) {
                console.warn('[player] slide seek error: ' + err.message)
            }
        },
        // ---- 双指缩放（捏合） ----
        // 手势流程：touchstart 记录起始矩形与双指间距 → touchmove 按间距比例
        // 缩放（锚定两指中心）+ 平移 → 节流调用原生 setRect 动态更新渲染区域 →
        // touchend 复位。坐标依赖框架 touch 事件是否携带 points 数据，
        // 设备日志可确认（见 onTrackTouch）。
        onPinchStart(e) {
            var ts = e && e.touches
            if (!ts || ts.length < 2) return
            var t0 = ts[0]
            var t1 = ts[1]
            var p0 = t0 && t0.point ? t0.point : t0
            var p1 = t1 && t1.point ? t1.point : t1
            if (!p0 || !p1 ||
                typeof p0.x !== 'number' || typeof p0.y !== 'number' ||
                typeof p1.x !== 'number' || typeof p1.y !== 'number') {
                console.warn('[player] pinch start: no touch coords, keys='
                    + (t0 ? Object.keys(t0).join(',') : 'null'))
                this.pinch = null
                return
            }
            var dx = p1.x - p0.x
            var dy = p1.y - p0.y
            this.pinch = {
                dist: Math.sqrt(dx * dx + dy * dy),
                x: this.videoRect.x,
                y: this.videoRect.y,
                w: this.videoRect.w,
                h: this.videoRect.h
            }
            console.log('[player] pinch start dist=' + this.pinch.dist
                + ' rect=' + this.videoRect.x + ',' + this.videoRect.y + ','
                + this.videoRect.w + ',' + this.videoRect.h)
        },
        onPinchMove(e) {
            if (!this.pinch) return
            var t = e && e.touches
            if (!t || t.length < 2) return
            var t0 = t[0]
            var t1 = t[1]
            var p0 = t0 && t0.point ? t0.point : t0
            var p1 = t1 && t1.point ? t1.point : t1
            if (!p0 || !p1 ||
                typeof p0.x !== 'number' || typeof p1.x !== 'number') return
            var dx = p1.x - p0.x
            var dy = p1.y - p0.y
            var dist = Math.sqrt(dx * dx + dy * dy)
            var p = this.pinch
            if (p.dist <= 0) return
            var scale = dist / p.dist   // 相对起始间距的比例
            if (scale < PINCH_MIN_SCALE) scale = PINCH_MIN_SCALE
            if (scale > PINCH_MAX_SCALE) scale = PINCH_MAX_SCALE
            var cxp = (p0.x + p1.x) / 2   // 两指当前中心（逻辑坐标）
            var cyp = (p0.y + p1.y) / 2
            var w = Math.round(p.w * scale)
            var h = Math.round(p.h * scale)
            // 以两指中心为锚：新中心 = 手势中心；矩形左上角随之移动
            var x = Math.round(cxp - w / 2)
            var y = Math.round(cyp - h / 2)
            // 边界钳制：始终保留 80×40 可见（放大超屏后可平移，不可完全移出）
            var minX = 80 - w
            var minY = 40 - h
            var maxX = 960 - 80
            var maxY = 266 - 40
            if (x < minX) x = minX
            if (x > maxX) x = maxX
            if (y < minY) y = minY
            if (y > maxY) y = maxY
            // 节流：避免每帧跨 JS→C++ 调用（40ms 一次已足够顺滑）
            var now = Date.now()
            if (now - (this._pinchLastT || 0) < PINCH_THROTTLE_MS) return
            this._pinchLastT = now
            this.applyVideoRect(x, y, w, h)
        },
        onPinchEnd() {
            if (!this.pinch) return
            this.pinch = null
            this._pinchLastT = 0
            // 捏合触发的 touchend 后框架可能派发一次 click，置 guard 吞掉；
            // 若未派发 click（guard 无人消费），500ms 后自动复位防卡死
            this.pinchTapGuard = true
            var self = this
            setTimeout(function () {
                if (self.pinchTapGuard) {
                    self.pinchTapGuard = false
                    console.log('[player] pinch guard auto-reset')
                }
            }, 500)
            console.log('[player] pinch end')
        },
        applyVideoRect(x, y, w, h) {
            if (!this.mPlayer) return
            try {
                this.mPlayer.setRect(x, y, w, h)
                this.videoRect = { x: x, y: y, w: w, h: h }
                console.log('[player] setRect ' + x + ',' + y + ',' + w + ',' + h)
            } catch (err) {
                console.warn('[player] setRect error: ' + err.message)
            }
        },
        // ---- 快进 / 回退（相对当前播放位置 ±10s） ----
        onSeekForward() {
            // 防御：控制栏隐藏/未渲染时不响应
            if (!this.controlsVisible || !this.mPlayer) return
            this.btnTapJustOccurred = true   // 按钮点击，短路 stage onClick 的隐藏 toggle
            this.seekBy(SEEK_STEP_MS)
        },
        onSeekBack() {
            if (!this.controlsVisible || !this.mPlayer) return
            this.btnTapJustOccurred = true   // 按钮点击，短路 stage onClick 的隐藏 toggle
            this.seekBy(-SEEK_STEP_MS)
        },
        seekBy(deltaMs) {
            if (!this.mPlayer) return
            var base = typeof this.currentPosition === 'number' ? this.currentPosition : 0
            var target = base + deltaMs
            if (target < 0) target = 0
            if (this.duration && target > this.duration) target = this.duration
            try {
                this.mPlayer.seek(target)
                this.currentPosition = target   // 立即更新进度条，不等下轮轮询
                console.warn('[player] seekBy ' + deltaMs + 'ms -> ' + target + 'ms')
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
            // 返回前一个页面：page.finish() 关闭当前页面（框架文档确认：
            // $falcon.closePage() 不存在，closePageByName/ById 仅系统级应用可用；
            // 普通应用正确返回方式是 this.$page.finish()）。
            try {
                this.$page.finish()
            } catch (e) {
                console.warn('[player] finish error: ' + (e ? e.message : e))
            }
        }
    }
}
</script>