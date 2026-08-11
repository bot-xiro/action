<template>
    <div class="page">
        <!-- 顶部栏：返回 + 标题 -->
        <div class="topbar">
            <text class="back-btn" @click="goBack">‹ 返回</text>
            <text class="topbar-title">设置</text>
            <text class="topbar-right"></text>
        </div>

        <!-- 内容滚动区：视口 960×266 有限，内容超出可上下翻动 -->
        <scroller scroll-direction="vertical" :show-scrollbar="false" :over-scroll="60">
            <!-- 播放器设置 -->
            <div class="section">
                <text class="section-title">播放器</text>

                <!-- 选项1：自研 gstplayer -->
                <div class="option" :class="{ 'option-active': mode === 'gst' }" @click="onSelect('gst')">
                    <div class="option-left">
                        <text class="option-name">自研播放器</text>
                        <text class="option-desc">KMS 双平面，视频独立平面（gstplayer）</text>
                    </div>
                    <text v-if="mode === 'gst'" class="option-badge">当前</text>
                </div>

                <!-- 选项2：系统播放器 -->
                <div class="option" :class="{ 'option-active': mode === 'system' }" @click="onSelect('system')">
                    <div class="option-left">
                        <text class="option-name">系统播放器</text>
                        <text class="option-desc">调起系统播放器应用，自带控制栏悬浮</text>
                    </div>
                    <text v-if="mode === 'system'" class="option-badge">当前</text>
                </div>
            </div>

            <!-- 说明 -->
            <div class="tips">
                <text class="tips-text">提示：切换后进入播放页生效。</text>
                <text class="tips-text">系统播放器 = 调起系统视频播放器应用播放（8001661999525016）：自带控制栏悬浮、无层级遮挡问题；播放期间本应用退到后台，返回后继续浏览。</text>
                <text class="tips-text">自研播放器 = gstplayer（KMS 双平面），视频独立平面渲染；控制栏唤出时视频画面让位。</text>
            </div>
        </scroller>
    </div>
</template>

<style scoped>
.page {
    flex: 1;
    background-color: #f5f6f7;
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

.back-btn {
    font-size: 20px;
    color: #ffffff;
    padding-left: 10px;
    padding-right: 10px;
    padding-top: 4px;
    padding-bottom: 4px;
    border-radius: 4px;
    background-color: rgba(255, 255, 255, 0.25);
}

.topbar-title {
    font-size: 26px;
    color: #ffffff;
    font-weight: bold;
}

.topbar-right {
    width: 80px;
}

.section {
    margin-top: 16px;
    margin-left: 16px;
    margin-right: 16px;
}

.section-title {
    font-size: 22px;
    color: #888888;
    margin-bottom: 10px;
}

.option {
    flex-direction: row;
    align-items: center;
    justify-content: space-between;
    background-color: #ffffff;
    border-radius: 8px;
    border-width: 2px;
    border-color: transparent;
    padding-left: 16px;
    padding-right: 16px;
    padding-top: 14px;
    padding-bottom: 14px;
    margin-bottom: 12px;
}

.option-active {
    border-color: #fb7299;
}

.option-left {
    flex: 1;
    flex-direction: column;
}

.option-name {
    font-size: 24px;
    color: #222222;
}

.option-desc {
    margin-top: 4px;
    font-size: 18px;
    color: #999999;
}

.option-badge {
    font-size: 18px;
    color: #ffffff;
    background-color: #fb7299;
    border-radius: 10px;
    padding-left: 14px;
    padding-right: 14px;
    padding-top: 3px;
    padding-bottom: 3px;
}

.tips {
    margin-top: 8px;
    margin-left: 20px;
    margin-right: 20px;
    flex-direction: column;
}

.tips-text {
    font-size: 18px;
    color: #aaaaaa;
    margin-top: 4px;
}
</style>

<script>
import settings from '../../utils/settings.js'

export default {
    name: 'settings',
    data() {
        return {
            mode: settings.DEFAULT_MODE
        }
    },
    mounted() {
        var self = this
        console.warn('[settings] mounted')
        settings.getMode().then(function (m) {
            self.mode = m
            console.warn('[settings] loaded mode=' + m)
        })
    },
    methods: {
        goBack() {
            console.log('[settings] goBack')
            try {
                this.$page.finish()
            } catch (e) {
                console.warn('[settings] finish error: ' + (e ? e.message : e))
            }
        },
        onSelect(mode) {
            var self = this
            console.warn('[settings] select mode=' + mode + ' (current=' + this.mode + ')')
            if (this.mode === mode) return
            settings.setMode(mode).then(function (saved) {
                self.mode = saved
                console.warn('[settings] saved mode=' + saved)
                // 轻提示（jsapi modal.toast，框架内置）
                try {
                    var m = $falcon.jsapi && $falcon.jsapi.modal
                    if (m && typeof m.toast === 'function') {
                        m.toast({
                            message: '已切换为' + (saved === 'system' ? '系统播放器' : '自研播放器'),
                            duration: 2000
                        })
                    }
                } catch (e) {
                    console.warn('[settings] toast error: ' + (e && e.message))
                }
            })
        }
    }
}
</script>
