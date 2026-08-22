<template>
    <div class="page">
        <!-- 顶部栏：返回 + 标题 -->
        <div class="topbar">
            <text class="back-btn" @click="goBack">‹ 返回</text>
            <text class="topbar-title">设置</text>
            <text class="topbar-right"></text>
        </div>

        <!-- 内容滚动区：使用原生 scroller 组件，完全参考首页 index.vue 的写法 -->
        <scroller class="list" scroll-direction="vertical" :show-scrollbar="false" :over-scroll="60" @scroll="onScroll" @scrolltolower="onLoadMore">
            <div class="list-inner">
                <!-- 登录区块 -->
                <div class="section">
                    <text class="section-title">账号</text>

                    <!-- 未登录：显示登录选项 -->
                    <div class="option" v-if="!loggedIn" @click="navToQRLogin">
                        <div class="option-left">
                            <text class="option-name">扫码登录</text>
                            <text class="option-desc">使用哔哩哔哩 APP 扫码登录</text>
                        </div>
                        <text class="option-badge">去登录</text>
                    </div>

                    <div class="option" v-if="!loggedIn" @click="navToCookieLogin">
                        <div class="option-left">
                            <text class="option-name">Cookie 登录</text>
                            <text class="option-desc">从电脑同步 Cookie（方式2）</text>
                        </div>
                        <text class="option-badge">去登录</text>
                    </div>
                    <div class="option" v-else>
                        <div class="option-left">
                            <text class="option-name">{{ userInfo.uname || 'Bilibili 用户' }}</text>
                            <text class="option-desc">UID: {{ userInfo.mid || '-' }}  •  等级: {{ userInfo.level || '-' }}</text>
                        </div>
                        <text class="option-badge" @click.stop="onLogout">退出登录</text>
                    </div>
                </div>

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

                <!-- 其他设置 -->
                <div class="section">
                    <text class="section-title">其他</text>

                    <div class="option" @click="onClearCache">
                        <div class="option-left">
                            <text class="option-name">清理缓存</text>
                            <text class="option-desc">清除搜索历史、播放记录等本地数据</text>
                        </div>
                    </div>

                    <div class="option" @click="onAbout">
                        <div class="option-left">
                            <text class="option-name">关于</text>
                            <text class="option-desc">版本信息、开源协议</text>
                        </div>
                    </div>
                </div>

                <!-- 说明 -->
                <div class="tips">
                    <text class="tips-text">提示：切换播放器后进入播放页生效。</text>
                    <text class="tips-text">系统播放器 = 调起系统视频播放器应用播放（8001661999525016）：自带控制栏悬浮、无层级遮挡问题；播放期间本应用退到后台，返回后继续浏览。</text>
                    <text class="tips-text">自研播放器 = gstplayer（KMS 双平面），视频独立平面渲染；控制栏唤出时视频画面让位。</text>
                    <text class="tips-text">登录数据保存在系统存储，卸载应用不丢失。</text>
                </div>
            </div>
        </scroller>
    </div>
</template>

<style scoped>
.page {
    position: relative;
    flex: 1;
    background-color: #f5f6f7;
    flex-direction: column;
}

.list {
    position: absolute;
    top: 48px;
    bottom: 0;
    left: 0;
    right: 0;
    flex-direction: column;
}

.list-inner {
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
import auth from '../../utils/auth.js'

export default {
    name: 'settings',
    data() {
        return {
            mode: settings.DEFAULT_MODE,
            loggedIn: false,
            userInfo: {}
        }
    },
    mounted() {
        var self = this
        console.warn('[settings] mounted')
        
        // 加载播放器模式
        settings.getMode().then(function (m) {
            self.mode = m
            console.warn('[settings] loaded mode=' + m)
        })
        
        // 检查登录状态
        this.checkLoginStatus()
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
        
        checkLoginStatus() {
            var self = this
            auth.isLoggedIn().then(function (loggedIn) {
                self.loggedIn = loggedIn
                if (loggedIn) {
                    self.loadUserInfo()
                }
            })
        },
        
        loadUserInfo() {
            var self = this
            auth.getUserInfo().then(function (info) {
                self.userInfo = info || {}
            })
        },
        
        onSelect(mode) {
            var self = this
            console.warn('[settings] select mode=' + mode + ' (current=' + this.mode + ')')
            if (this.mode === mode) return
            settings.setMode(mode).then(function (saved) {
                self.mode = saved
                console.warn('[settings] saved mode=' + saved)
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
        },
        
        // 导航到二维码登录页面
        navToQRLogin() {
            console.log('[settings] navToQRLogin')
            $falcon.navTo('qr-login', {})
        },
        
        // 导航到 Cookie 登录页面
        navToCookieLogin() {
            console.log('[settings] navToCookieLogin')
            $falcon.navTo('cookie-login', {})
        },
        
        onLogout() {
            var self = this
            auth.logout().then(function () {
                self.loggedIn = false
                self.userInfo = {}
                try {
                    var m = $falcon.jsapi && $falcon.jsapi.modal
                    if (m && typeof m.toast === 'function') {
                        m.toast({ message: '已退出登录', duration: 2000 })
                    }
                } catch (e) {}
            })
        },
        
        onClearCache() {
            var self = this
            try {
                var m = $falcon.jsapi && $falcon.jsapi.modal
                if (m && typeof m.confirm === 'function') {
                    m.confirm({
                        title: '清理缓存',
                        message: '将清除搜索历史、播放记录等本地数据，确定吗？',
                        confirmText: '确定',
                        cancelText: '取消'
                    }).then(function (res) {
                        if (res && res.confirm) {
                            auth.clearCookie()
                            // 同时清理 storage.js 的搜索历史
                            var storage = require('../../utils/storage.js')
                            storage.clearHistory()
                            if (m && typeof m.toast === 'function') {
                                m.toast({ message: '缓存已清理', duration: 2000 })
                            }
                        }
                    })
                }
            } catch (e) {
                console.warn('[settings] clearCache error: ' + e)
            }
        },
        
        onAbout() {
            try {
                var m = $falcon.jsapi && $falcon.jsapi.modal
                if (m && typeof m.alert === 'function') {
                    m.alert({
                        title: '关于',
                        message: 'Bilibili MiniApp for X6PRO\n版本: 1.0.0\n基于 HAAS UI 框架开发\n数据存储: 系统存储',
                        confirmText: '确定'
                    })
                }
            } catch (e) {}
        },
        
        onScroll(e) {
            // contentOffset.y < 0 表示顶部下拉越界（over-scroll 回弹区域）
            if (!e || !e.contentOffset) {
                return
            }
            var y = e.contentOffset.y || 0
            if (y < -40) {
                // 可以在这里添加下拉刷新逻辑，目前设置页不需要下拉刷新
            }
        },
        onLoadMore() {
            // 设置页不需要加载更多，留空即可
        }
    }
}
</script>