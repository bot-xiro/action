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
            <!-- holE 挖洞：绝对定位铺满页面视口 960×266，与 gstPlayer.open 的
                 pos 参数一致（960×266 全屏视频区）。【2026-08-11 用户指令】层级基线：
                 视频 plane 76 zpos=0 置底 + UI 平面抬 zpos=1（preheat）→ 控制栏盖住
                 视频；本 hole 为后续挖洞预留（洞区域透明后视频从洞中透出）。 -->
            <hole class="video-hole"></hole>

            <!-- 顶部控制栏（返回 + 标题）：absolute 悬浮，z-index 999 保证在 DOM 最顶层。
                 默认透明不可见（opacity 0 + pointer-events none），点击屏幕唤出。
                 【2026-08-11】恢复悬浮布局（用户要求）：视频全屏 + 控制栏悬浮在视频上。
                 gstplayer 双平面下控制栏会被视频 plane 遮挡，需框架原生视频
                 （CVPlayer，视频在 UI 层内合成）才能让悬浮生效。 -->
            <div class="ctrl-top" :class="{ 'ctrl-visible': controlsVisible }">
                <text class="back-btn" @click="closePlayer">‹ 返回</text>
                <text class="bar-title" :lines="1">{{ title }}</text>
            </div>

            <!-- 底部控制栏（按钮行 + 进度条）：绝对定位悬浮，半透明黑底，
                 与顶部栏共用同一显隐状态。
                 【布局】两行：上=按钮行（左回退 / 中播放暂停 / 右快进），下=进度条+时间。 -->
            <div class="ctrl-bottom" :class="{ 'ctrl-visible': controlsVisible }">
                <div class="progress-row">
                    <!-- 进度条：自研实现（原生 seekbar 组件被框架忽略、回调零触发，见 DEV_LOG 2026-08-10，
                         官方应用亦无使用 seekbar 的先例，弃用）。track 全宽 928px 几何确定，
                         自身绑定 touch 系事件——探测版已证实 track(实背景)可命中且
                         changedTouches[0] 携带 pageX/pageY/screenX/screenY 坐标。
                         点击 = touchstart 立即按绝对坐标 seek；拖动 = touchmove 按相对起始
                         startX 的位移增量 seek（不依赖轨道绝对左缘，拖动跟随精确）。
                         【命中区/视觉分离】外层 progress-track 高 36px 黑底(0.3 alpha)——
                         命中区 2.5×（框架近透明不可命中，故命中容器必须有背景色；黑底与
                         ctrl-bottom 同色系视觉近乎不可见）；内层 progress-track-line 高 14px
                         白灰细条承载视觉（含 fill/thumb），显示与 14px 时代完全一致。 -->
                    <div class="progress-track" @touchstart="onTrackStart($event)"
                        @touchmove="onTrackMove($event)" @touchend="onTrackEnd($event)">
                        <div class="progress-track-line">
                            <div class="progress-fill" :style="{ width: progressPct() + '%' }"></div>
                            <div class="progress-thumb" :style="{ left: progressThumbLeft() + '%' }"></div>
                        </div>
                    </div>
                    <text class="time-text">{{ fmtTime(currentPosition) }} / {{ fmtTime(duration) }}</text>
                </div>
                <div class="btn-row">
                    <text class="seek-btn" @click="onSeekBack">回退</text>
                    <text class="mini-play-btn" @click="onTogglePlay">{{ paused ? '▶ 播放' : '⏸ 暂停' }}</text>
                    <text class="seek-btn" @click="onSeekForward">快进</text>
                </div>
            </div>
        </div>
    </div>
</template>

<style scoped>
/* 外层容器：铺满 960×480 全屏。背景纯黑——透明背景已被证实无效（UI 层恒为
   XR24 不透明，JQuick 不支持 alpha），黑底保证视频外区域为黑边。 */
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

/* 挖洞区域：覆盖整个视口（页面 x=0~960，逻辑高 266）——对应 gstPlayer.open
   ({ pos_x: 0, pos_y: 107, pos_w: 960, pos_h: 266 })：视频物理全屏竖条。
   【2026-08-11】视频 zpos=0 置底 + UI zpos=1 盖视频（控制栏可操作），
   挖洞透出视频后再依赖本区域透明显示。 */
.video-hole {
    position: absolute;
    top: 0;
    left: 0;
    width: 960px;
    height: 266px;
    z-index: 1;
}

