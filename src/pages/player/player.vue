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
            <!-- 独立控制栏框架：固定在屏幕顶部（标题）/底部（控制），常驻显示，不跟随视频 -->
            <player-controls :title="title" :paused="paused" @toggle-play="togglePlay" @close="closePlayer"></player-controls>
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
    /* 绝对定位铺满 player-area：挖洞区域固定 960×266 全屏，
       不受控制栏或 flex 布局影响，waylandsink 渲染窗口铺满整个屏幕
       与 waylandsink render-rectangle <0,100,960,266> 对齐（画面下移100px） */
    position: absolute;
    top: 100px;
    left: 0;
    width: 960px;
    height: 266px;
}
</style>

<script>
import api from '../../utils/api.js'
import { gstPlayer } from 'gstplayer'
import PlayerControls from './player-controls.vue'
import storage from '../../utils/storage.js'
import bridge from '../../utils/bridge.js'

export default {
    name: 'player',
    components: {
        PlayerControls
    },
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
            stateCb: null,
            useSystemPlayer: false
        }
    },
    mounted() {
        var opt = this.$page.loadOptions || {}
        this.bvid = opt.bvid || ''
        this.cid = opt.cid || ''
        this.title = opt.title || '视频播放'
        console.warn('[player] mounted bvid=' + this.bvid + ' cid=' + this.cid)

        // 读取设置：是否使用系统播放器
        this.useSystemPlayer = storage.getSetting('useSystemPlayer') || false
        console.warn('[player] useSystemPlayer:', this.useSystemPlayer)

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
                    
                    // 根据设置决定播放方式
                    if (this.useSystemPlayer) {
                        this.trySystemPlayer(this.playUrl)
                    } else {
                        this.tryPlay(this.playUrl)
                    }
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
        // 尝试使用系统播放器
        async trySystemPlayer(url) {
            console.warn('[player] trying system player for:', url)
            try {
                // 系统播放器通常需要本地文件路径或直链
                // B 站视频流通常是带防盗链的 m3u8/flv/mp4，可能无法直接在系统播放器打开
                // 这里尝试调用 bridge.shell.openSystemVideo
                const success = await bridge.openSystemVideo(url)
                if (success) {
                    console.warn('[player] system player launched successfully')
                    this.ready = true
                    this.loading = false
                    // 系统播放器启动后关闭当前页面（可选）
                    setTimeout(() => {
                        this.closePlayer()
                    }, 1000)
                } else {
                    console.warn('[player] system player not available, falling back to in-app player')
                    this.error = '系统播放器不可用，自动切换到应用内播放器'
                    this.tryPlay(url)
                }
            } catch (e) {
                console.warn('[player] system player error, fallback:', e.message)
                this.error = '系统播放器调用失败，自动切换到应用内播放器'
                this.tryPlay(url)
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
                    // 视频画面整体下移 100px（用户：下方黑边接近一半，允许底部溢出屏幕）
                    pos_y: 100,
                    pos_w: 960,
                    pos_h: 266,
                    // fill 不传 = fit：等比缩放完整显示在屏幕内（左右黑边），
                    // crop 会使 waylandsink 窗口随视频放大超出屏幕，禁用
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
