<template>
    <!-- 独立控制栏框架：与视频播放器分开。
         标题栏 + 控制按钮集中在屏幕顶部区域（y=0~100），
         完全避开 waylandsink 视频窗口（pos_y=100 起），
         从页面启动即显示在最上层，永不被视频原生层遮挡。 -->
    <div class="controls-framework">
        <!-- 顶部标题栏 -->
        <div class="top-bar">
            <text class="bar-title" :lines="1">{{ title }}</text>
        </div>

        <!-- 控制按钮栏（紧跟标题栏下方，仍在视频窗口之外） -->
        <div class="ctrl-bar">
            <text class="ctrl-btn" @click="onTogglePlay">{{ paused ? '▶ 播放' : '⏸ 暂停' }}</text>
            <text class="ctrl-btn" @click="onClose">✕ 关闭</text>
        </div>
    </div>
</template>

<style scoped>
.controls-framework {
    /* 独立框架：absolute 固定屏幕顶部 100px 高度区域。
       视频 waylandsink 窗口从 pos_y=100 才开始，顶部 100px 是
       DOM 可见区域，按钮从这里启动即显示，永远不被视频遮挡。 */
    position: absolute;
    top: 0;
    left: 0;
    width: 960px;
    height: 100px;
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

.ctrl-bar {
    position: absolute;
    top: 50px;
    left: 0;
    width: 960px;
    height: 50px;
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