/* ---- 悬浮控制栏通用样式：默认隐藏，铺满页面全宽（悬浮于视频之上，
   系统播放器 CVPlayer 方案下视频在 UI 层内合成，控制栏自然浮在视频上） ---- */
.ctrl-top,
.ctrl-bottom {
    position: absolute;
    left: 0;
    width: 960px;
    z-index: 999;               /* DOM 最顶层，保证盖在 hole 之上 */
    padding-left: 16px;
    padding-right: 16px;
    opacity: 0;                 /* 默认透明不可见，全屏沉浸播放 */
    pointer-events: none;       /* 不可点击，点击事件穿透到 stage 唤出控制栏 */
    transition: opacity 0.3s;   /* 淡入淡出（框架若不支持 transition 则退化为瞬间切换） */
}

/* 顶部栏保留半透明黑底：标题/返回键下方衬底保证视频上可读 */
.ctrl-top {
    top: 0;
    height: 60px;
    flex-direction: row;
    align-items: center;
    background-color: rgba(0, 0, 0, 0.5);
}

/* 底部控制栏：半透明黑底（正常手机端播放器样式，用户要求与之一致），
   内容顺序：进度条在上、按钮行在下 */
.ctrl-bottom {
    bottom: 0;
    height: 128px;
    flex-direction: column;
    justify-content: center;
    background-color: rgba(0, 0, 0, 0.5);
}

/* 按钮行：回退 / 播放暂停 / 快进 均匀分布（进度条下方） */
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

/* 进度条行：纵向两行——上=轨道（全宽 928px 几何固定，便于坐标换算），下=时间文本 */
.progress-row {
    flex-direction: column;
    align-items: flex-start;
    width: 928px;               /* 960 - 左右 padding 16*2 */
}

/* 轨道命中容器：高 36px——识别区 2.5× 扩大。背景 rgba(0,0,0,0.3)：与 ctrl-bottom
   黑底同色系视觉近不可见，但满足框架"实背景才可命中"约束。 */
.progress-track {
    position: relative;
    width: 928px;
    height: 36px;
    justify-content: center;    /* 内部视觉条垂直居中 */
    background-color: rgba(0, 0, 0, 0.3);
}

/* 视觉轨道：14px 白灰细条（居中于 36px 命中容器），承载 fill/thumb 视觉。 */
.progress-track-line {
    position: relative;
    width: 928px;
    height: 14px;
    border-radius: 7px;
    background-color: rgba(255, 255, 255, 0.3);
}

/* 已播放填充条：绝对定位，宽度由 JS 轮询进度动态设置。
   pointer-events:none——不拦截触摸，保证触摸命中统一落到 track 上。
   相对 14px 视觉条定位：top/bottom 各 5px 得 4px 视觉条。 */
.progress-fill {
    position: absolute;
    left: 0;
    top: 5px;                   /* 14 高轨道中 4px 视觉条：top/bottom 各 5 */
    bottom: 5px;
    border-radius: 2px;
    background-color: #fb7299;
    pointer-events: none;
}

/* 进度圆点：中心对齐进度位置（left = pct% 时实际圆点左缘，用负 margin 回移半宽）。
   pointer-events:none——同 fill，不拦截触摸。相对 14px 视觉条垂直居中。 */
.progress-thumb {
    position: absolute;
    top: -1px;                  /* (14-16)/2 垂直居中，微超轨上缘 */
    width: 16px;
    height: 16px;
    margin-left: -8px;          /* 回移半宽：left 对准圆点中心 */
    border-radius: 8px;
    background-color: #ffffff;
    pointer-events: none;
}

