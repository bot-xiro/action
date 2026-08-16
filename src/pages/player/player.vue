<template>
    <div class="page">
        <!-- 加载/错误覆盖层：铺满视口；仅在视频 plane 激活前可见（打开失败/加载中） -->
        <div v-if="!ready && !systemModeActive" class="overlay">
            <text class="hint">{{ error || '加载播放地址...' }}</text>
            <text v-if="loading" class="spinner">●</text>
        </div>

        <!-- 全屏视频工作区（2026-08-14 重构）：视频 plane 覆盖整个 960×266 视口 -->
        <div v-if="!systemModeActive" v-show="ready" class="stage"
            @touchstart="onStageTouchStart($event)"
            @touchmove="onStageTouchMove($event)"
            @touchend="onStageTouchEnd($event)">
        </div>

        <!-- 物理挖洞层：纯黑底 + mask-image 形成真实 alpha=0 孔洞（视频区 960×266） -->
        <div v-if="!systemModeActive" v-show="ready" class="hole-punch"></div>

        <!-- 系统播放器模式：纯提示层（视频由系统 App 独立渲染） -->
        <div v-if="systemModeActive" class="overlay">
            <text class="hint">已调起系统播放器（{{ systemAppId }}）</text>
            <text class="hint-sub">播放完成后按返回键回到本应用</text>
        </div>
    </div>
</template>

<style scoped>
.page {
    flex: 1;
    background-color: #000000;
    flex-direction: column;
}

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

