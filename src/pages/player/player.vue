<template>
    <div class="page">
        <!-- 加载/错误覆盖层：铺满视口；仅在视频 plane 激活前可见（打开失败/加载中） -->
        <div v-if="!ready" class="overlay">
            <text class="hint">{{ error || '加载播放地址...' }}</text>
            <text v-if="loading" class="spinner">●</text>
        </div>

        <!-- 全屏视频工作区（2026-08-14 重构）：
             视频 plane 覆盖整个 960×266 视口（open pos = 全视口，KMSSINK 硬件
             等比缩放 + 黑边，不拉伸）；悬浮控制栏由原生 gdkpixbufoverlay 合入
             视频帧（native/ControlBar.cpp，布局常量与本页 BAR 常量一一对应）。
             本页负责触摸命中测试（触摸输入与平面无关，视频置顶仍可收到触摸）：
             轨道 → 点击/拖动 seek；按钮区 → 返回/快退/播放/快进；视频区 → 切换控制栏显隐。 -->
        <div v-else class="stage" @touchstart="onStageTouchStart($event)"
            @touchmove="onStageTouchMove($event)" @touchend="onStageTouchEnd($event)">
        </div>
    </div>
</template>

<style scoped>
/* 外层容器：铺满 960×266 视口。透明底（视频 plane 在 UI plane 下方 zpos=2<3，
   UI plane 必须透明才能让视频透出；仅加载/错误时 overlay 提供半透明遮罩） */
