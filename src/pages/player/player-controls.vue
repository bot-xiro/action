<template>
    <!-- 独立控制栏框架：与视频播放器分开，方向跟随视频原始朝向。
         横屏视频：标题条在屏幕顶部、控制条在屏幕底部（横向全屏）。
         竖屏视频：整个控制栏翻转 90° 成竖条，贴在视频底部所在侧（竖屏视频
         顺时针旋转 90° 后底部朝屏幕左侧，故竖条贴左边缘）。 -->
    <div class="controls-framework" :class="orientation === 'portrait' ? 'framework-portrait' : 'framework-landscape'">
        <!-- 标题栏：横屏=顶部横条；竖屏=竖条内顶部区域 -->
        <div class="top-bar" :class="orientation === 'portrait' ? 'top-bar-portrait' : 'top-bar-landscape'">
            <text class="bar-title" :lines="1">{{ title }}</text>
        </div>

        <!-- 控制栏：横屏=底部横条（按钮横排）；竖屏=竖条底部区域（按钮竖排） -->
        <div class="bottom-bar" :class="orientation === 'portrait' ? 'bottom-bar-portrait' : 'bottom-bar-landscape'">
            <text class="ctrl-btn" :class="orientation === 'portrait' ? 'ctrl-btn-portrait' : 'ctrl-btn-landscape'" @click="onTogglePlay">{{ paused ? '▶ 播放' : '⏸ 暂停' }}</text>
            <text class="ctrl-btn" :class="orientation === 'portrait' ? 'ctrl-btn-portrait' : 'ctrl-btn-landscape'" @click="onClose">✕ 关闭</text>
        </div>
    </div>
</template>

<style scoped>
/* 横屏模式：铺满全屏的独立框架 */
.framework-landscape {
    position: absolute;
    top: 0;
    left: 0;
    width: 960px;
    height: 266px;
    /* 必须高于视频层（waylandsink hole），否则控制栏被视频盖住无法显示/点击 */
    z-index: 100;
}

/* 竖屏模式：窄竖条，贴屏幕左边缘（视频顺时针旋转 90° 后底部所在侧） */
.framework-portrait {
    position: absolute;
    top: 0;
    left: 0;
    width: 90px;
    height: 266px;
    z-index: 100;
}

.top-bar-landscape {
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

/* 竖屏：标题在竖条顶部，横排截断显示 */
.top-bar-portrait {
    position: absolute;
    top: 0;
    left: 0;
    width: 90px;
    height: 50px;
    flex-direction: row;
    align-items: center;
    padding-left: 8px;
    padding-right: 8px;
    background-color: rgba(0, 0, 0, 0.55);
}

.bar-title {
    flex: 1;
    font-size: 20px;
    color: #ffffff;
    lines: 1;
}

.bottom-bar-landscape {
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

/* 竖屏：按钮在竖条底部竖排 */
.bottom-bar-portrait {
    position: absolute;
    bottom: 0;
    left: 0;
    width: 90px;
    height: 130px;
    flex-direction: column;
    align-items: center;
    justify-content: flex-end;
    padding-top: 8px;
    padding-bottom: 12px;
    background-color: rgba(0, 0, 0, 0.55);
}

.ctrl-btn-landscape {
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

/* 竖屏按钮：上下排列，紧凑宽度 */
.ctrl-btn-portrait {
    margin-top: 10px;
    font-size: 16px;
    color: #ffffff;
    padding-top: 6px;
    padding-right: 8px;
    padding-bottom: 6px;
    padding-left: 8px;
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
        paused: { type: Boolean, default: false },
        orientation: { type: String, default: 'landscape' }
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