.hint-sub {
    margin-top: 12px;
    font-size: 18px;
    color: #aaaaaa;
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

.stage {
    position: absolute;
    top: 0;
    left: 0;
    width: 960px;
    height: 266px;
    background-color: transparent;
    overflow: hidden;
    z-index: 5;
}

.hole-punch {
    position: absolute;
    top: 0;
    left: 0;
    width: 960px;
    height: 266px;
    background-color: #000000;
    z-index: 20;
    pointer-events: none;
    mask-image: url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='960' height='266'><defs><mask id='m' maskUnits='userSpaceOnUse' fill-rule='evenodd'><rect width='960' height='266' fill='black'/> <rect x='0' y='0' width='960' height='266' fill='white'/></mask></defs><rect width='100%' height='100%' fill='black' mask='url(%23m)'/></svg>");
    -webkit-mask-image: url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='960' height='266'><defs><mask id='m' maskUnits='userSpaceOnUse' fill-rule='evenodd'><rect width='960' height='266' fill='black'/><rect x='0' y='0' width='960' height='266' fill='white'/></mask></defs><rect width='100%' height='100%' fill='black' mask='url(%23m)'/></svg>");
    mask-mode: alpha;
    -webkit-mask-mode: alpha;
}
</style>

<script>
import api from '../../utils/api.js'
import settings from '../../utils/settings.js'
import { gstPlayer } from 'gstplayer'

// 系统播放器 appid（设备自带视频播放应用，自带 GStreamer + 控制栏悬浮）
const SYSTEM_PLAYER_APP_ID = '8001661999525016'

const PROGRESS_POLL_MS = 1000
const SEEK_STEP_MS = 10000
const TRACK_MOVE_THROTTLE_MS = 60
const BAR_HIDE_MS = 6000
const LOGIC_TOP = 107

const BAR = {
    top: 190,
    trackY: 196, trackH: 22,
    trackL: 24, trackR: 936,
    btnY: 226, btnH: 36,
    sbkL: 330, sbkR: 430,
    playL: 448, playR: 512,
    sfwL: 530, sfwR: 630,
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
            barVisible: true,
            duration: 0,
            currentPosition: 0,
            progressTimer: null,
            hideTimer: null,
            mPlayer: null,
            stateCb: null,
            playerMode: settings.DEFAULT_MODE,
            trackDrag: null,
            _lastTrackStartT: null,
            _lastTrackMoveX: null,
            _barGuardT: null,
            systemModeActive: false,
            systemAppId: SYSTEM_PLAYER_APP_ID
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
                // 此时重建已完成（若有），安全显示 stage
                if (!this.ready) {
                    this.ready = true
                    console.warn('[player] ready=true on playing state')
                }
                this.startProgressPolling()
                this.scheduleBarHide()
                this.pushBarState()
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
                this.pushBarState()
                console.warn('[player] runtime error: ' + this.error)
            }
        }
        try {
            gstPlayer.stateChanged.on(this.stateCb)
        } catch (e) {
            console.warn('[player] stateChanged.on error: ' + (e && e.message ? e.message : String(e)))
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
        // 仅 gst 模式需要清理原生播放器
        if (this.playerMode === settings.MODE_GST) {
            if (this.stateCb) {
                try { gstPlayer.stateChanged.off(this.stateCb) } catch (e) { }
            }
            if (this.mPlayer) {
                try { this.mPlayer.close() } catch (e) { }
                this.mPlayer = null
            }
        }
    },
    methods: {
        async loadPlayUrl() {
            // 直连模式：bvid 参数直接放完整播放 URL（cid 留空）
            if (this.bvid && !this.cid) {
                console.warn('[player] direct url=' + this.bvid)
                this.playUrl = this.bvid
                this.tryPlay(this.playUrl)
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
                    if (data.timelength && data.timelength > 0) {
                        this.duration = Math.round(data.timelength)
                        console.warn('[player] duration from API: ' + this.duration + 'ms')
                    }
                    console.warn('[player] playUrl qn=' + (data.quality || '?') + ' codecid=' + (data.video_codecid || '?') + ' len=' + this.playUrl.length)
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
            if (this.playerMode === settings.MODE_SYSTEM) {
                // 系统播放器模式：通过 falcon 跳转到系统 App（8001661999525016）
                this.playWithSystemPlayer(url)
                return
            }

            // 默认：自研 gstplayer 路径
            this.systemModeActive = false
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
                this.mPlayer.start()
                // ready 在 stateCb('playing') 时设为 true，避免重建前的首帧闪烁
                this.loading = false
                console.warn('[player] start OK, waiting for playing state')
            } catch (e) {
                var msg = (e && e.message) ? e.message : JSON.stringify(e)
                this.error = '播放失败: ' + msg
                this.loading = false
                console.warn('[player] play error: ' + msg)
            }
        },
        playWithSystemPlayer(url) {
            console.warn('[player] system player mode, navTo app=' + SYSTEM_PLAYER_APP_ID)
            this.systemModeActive = true
            this.loading = false
            this.ready = true  // 跳过自研播放器 stage，显示"已调起"提示
            try {
                // 系统播放器注册页面为 index（app.js.bin: pages/index/index.js），
                // 非 player。通过 $falcon.navTo 跳转并传播放参数。
                var target = 'falcon://' + SYSTEM_PLAYER_APP_ID + '/index'
                console.warn('[player] navTo target=' + target + ' url=' + url.substring(0, 80))
                $falcon.navTo(target, {
                    url: url,
                    durl: url,
                    bvid: this.bvid || '',
                    cid: this.cid || '',
                    title: this.title || '',
                    duration: this.duration || 0
                })
            } catch (e) {
                console.warn('[player] navTo system player failed: ' + (e && e.message))
                this.error = '调起系统播放器失败'
                this.systemModeActive = false
                this.ready = false
            }
        },
        pushBarState() {
            // 系统播放器模式：无原生控制栏可推送
            if (this.playerMode === settings.MODE_SYSTEM) return
            if (!this.mPlayer) return
            try {
                var gstPlayer = this.mPlayer
                gstPlayer.setBarState({
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
            if (this.playerMode === settings.MODE_SYSTEM) return
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
            if (this.playerMode === settings.MODE_SYSTEM) return
            var p = this.touchPoint(e)
            if (!p) {
                console.warn('[player] stage touch: no coords keys=' + Object.keys(e || {}).join(','))
                return
            }
            var x = p.x, y = p.y
            if (y >= BAR.titleBackY && y <= BAR.titleBackY + BAR.titleBackH &&
                x >= BAR.titleBackL && x <= BAR.titleBackR) {
                console.warn('[player] title back tap -> close')
                this.closePlayer()
                return
            }
            if (y >= BAR.trackY - 4 && y <= BAR.trackY + BAR.trackH + 6) {
                var now = Date.now()
                var reDispatch = (typeof this._lastTrackStartT === 'number') && (now - this._lastTrackStartT < 120)
                this._lastTrackStartT = now
                this.trackDrag = { startX: x, startPct: this.progressPct() / 100 }
                if (!reDispatch) {
                    this.seekFromTouchX(x, BAR.trackL, BAR.trackR - BAR.trackL, 'tap')
                }
                this.scheduleBarHide()
                return
            }
            if (y >= BAR.top) {
                if (!this.barVisible) {
                    console.warn('[player] bar region tap while hidden -> show bar')
                    this.toggleBar()
                    return
                }
                this._barGuardT = Date.now()
                if (y >= BAR.btnY && y <= BAR.btnY + BAR.btnH) {
                    if (x >= BAR.sbkL && x <= BAR.sbkR) { this.onSeekBack(); return }
                    if (x >= BAR.playL && x <= BAR.playR) { this.onTogglePlay(); return }
                    if (x >= BAR.sfwL && x <= BAR.sfwR) { this.onSeekForward(); return }
                }
                this.scheduleBarHide()
                return
            }
            console.warn('[player] video tap x=' + x + ' y=' + y + ' barVisible=' + this.barVisible)
            this.toggleBar()
        },
        onStageTouchMove(e) {
            if (!this.trackDrag || !this.mPlayer || !this.duration) return
            if (this.playerMode === settings.MODE_SYSTEM) return
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
            this.scheduleBarHide()
        },
        onStageTouchEnd(e) {
            if (this.trackDrag) {
                this.trackDrag = null
                this._lastTrackMoveX = null
                console.warn('[player] track end')
            }
        },
        onTogglePlay() {
            this.togglePlay()
        },
        togglePlay() {
            if (!this.mPlayer) return
            if (this.ended) {
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
        startProgressPolling() {
            if (this.progressTimer || !this.mPlayer) return
            if (this.playerMode === settings.MODE_SYSTEM) return
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
            } catch (e) { }
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