.page {
    flex: 1;
    background-color: transparent;
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
    background-color: rgba(0, 0, 0, 0.8);
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

/* 全屏工作区：覆盖整个 960×266 视口（视频 plane 在其下，触摸命中全屏） */
.stage {
    position: absolute;
    top: 0;
    left: 0;
    width: 960px;
    height: 266px;
    background-color: transparent;
    overflow: hidden;
}
</style>

<script>
import api from '../../utils/api.js'
import settings from '../../utils/settings.js'
import { gstPlayer } from 'gstplayer'

const PROGRESS_POLL_MS = 1000   // 进度轮询间隔（毫秒）：JS→C++ 跨调用 + 原生重绘，1s 已足够
const SEEK_STEP_MS = 10000       // 快进/回退单步时长（10 秒，毫秒）
const TRACK_MOVE_THROTTLE_MS = 60 // 拖动 seek 节流
const BAR_HIDE_MS = 6000         // 播放中控制栏无操作自动隐藏（6 秒）
const LOGIC_TOP = 107            // 页面左上角在逻辑屏中的 y（全视口 open pos_y=107）

// 悬浮控制栏几何（与 native/ControlBar.cpp bargeom 一一对应，用户空间 960×266）
// 【2026-08-14 放大】按键与 seek 轨道加大：轨道 22 高、按钮行 36 高、命中区放宽
const BAR = {
    top: 190,                    // 控制栏顶
    trackY: 196, trackH: 22,     // 进度轨道
    trackL: 24, trackR: 936,
    btnY: 226, btnH: 36,         // 按钮行
    // 【2026-08-14 删除左下角返回】backL/backR 已废弃
    sbkL: 330, sbkR: 430,
    playL: 448, playR: 512,
    sfwL: 530, sfwR: 630,
    // 【2026-08-14 返回左上角】标题区返回按钮命中区
    titleBackY: 0, titleBackH: 40,
    titleBackL: 8, titleBackR: 48
}

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
            ended: false,
            error: '',
            barVisible: true,        // 悬浮控制栏显隐（原生叠加层）
            duration: 0,             // 总时长（毫秒）
            currentPosition: 0,      // 当前播放位置（毫秒）
            progressTimer: null,
            hideTimer: null,
            mPlayer: null,
            stateCb: null,
            playerMode: settings.DEFAULT_MODE,
            trackDrag: null,         // 轨道拖动快照 {startX, startPct}
            _lastTrackStartT: null,
            _lastTrackMoveX: null,
            _barGuardT: null         // 按钮触摸防派生 click 时间戳
        }
    },
    mounted() {
        var opt = this.$page.loadOptions || {}
        this.bvid = opt.bvid || ''
        this.cid = opt.cid || ''
        this.title = opt.title || '视频播放'
        console.warn('[player] mounted bvid=' + this.bvid + ' cid=' + this.cid)

        // 播放器状态事件（原生 bus 线程 → JS 线程）
        this.stateCb = (state) => {
            console.warn('[player] stateChanged: ' + state)
            if (state === 'playing') {
                this.paused = false
                this.ended = false
                this.error = ''
                this.startProgressPolling()
                this.scheduleBarHide()
            } else if (state === 'paused') {
                this.paused = true
                this.stopProgressPolling()
                this.cancelBarHide()
                this.barVisible = true
                this.pushBarState()
            } else if (state === 'ended') {
                this.ended = true
                this.paused = true
                this.stopProgressPolling()
                this.cancelBarHide()
                this.barVisible = true
                this.pushBarState()
                console.warn('[player] ended, tap play to replay')
            } else if (state && state.indexOf('error:') === 0) {
                this.error = '播放错误: ' + state.substring(6)
                this.paused = true
                this.stopProgressPolling()
                this.cancelBarHide()
                this.barVisible = true
                // 保持 stage 挂载（原生控制栏显示错误标记，返回按钮可退出）
                this.pushBarState()
                console.warn('[player] runtime error: ' + this.error)
            }
        }
        try {
            gstPlayer.stateChanged.on(this.stateCb)
        } catch (e) {
            console.warn('[player] stateChanged.on error: ' + e.message)
        }

        var self = this
        settings.getMode().then(function (m) {
            self.playerMode = m
            console.warn('[player] playerMode=' + m)
            self.loadPlayUrl()
        })
    },
    beforeDestroy() {
        console.warn('[player] beforeDestroy')
        this.stopProgressPolling()
        this.cancelBarHide()
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
            // 直连模式：bvid 参数直接放完整播放 URL（cid 留空）
            if (this.bvid && !this.cid) {
                console.warn('[player] direct url=' + this.bvid)
                this.tryPlay(this.bvid)
                return
            }
            if (!this.bvid || !this.cid) {
                this.error = '缺少播放参数 ' + JSON.stringify(this.$page.loadOptions)
                this.loading = false
                return
            }
            try {
                var data = await api.getPlayUrl(this.bvid, this.cid, 64, 1)
                if (data && data.durl && data.durl.length > 0) {
                    this.playUrl = data.durl[0].url
                    // 【2026-08-14 时长修复】设备原生 getDuration 查询恒为 0（rk 分支
                    // 查询链路问题），改用 B 站 API 返回的 timelength（毫秒）驱动进度条
                    if (data.timelength && data.timelength > 0) {
                        this.duration = Math.round(data.timelength)
                        console.warn('[player] duration from API: ' + this.duration + 'ms')
                    }
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
            // 【2026-08-14 全屏+悬浮栏】open 传全视口矩形（页面 0,0,960,266 →
            // 逻辑 0,107,960,266），KMSSINK 硬件等比缩放铺满（黑边留白不拉伸）；
            // 悬浮控制栏由原生合入视频帧。system 模式仍关闭（见 git 历史）。
            this.mPlayer = gstPlayer
            try {
                var ok = this.mPlayer.open({
                    uri: url,
                    audio: true,
                    pos_x: 0,
                    pos_y: LOGIC_TOP,
                    pos_w: 960,
                    pos_h: 266,
                    loop: 0
                })
                console.warn('[player] open ret: ' + ok)
                // 视频平面固定 zpos=0（UI 平面 zpos=3），控制栏已合成进视频帧
                this.mPlayer.start()
                this.ready = true
                this.loading = false
                this.pushBarState()
                console.warn('[player] play OK')
            } catch (e) {
                var msg = (e && e.message) ? e.message : JSON.stringify(e)
                this.error = '播放失败: ' + msg
                this.loading = false
                console.warn('[player] play error: ' + msg)
            }
        },
        // ---- 悬浮控制栏状态（原生 gdkpixbufoverlay 重绘）----
        pushBarState() {
            if (!this.mPlayer) return
            try {
                this.mPlayer.setBarState({
                    visible: this.barVisible,
                    playing: !this.paused && !this.ended,
                    ended: !!this.ended,
                    error: !!this.error,
                    position: this.currentPosition,
                    duration: this.duration,
                    title: this.title
                })
            } catch (e) {
                console.warn('[player] setBarState error: ' + (e && e.message))
            }
        },
        toggleBar() {
            this.barVisible = !this.barVisible
            console.warn('[player] bar ' + (this.barVisible ? 'shown' : 'hidden'))
            this.pushBarState()
            if (this.barVisible) this.scheduleBarHide()
        },
        scheduleBarHide() {
            var self = this
            this.cancelBarHide()
            if (this.paused || this.ended || !this.ready) return
            this.hideTimer = setTimeout(function () {
                self.hideTimer = null
                if (!self.paused && !self.ended && self.barVisible) {
                    self.barVisible = false
                    self.pushBarState()
                    console.warn('[player] bar auto-hidden')
                }
            }, BAR_HIDE_MS)
        },
        cancelBarHide() {
            if (this.hideTimer) {
                clearTimeout(this.hideTimer)
                this.hideTimer = null
            }
        },
        // ---- 触摸命中（触摸输入与视频 plane 无关：视频置顶仍可收到）----
        touchPoint(e) {
            var ct = e && e.changedTouches && e.changedTouches[0]
            if (ct && typeof ct.pageX === 'number' && typeof ct.pageY === 'number') {
                return { x: ct.pageX, y: ct.pageY }
            }
            var t = e && e.touches && e.touches[0]
            if (t && typeof t.pageX === 'number' && typeof t.pageY === 'number') {
                return { x: t.pageX, y: t.pageY }
            }
            return null
        },
        onStageTouchStart(e) {
            if (!this.ready || !this.mPlayer) return
            var p = this.touchPoint(e)
            if (!p) {
                console.warn('[player] stage touch: no coords keys=' + Object.keys(e || {}).join(','))
                return
            }
            var x = p.x, y = p.y
            // 【2026-08-14 返回左上角】标题区返回按钮（左上角 y 0-40）
            if (y >= BAR.titleBackY && y <= BAR.titleBackY + BAR.titleBackH &&
                x >= BAR.titleBackL && x <= BAR.titleBackR) {
                console.warn('[player] title back tap -> close')
                this.closePlayer()
                return
            }
            // 进度轨道：点击立即 seek + 记录拖动锚点（重派发 120ms 内只更新锚点）
            if (y >= BAR.trackY - 4 && y <= BAR.trackY + BAR.trackH + 6) {
                var now = Date.now()
                var reDispatch = (typeof this._lastTrackStartT === 'number') && (now - this._lastTrackStartT < 120)
                this._lastTrackStartT = now
                this.trackDrag = { startX: x, startPct: this.progressPct() / 100 }
                if (!reDispatch) {
                    this.seekFromTouchX(x, BAR.trackL, BAR.trackR - BAR.trackL, 'tap')
                }
                // 【自动隐藏修复 2026-08-14】轨道交互重置自动隐藏计时
                this.scheduleBarHide()
                return
            }
            // 控制栏按钮区
            if (y >= BAR.top) {
                if (!this.barVisible) {
                    // 控制栏隐藏时点击该区域：先唤出控制栏，不触发按钮
                    console.warn('[player] bar region tap while hidden -> show bar')
                    this.toggleBar()
                    return
                }
                this._barGuardT = Date.now()
                if (y >= BAR.btnY && y <= BAR.btnY + BAR.btnH) {
                    // 【2026-08-14 删除左下角返回】返回按钮已移到左上角标题区
                    if (x >= BAR.sbkL && x <= BAR.sbkR) { this.onSeekBack(); return }
                    if (x >= BAR.playL && x <= BAR.playR) { this.onTogglePlay(); return }
                    if (x >= BAR.sfwL && x <= BAR.sfwR) { this.onSeekForward(); return }
                }
                // 控制栏区域内空白：不切换显隐，但重置自动隐藏计时
                // 【自动隐藏修复 2026-08-14】此前 bar 区交互一律 cancelBarHide()
                // 且空白点击不恢复 scheduleBarHide() → 控制栏永不自动隐藏
                this.scheduleBarHide()
                return
            }
            // 视频区域：切换控制栏显隐
            console.warn('[player] video tap x=' + x + ' y=' + y + ' barVisible=' + this.barVisible)
            this.toggleBar()
        },
        onStageTouchMove(e) {
            if (!this.trackDrag || !this.mPlayer || !this.duration) return
            var p = this.touchPoint(e)
            if (!p) return
            var x = p.x
            if (typeof this._lastTrackMoveX === 'number' && Math.abs(x - this._lastTrackMoveX) < 4) return
            var now = Date.now()
            if (now - (this._lastTrackMoveT || 0) < TRACK_MOVE_THROTTLE_MS) return
            this._lastTrackMoveT = now
            this._lastTrackMoveX = x
            var dX = x - this.trackDrag.startX
            var pct = this.trackDrag.startPct + dX / (BAR.trackR - BAR.trackL)
            if (pct < 0) pct = 0
            if (pct > 1) pct = 1
            var target = Math.round(this.duration * pct)
            console.warn('[player] track drag x=' + x + ' pct=' + Math.round(pct * 100) + '% target=' + target + 'ms')
            this.seekTo(target, 'drag')
            // 【自动隐藏修复 2026-08-14】拖动过程持续重置自动隐藏计时
            this.scheduleBarHide()
        },
        onStageTouchEnd(e) {
            if (this.trackDrag) {
                this.trackDrag = null
                this._lastTrackMoveX = null
                console.warn('[player] track end')
            }
            // 轨道交互后可能派生 click（无 click 处理器，无需 guard；保留日志）
        },
        // ---- 播放控制 ----
        onTogglePlay() {
            this.togglePlay()
        },
        togglePlay() {
            if (!this.mPlayer) return
            if (this.ended) {
                // 播放结束 → 重播：seek 0 + resume
                console.warn('[player] replay from ended')
                this.ended = false
                this.paused = false
                try {
                    this.mPlayer.seek(0)
                    this.mPlayer.resume()
                    this.currentPosition = 0
                } catch (e) { }
                this.startProgressPolling()
                this.scheduleBarHide()
                this.pushBarState()
                return
            }
            this.paused = !this.paused
            if (this.paused) {
                try { this.mPlayer.pause() } catch (e) { }
                this.cancelBarHide()
            } else {
                try { this.mPlayer.resume() } catch (e) { }
                this.scheduleBarHide()
            }
            this.barVisible = true
            this.pushBarState()
            console.warn('[player] ' + (this.paused ? 'paused' : 'resumed'))
        },
        // ---- 进度：轮询 / 点击 / 拖动 / 步进 ----
        startProgressPolling() {
            if (this.progressTimer || !this.mPlayer) return
            var self = this
            var tick = function () {
                self.pollProgress()
                self.progressTimer = setTimeout(tick, PROGRESS_POLL_MS)
            }
            tick()
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
                var changed = false
                if (typeof pos === 'number' && pos >= 0) {
                    this.currentPosition = Math.round(pos)
                    changed = true
                }
                if (typeof dur === 'number' && dur > 0) {
                    this.duration = Math.round(dur)
                    changed = true
                }
                if (changed) this.pushBarState()
            } catch (e) {
                // 查询失败静默：pipeline 未就绪等场景，下轮再试
            }
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
        progressPct() {
            if (!this.duration) return 0
            var pct = this.currentPosition * 100 / this.duration
            if (pct < 0) pct = 0
            if (pct > 100) pct = 100
            return pct
        },
        seekFromTouchX(x, trackLeft, trackWidth, kind) {
            var pct = (x - trackLeft) / trackWidth
            if (pct < 0) pct = 0
            if (pct > 1) pct = 1
            var target = Math.round(this.duration * pct)
            console.warn('[player] seek coord ' + kind + ' x=' + x + ' pct=' + Math.round(pct * 100) + '% target=' + target + 'ms')
            this.seekTo(target, kind)
        },
        seekTo(val, reason) {
            var target = Math.round(val)
            if (target < 0) target = 0
            if (this.duration && target > this.duration) target = this.duration
            try {
                this.mPlayer.seek(target)
                this.currentPosition = target
                this.pushBarState()
            } catch (err) {
                console.warn('[player] seek error: ' + err.message)
            }
        },
        onSeekForward() {
            this.seekBy(SEEK_STEP_MS)
        },
        onSeekBack() {
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
                this.currentPosition = target
                this.scheduleBarHide()
                this.pushBarState()
                console.warn('[player] seekBy ' + deltaMs + 'ms -> ' + target + 'ms')
            } catch (err) {
                console.warn('[player] seek error: ' + err.message)
            }
        },
        closePlayer() {
            console.warn('[player] closePlayer')
            if (this.mPlayer) {
                try { this.mPlayer.close() } catch (e) { }
                this.mPlayer = null
            }
            try {
                this.$page.finish()
            } catch (e) {
                console.warn('[player] finish error: ' + (e ? e.message : e))
            }
        }
    }
}
</script>