.time-text {
    margin-top: 4px;
    font-size: 16px;
    color: #ffffff;
    text-align: right;
    width: 100%;
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
import settings from '../../utils/settings.js'
import { gstPlayer } from 'gstplayer'

const CONTROLS_HIDE_MS = 5000   // 控制栏无操作自动隐藏延时（5 秒）
const PROGRESS_POLL_MS = 1000   // 进度轮询间隔（毫秒）：500→1000 减半 JS-C++ 跨调用，RK3562 上降低 UI 线程开销
const SEEK_STEP_MS = 10000       // 快进/回退单步时长（10 秒，毫秒）
const PINCH_MIN_SCALE = 0.5      // 双指缩放下限（相对初始矩形）
const PINCH_MAX_SCALE = 2.0      // 双指缩放上限（放大 2 倍后宽度超出屏幕，可平移查看细节）
const PINCH_THROTTLE_MS = 40     // touchmove→setRect 调用节流间隔（JS→C++ 跨调用，减少 UI 线程开销）
const TRACK_SCREEN_LEFT = 16     // 轨道左缘屏幕/页面坐标（ctrl-bottom padding-left，进度条 x=16 起）
const TRACK_WIDTH = 928          // 轨道全宽（960 - 左右 padding 16*2，与 .progress-track 保持一致）
const TRACK_MOVE_THROTTLE_MS = 60 // 拖动 seek 节流：touchmove 高频触发时限制 JS→C++ 跨调用
const LOGIC_TOP = 107            // 页面左上角在逻辑屏中的 y（open pos_y=107：页面 y → 逻辑 y = +107）

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
            playerMode: settings.DEFAULT_MODE,  // 播放器模式：gst=自研 / system=系统播放器
            // 双指缩放：当前渲染矩形（逻辑坐标，与 open pos_* 同坐标系，初始即 open 参数）
            // 【2026-08-11】恢复全屏：视频区域 960×266（用户要求视频不缩短/不放大）
            videoRect: { x: 0, y: 107, w: 960, h: 266 },
            pinch: null,               // 活跃捏合快照 {dist, x, y, w, h}；null=未捏合
            pinchTapGuard: false,      // 捏合结束后的下一个 click 吞噬标志
            trackDrag: null,           // 进度条拖动快照 {startX, startPct}；null=未在拖动
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
                // 【2026-08-11 动态层级】播放中视频置顶（zpos=3）全屏可见，
                // 不再自动显示控制栏（会触发置底遮挡视频）；用户点击唤出。
                this.setVideoTopmost(true)
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

        // 读取播放器模式（gst=自研 / system=系统播放器），再加载播放地址
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
            // 开发自测/直连：bvid 参数直接放完整播放 URL（cid 留空）→ 直连播放
            // （测试入口见 index.vue"测试"按钮：navTo player 传 bvid=url）
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
            // 【播放器切换 2026-08-11】按设置选择播放器：
            //   'gst'    = 自研 gstplayer（KMS 双平面，视频独立 plane）
            //   'system' = 系统播放器（appid 8001661999525016）：$falcon.startApp
            //              打开系统播放器应用播放（DEV_LOG 2026-08-10 已验证可打开；
            //              系统播放器单平面渲染 + 自带控制栏，无层级遮挡问题）。
            // CVPlayer/videoproxy 应用层不可用（2026-08-11 probe：getCVPlayerManager
            // undefined、require videoproxy/cvplayer/bridge/fido 均 unknown module），
            // 因此系统播放器只能通过 startApp 拉起独立应用（bilibili 退后台，
            // 播放页显示提示文案，返回后继续浏览）。
            if (this.playerMode === 'system') {
                console.warn('[player] system player: startApp 8001661999525016 url=' + url)
                try {
                    var appRet = $falcon.startApp('8001661999525016', {
                        url: url,
                        title: this.title || 'bilibili'
                    })
                    console.warn('[player] startApp ret=' + JSON.stringify(appRet))
                    // 调起成功：bilibili 退后台，系统播放器接管播放（自带控制栏悬浮）
                    this.error = '已调起系统播放器播放，返回后继续浏览'
                    this.loading = false
                    this.ready = false
                    return
                } catch (e) {
                    console.warn('[player] startApp error: ' + (e && e.message))
                    // 调起失败回退 gstplayer，保证可播
                }
            }

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
        // 【2026-08-11 动态层级】控制栏与视频层级联动：
        //   唤出控制栏 → 视频置底(zpos=0, UI zpos=1 盖视频) → 控制栏可操作
        //   隐藏控制栏 → 视频置顶(zpos=3) → 视频全屏可见
        // 背景：UI 层 XR24 不透明、JQuick 不支持 hole，双平面下两者互斥，
        // 只能动态切换（DRM plane zpos 属性即时生效，见 GstPlayer.cpp setVideoZpos）
        setVideoTopmost(top) {
            if (!this.mPlayer) return
            try {
                this.mPlayer.setVideoZpos(top ? 3 : 0)
                console.warn('[player] video zpos=' + (top ? 3 : 0) + (top ? ' (topmost)' : ' (under UI)'))
            } catch (e) {
                console.warn('[player] setVideoZpos error: ' + (e && e.message))
            }
        },
        showControls() {
            this.controlsVisible = true
            this.setVideoTopmost(false)   // 控制栏唤出：视频置底让位
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
            this.setVideoTopmost(true)   // 控制栏隐藏：视频恢复置顶全屏可见
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
            // 当前播放位置 → 进度百分比（0~100，驱动 fill 宽度 / thumb 左缘）
            if (!this.duration) return 0
            var pct = this.currentPosition * 100 / this.duration
            if (pct < 0) pct = 0
            if (pct > 100) pct = 100
            return pct
        },
        progressThumbLeft() {
            // 圆点中心对齐进度位置：left = pct% - 半宽(8px)/轨道宽(928px)*100
            // 直接换算百分比，避免负 margin 兼容性问题
            var pct = this.progressPct()
            var left = pct - (8 / TRACK_WIDTH) * 100
            if (left < 0) left = 0
            if (left > 100) left = 100
            return left
        },
        // ---- 进度条：触摸坐标换算点击/拖动 seek ----
        // 思路（探测版 3ba1c87 证实）：track 为实背景元素可命中 touch 事件，
        // changedTouches[0] 携带 pageX/pageY/screenX/screenY 坐标。
        // 点击 = touchstart 立即按绝对坐标换算 seek；拖动 = touchmove 按相对起点位移
        // 换算 seek（不依赖轨道绝对左缘，跟随精确）。原生 seekbar 组件被框架忽略
        // （FALCON_IGNORE_ELEMENTS=['modal','seekbar']，官方应用无使用先例）弃用。
        // 坐标统一取 pageX（页面坐标：轨道左缘=16，全宽 928 的几何直接可用；
        // 探测日志 pageX≈screenX-16，二者恒定差 16 即 padding，pageX 更直接）。
        touchX(e) {
            var ct = e && e.changedTouches && e.changedTouches[0]
            if (ct && typeof ct.pageX === 'number') return ct.pageX
            var t = e && e.touches && e.touches[0]
            if (t && typeof t.pageX === 'number') return t.pageX
            return null
        },
        onTrackStart(e) {
            if (!this.controlsVisible || !this.mPlayer || !this.duration) return
            var x = this.touchX(e)
            if (x === null) return   // 无坐标：记录日志但不设锚点，避免脏时间戳
            var now = Date.now()
            // 防御：框架对一次触摸会重派发 touchstart（探测版实测 70ms 内双 touchstart）。
            // 重派发时跳过"点击 seek"，仅更新拖动锚点（手指已移动则跟手不跳变）。
            var reDispatch = (typeof this._lastTrackStartT === 'number') && (now - this._lastTrackStartT < 120)
            console.warn('[player] track start x=' + x + ' keys=' + Object.keys(e || {}).join(',')
                + (reDispatch ? ' reDispatch' : ''))
            this._lastTrackStartT = now
            // 记录拖动起点：起点坐标 + 起点进度百分比
            this.trackDrag = {
                startX: x,
                startPct: this.progressPct() / 100
            }
            if (!reDispatch) {
                // 首次按下：点击即 seek 到该位置（绝对坐标）
                this.seekFromTouchX(x, TRACK_SCREEN_LEFT, TRACK_WIDTH, 'tap')
            }
        },
        onTrackMove(e) {
            if (!this.trackDrag || !this.mPlayer || !this.duration) return
            var x = this.touchX(e)
            if (x === null) return
            // 拖动 seek：起点百分比 + 位移比例（相对起点换算，避免依赖轨道绝对左缘）
            var dX = x - this.trackDrag.startX
            var pct = this.trackDrag.startPct + dX / TRACK_WIDTH
            if (pct < 0) pct = 0
            if (pct > 1) pct = 1
            if (typeof this._lastTrackMoveX === 'number' && Math.abs(x - this._lastTrackMoveX) < 4) return
            this._lastTrackMoveX = x
            var target = Math.round(this.duration * pct)
            console.warn('[player] track drag x=' + x + ' dX=' + dX + ' pct=' + Math.round(pct * 100) + '% target=' + target + 'ms')
            this.seekTo(target, 'drag')
        },
        onTrackEnd(e) {
            if (!this.trackDrag && typeof this._lastTrackStartT !== 'number') return
            this.trackDrag = null
            this._lastTrackMoveX = null
            console.warn('[player] track end')
            // 拖动/点击结束派生 click 冒泡到 stage 会误隐藏控制栏 → 短路。
            // 定时器兜底：若框架未派发派生 click（guard 无人消费），300ms 后自动复位防卡死
            // （与 pinchTapGuard 同款机制，避免脏标志吞掉下一次正常点击）
            this.btnTapJustOccurred = true
            var self = this
            setTimeout(function () {
                if (self.btnTapJustOccurred) {
                    self.btnTapJustOccurred = false
                    console.log('[player] track btnTap guard auto-reset')
                }
            }, 300)
        },
        // 绝对坐标 → 进度百分比 → seek（tap）
        seekFromTouchX(x, trackLeft, trackWidth, kind) {
            var pct = (x - trackLeft) / trackWidth
            if (pct < 0) pct = 0
            if (pct > 1) pct = 1
            var target = Math.round(this.duration * pct)
            console.warn('[player] seek coord ' + kind + ' x=' + x + ' left=' + trackLeft
                + ' w=' + trackWidth + ' pct=' + Math.round(pct * 100) + '% target=' + target + 'ms')
            this.seekTo(target, kind)
        },
        seekTo(val, reason) {
            var target = Math.round(val)
            if (target < 0) target = 0
            if (this.duration && target > this.duration) target = this.duration
            try {
                this.mPlayer.seek(target)
                this.currentPosition = target   // 立即更新进度条，不等下轮轮询
            } catch (err) {
                console.warn('[player] seek error: ' + err.message)
            }
        },
        // ---- 双指缩放（捏合） ----
        // 手势流程：touchstart 记录起始矩形与双指间距 → touchmove 按间距比例
        // 缩放（锚定两指中心）+ 平移 → 节流调用原生 setRect 动态更新渲染区域 →
        // touchend 复位。
        // 【根因·设备日志 2026-08-10】框架 touch 事件对象 keys 仅 {changedTouches, type}
        // （探测版 3ba1c87 证实），不存在 e.touches 聚合数组 → 旧实现读 e.touches 得
        // undefined，!ts 静默 return，捏合全链路日志 0 条（连 "no touch coords" 警告
        // 都没有），双指缩放从未真正启动。
        // 【修复】改用手动触点集合 this._tp（key=identifier，value=坐标）：
        // touchstart/touchmove/touchend 时从 changedTouches 逐点增/改/删，
        // 集合 ≥2 即进入捏合。兼容两种派发模型：
        //   ① 一次事件携带多指（changedTouches 含 2 点）→ 直接启动；
        //   ② 多次事件各携带一指（逐指 touchstart）→ 集合累积到 2 后启动。
        // 【验证日志】双指按下时打印 "touch start n=" 集合大小——若框架底层仅上报单指
        // （n 恒为 1，第二指事件被吞），日志即为铁证（区别于此前静默失败）。
        // 【坐标字段】探测版(3ba1c87)证实 touch 点携带 pageX/pageY（changedTouches[0]
        // keys=pageX,pageY,screenX,screenY）——此前读 point.x/.x 与框架实际字段不匹配，
        // 坐标永远为 undefined → pinch=null 手势无法启动（用户反馈双指缩放不可用）。
        // 【坐标系】pageX/pageY 是页面坐标（960×266 视口）；open/setRect 用逻辑坐标
        // （页面左上角 = 逻辑 y=107）→ 手势中心 y 需 +107 换算；x 页面=逻辑（同为 960 宽）。
        // 【日志】pinch 关键链 warn 级（设备丢弃 console.log），单指 touch 用 log 避免刷屏。
        pinchPoint(t) {
            // 取 touch 点坐标：优先 pageX/pageY（框架实际字段），兼容 point.x / x 形态
            if (!t) return null
            if (typeof t.pageX === 'number' && typeof t.pageY === 'number') {
                return { x: t.pageX, y: t.pageY }
            }
            var p = t.point
            if (p && typeof p.x === 'number' && typeof p.y === 'number') return p
            if (typeof t.x === 'number' && typeof t.y === 'number') return { x: t.x, y: t.y }
            return null
        },
        // 从事件 changedTouches 更新触点集合；返回集合大小
        touchMapApply(e, mode) {
            var ct = e && e.changedTouches
            if (!ct || !ct.length) return -1
            var tp = this._tp || (this._tp = {})
            for (var i = 0; i < ct.length; i++) {
                var p = this.pinchPoint(ct[i])
                if (!p) continue
                if (mode === 'end') {
                    delete tp[ct[i].identifier]
                } else {
                    tp[ct[i].identifier] = p
                }
            }
            return Object.keys(tp).length
        },
        onPinchStart(e) {
            var n = this.touchMapApply(e, 'start')
            if (n < 0) {
                console.warn('[player] touch start: no changedTouches keys=' + Object.keys(e || {}).join(','))
                return
            }
            if (n === 2) {
                console.warn('[player] touch start n=' + n + ' PINCH READY')
            } else {
                console.log('[player] touch start n=' + n)
            }
            if (n < 2 || this.pinch) return   // 单指或已有捏合：不重复启动
            var tp = this._tp
            var ids = Object.keys(tp)
            var p0 = tp[ids[0]]
            var p1 = tp[ids[1]]
            var dx = p1.x - p0.x
            var dy = p1.y - p0.y
            var dist = Math.sqrt(dx * dx + dy * dy)
            if (dist <= 0) {
                console.warn('[player] pinch start: zero dist, ids=' + ids.join(','))
                return
            }
            this.pinch = {
                dist: dist,
                x: this.videoRect.x,
                y: this.videoRect.y,
                w: this.videoRect.w,
                h: this.videoRect.h
            }
            console.warn('[player] pinch start dist=' + this.pinch.dist
                + ' rect=' + this.videoRect.x + ',' + this.videoRect.y + ','
                + this.videoRect.w + ',' + this.videoRect.h)
        },
        onPinchMove(e) {
            var n = this.touchMapApply(e, 'move')
            if (n < 0 || n < 2) return
            if (!this.pinch) {
                // 兜底：若 touchstart 阶段未凑齐两指（如第二指事件异常），move 时按当前
                // 两指间距补建基线，避免手势永远无法启动
                var tp0 = this._tp
                var ids0 = Object.keys(tp0)
                var q0 = tp0[ids0[0]]
                var q1 = tp0[ids0[1]]
                var qdx = q1.x - q0.x
                var qdy = q1.y - q0.y
                var qdist = Math.sqrt(qdx * qdx + qdy * qdy)
                if (qdist <= 0) return
                this.pinch = {
                    dist: qdist,
                    x: this.videoRect.x,
                    y: this.videoRect.y,
                    w: this.videoRect.w,
                    h: this.videoRect.h
                }
                console.warn('[player] pinch start (deferred on move) dist=' + qdist)
            }
            var tp = this._tp
            var ids = Object.keys(tp)
            var p0 = tp[ids[0]]
            var p1 = tp[ids[1]]
            if (!p0 || !p1) return
            var dx = p1.x - p0.x
            var dy = p1.y - p0.y
            var dist = Math.sqrt(dx * dx + dy * dy)
            var p = this.pinch
            if (p.dist <= 0) return
            var scale = dist / p.dist   // 相对起始间距的比例
            if (scale < PINCH_MIN_SCALE) scale = PINCH_MIN_SCALE
            if (scale > PINCH_MAX_SCALE) scale = PINCH_MAX_SCALE
            var cxp = (p0.x + p1.x) / 2   // 两指当前中心（页面 x = 逻辑 x，同为 960 宽）
            var cyp = (p0.y + p1.y) / 2 + LOGIC_TOP   // 页面 y → 逻辑 y：页面左上角在逻辑 y=107
            var w = Math.round(p.w * scale)
            var h = Math.round(p.h * scale)
            // 以两指中心为锚：新中心 = 手势中心；矩形左上角随之移动
            var x = Math.round(cxp - w / 2)
            var y = Math.round(cyp - h / 2)
            // 边界钳制（逻辑坐标 960×480）：始终保留 80×40 可见（放大超屏后可平移，不可完全移出）
            var minX = 80 - w
            var minY = 40 - h
            var maxX = 960 - 80
            var maxY = 480 - 40
            if (x < minX) x = minX
            if (x > maxX) x = maxX
            if (y < minY) y = minY
            if (y > maxY) y = maxY
            // 节流：避免每帧跨 JS→C++ 调用（40ms 一次已足够顺滑）
            var now = Date.now()
            if (now - (this._pinchLastT || 0) < PINCH_THROTTLE_MS) return
            this._pinchLastT = now
            console.warn('[player] pinch move scale=' + scale.toFixed(2) + ' c=' + cxp + ',' + cyp + ' -> ' + x + ',' + y + ',' + w + ',' + h)
            this.applyVideoRect(x, y, w, h)
        },
        onPinchEnd(e) {
            var n = this.touchMapApply(e, 'end')
            if (n >= 2) return   // 仍有至少两指：捏合继续
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
                    console.warn('[player] pinch guard auto-reset')
                }
            }, 500)
            console.warn('[player] pinch end n=' + n)
        },
        applyVideoRect(x, y, w, h) {
            if (!this.mPlayer) return
            try {
                this.mPlayer.setRect(x, y, w, h)
                this.videoRect = { x: x, y: y, w: w, h: h }
                console.warn('[player] setRect ' + x + ',' + y + ',' + w + ',' + h)
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