<template>
    <div class="page">
        <!-- 顶部标题栏 -->
        <div class="topbar">
            <text class="topbar-title">设置</text>
            <text class="topbar-back" @click="goBack">返回</text>
        </div>

        <scroller class="content" scroll-direction="vertical" :show-scrollbar="false">
            <div class="section">
                <text class="section-title">播放器设置</text>

                <!-- 系统播放器开关 -->
                <div class="setting-item">
                    <div class="setting-info">
                        <text class="setting-label">使用系统播放器</text>
                        <text class="setting-desc">开启后，点击播放将调用系统默认视频播放器（需视频为本地文件或可直链访问）</text>
                    </div>
                    <div class="custom-switch" :class="{ 'custom-switch-on': useSystemPlayer }" @click="onSystemPlayerToggle">
                        <div class="switch-thumb" :class="{ 'switch-thumb-on': useSystemPlayer }"></div>
                    </div>
                </div>

                <!-- 说明 -->
                <div class="setting-note">
                    <text class="note-title">说明：</text>
                    <text class="note-text">• 系统播放器：调用设备自带播放器，支持更多格式，但无法内嵌控制（无弹幕、倍速、进度条同步）</text>
                    <text class="note-text">• 应用内播放器：基于 GStreamer 硬解，视频内嵌在页面，支持弹幕、倍速、进度控制</text>
                    <text class="note-text">• 当前视频源为 B 站在线流媒体，系统播放器可能无法直接打开（需本地文件或直链）</text>
                </div>
            </div>

            <div class="section">
                <text class="section-title">关于</text>
                <div class="setting-item info-item">
                    <text class="info-label">版本</text>
                    <text class="info-value">0.0.1</text>
                </div>
                <div class="setting-item info-item">
                    <text class="info-label">播放内核</text>
                    <text class="info-value">GStreamer + MPP 硬解 (RK3562)</text>
                </div>
                <div class="setting-item info-item">
                    <text class="info-label">数据来源</text>
                    <text class="info-value">B 站公开 API</text>
                </div>
            </div>

            <div class="section">
                <text class="section-title">危险区</text>
                <div class="setting-item danger-item" @click="resetAllSettings">
                    <text class="danger-label">重置所有设置</text>
                    <text class="danger-desc">恢复默认配置（使用应用内播放器）</text>
                </div>
            </div>
        </scroller>
    </div>
</template>

<style scoped>
.page {
    flex: 1;
    background-color: #ffffff;
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

.topbar-title {
    font-size: 28px;
    color: #ffffff;
    font-weight: bold;
}

.topbar-back {
    font-size: 24px;
    color: #ffffff;
    opacity: 0.9;
}

.content {
    flex: 1;
    padding: 16px;
    flex-direction: column;
}

.section {
    margin-bottom: 24px;
    background-color: #fafafa;
    border-radius: 8px;
    padding: 12px 16px;
}

.section-title {
    font-size: 18px;
    color: #333333;
    font-weight: bold;
    margin-bottom: 12px;
    padding-left: 4px;
    border-left-width: 3px;
    border-left-color: #fb7299;
}

/* 设置项通用 */
.setting-item {
    flex-direction: row;
    align-items: center;
    justify-content: space-between;
    padding: 12px 4px;
    border-bottom-width: 1px;
    border-bottom-color: #eeeeee;
}

.setting-item:last-child {
    border-bottom-width: 0;
}

.setting-info {
    flex: 1;
    flex-direction: column;
}

.setting-label {
    font-size: 20px;
    color: #333333;
}

.setting-desc {
    margin-top: 4px;
    font-size: 14px;
    color: #999999;
    lines: 2;
}

.setting-switch {
    width: 56px;
    height: 30px;
}

/* 自定义开关样式 */
.custom-switch {
    width: 56px;
    height: 30px;
    border-radius: 15px;
    background-color: #dddddd;
    position: relative;
}

.custom-switch-on {
    background-color: #fb7299;
}

.switch-thumb {
    position: absolute;
    top: 2px;
    left: 2px;
    width: 26px;
    height: 26px;
    border-radius: 13px;
    background-color: #ffffff;
    box-shadow: 0 2px 4px rgba(0,0,0,0.2);
}

.switch-thumb-on {
    left: 28px;
}

/* 说明区域 */
.setting-note {
    margin-top: 8px;
    padding: 12px;
    background-color: #fff8f0;
    border-radius: 6px;
    flex-direction: column;
}

.note-title {
    font-size: 16px;
    color: #e67e22;
    font-weight: bold;
    margin-bottom: 6px;
}

.note-text {
    font-size: 14px;
    color: #997755;
    margin-bottom: 4px;
    lines: 2;
}

/* 信息项 */
.info-item {
    flex-direction: row;
    justify-content: space-between;
}

.info-label {
    font-size: 18px;
    color: #666666;
}

.info-value {
    font-size: 18px;
    color: #333333;
}

/* 危险区 */
.danger-item {
    flex-direction: row;
    justify-content: space-between;
}

.danger-label {
    font-size: 18px;
    color: #e74c3c;
    font-weight: bold;
}

.danger-desc {
    font-size: 14px;
    color: #999999;
}
</style>

<script>
import storage from '../../utils/storage.js'

export default {
    name: 'settings',
    data() {
        return {
            useSystemPlayer: false
        }
    },
    mounted() {
        this.loadSettings()
    },
    methods: {
        loadSettings() {
            const settings = storage.getSettings()
            this.useSystemPlayer = settings.useSystemPlayer || false
        },
        onSystemPlayerToggle() {
            this.useSystemPlayer = !this.useSystemPlayer
            storage.setSetting('useSystemPlayer', this.useSystemPlayer)
            console.warn('[settings] useSystemPlayer changed to:', this.useSystemPlayer)
        },
        onSystemPlayerChange(e) {
            const checked = e.checked === true || e.checked === 'true'
            this.useSystemPlayer = checked
            storage.setSetting('useSystemPlayer', checked)
            console.warn('[settings] useSystemPlayer changed to:', checked)
        },
        resetAllSettings() {
            storage.resetSettings()
            this.useSystemPlayer = false
            console.warn('[settings] all settings reset')
        },
        goBack() {
            $falcon.closePage()
        }
    }
}
</script>