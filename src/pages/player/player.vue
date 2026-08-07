<template>
    <div class="page">
        <!-- 加载/错误覆盖层 -->
        <div v-if="!ready" class="overlay">
            <text class="hint">{{ error || '加载播放地址...' }}</text>
            <text v-if="loading" class="spinner">●</text>
        </div>

        <!-- 视频区（hole 挖洞显示 waylandsink 输出，全屏 960×266） -->
        <div v-else class="player-area">
            <hole ref="videoHole" class="video-hole"></hole>
            <!-- 控制栏（悬浮在视频底部） -->
            <div class="controls">
                <text class="ctrl-title" :lines="1">{{ title }}</text>
                <text class="ctrl-btn" @click="togglePlay">{{ paused ? '▶ 播放' : '⏸ 暂停' }}</text>
                <text class="ctrl-btn" @click="closePlayer">✕ 关闭</text>
            </div>
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
    right: 0;
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

.player-area {
    flex: 1;
    width: 960px;
    height: 266px;
    position: relative;
    background-color: #000000;
}

.video-hole {
    width: 960px;
    height: 266px;
}

.controls {
    position: absolute;
    left: 0;
    right: 0;
    bottom: 0;
    width: 960px;
    height: 66px;
    flex-direction: row;
    align-items: center;
    justify-content: space-between;
    padding-left: 16px;
    padding-right: 16px;
    background-color: rgba(0, 0, 0, 0.5);
}

.ctrl-title {
    flex: 1;
    font-size: 20px;
    color: #ffffff;
    lines: 1;
}

.ctrl-btn {
    margin-left: 20px;
    font-size: 20px;
    color: #cccccc;
    padding-top: 8px;
    padding-right: 16px;
    padding-bottom: 8px;
    padding-left: 16px;
    background-color: #2a2a2a;
    border-radius: 4px;
    border-width: 1px;
    border-color: #444444;
}
</style>

<script>
import api from '../../utils/api.js'
import { gstPlayer } from 'gstplayer'

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
            mPlayer: null,
            stateCb: null
        }
    },
    mounted() {
        var opt = this.$page.loadOptions || {}
        this.bvid = opt.bvid || ''
        this.cid = opt.cid || ''
        this.title = opt.title || '视频播放'
        console.warn('[player] mounted bvid=' + this.bvid + ' cid=' + this.cid)

        // 注册状态事件（stateChanged.on(cb) / .off(cb)）
        this.stateCb = (state) => {
            console.warn('[player] stateChanged: ' + state)
            if (state === 'playing') {
                this.paused = false
            } else if (state === 'paused') {
                this.paused = true
            } else if (state === 'ended') {
                this.error = '播放结束'
                this.ready = false
                this.loading = false
            } else if (state && state.indexOf('error:') === 0) {
                this.error = '播放错误: ' + state.substring(6)
                this.ready = false
                this.loading = false
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
            // gstplayer 导出单例实例，直接调用方法；open/start 为同步方法
            this.mPlayer = gstPlayer
            try {
                var ok = this.mPlayer.open({
                    uri: url,
                    audio: true,
                    pos_x: 0,
                    pos_y: 0,
                    pos_w: 960,
                    pos_h: 266,
                    // fill 不传 = fit：等比缩放完整显示在屏幕内（左右黑边），
                    // crop 会使 waylandsink 窗口随视频放大超出屏幕，禁用
                    // alpha=0：窗口透明，黑底消失，仅视频画面可见（实验）
                    alpha: 0,
                    loop: 0
                })
                console.warn('[player] open ret: ' + ok)
                this.mPlayer.start()
                this.ready = true
                this.loading = false
                console.warn('[player] play OK')
            } catch (e) {
                var msg = (e && e.message) ? e.message : JSON.stringify(e)
                this.error = '播放失败: ' + msg
                this.loading = false
                console.warn('[player] play error: ' + msg)
            }
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
