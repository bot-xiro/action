<template>
    <!-- 独立控制栏框架：与视频播放器分开。
         标题栏固定显示在屏幕顶部，控制按钮固定显示在屏幕底部。
         z-index 999 置顶，保证不被视频/任何元素遮挡。 -->
    <div class="controls-framework">
        <!-- 顶部标题栏：名称固定显示在屏幕顶部 -->
        <div class="top-bar">
            <text class="bar-title" :lines="1">{{ title }}</text>
        </div>

        <!-- 底部控制栏：控制按钮固定显示在屏幕底部 -->
        <div class="bottom-bar">
            <text class="ctrl-btn" @click="onTogglePlay">{{ paused ? '▶ 播放' : '⏸ 暂停' }}</text>
            <text class="ctrl-btn" @click="onClose">✕ 关闭</text>
        </div>
    </div>
</template>

<style scoped>
.controls-framework {
    /* 独立框架：absolute 铺满整个屏幕，z-index 999 置顶显示，不随视频/布局移动 */
    position: absolute;
    top: 0;
    left: 0;
    width: 960px;
    height: 266px;
    /* 必须高于视频层（waylandsink hole）及任何其他元素，保证始终显示在最前 */
    z-index: 999;
}

.top-bar {
    position: absolute;
    top: 0;
    left: 0;
    width: 960px;
    height: 50px;
    flex-direction: row;
    align-items: center;
    padding-left: 16px;
    padding-right: 16px;
    background-color: rgba(0, 0, 0, 0.55);
}

.bar-title {
    flex: 1;
    font-size: 20px;
    color: #ffffff;
    lines: 1;
}

.bottom-bar {
    position: absolute;
    bottom: 0;
    left: 0;
    width: 960px;
    height: 66px;
    flex-direction: row;
    align-items: center;
    justify-content: flex-end;
    padding-left: 16px;
    padding-right: 16px;
    background-color: rgba(0, 0, 0, 0.55);
}

.ctrl-btn {
    margin-left: 20px;
    font-size: 20px;
    color: #ffffff;
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
export default {
    name: 'player-controls',
    props: {
        title: { type: String, default: '视频播放' },
        paused: { type: Boolean, default: false }
    },
    methods: {
        onTogglePlay() {
            this.$emit('toggle-play')
        },
        onClose() {
            this.$emit('close')
        }
    }
}
</script>
