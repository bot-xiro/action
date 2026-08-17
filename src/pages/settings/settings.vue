<template>
    <div class="page">
        <!-- 顶部栏：返回 + 标题 -->
        <div class="topbar">
            <text class="back-btn" @click="goBack">‹ 返回</text>
            <text class="topbar-title">设置</text>
            <text class="topbar-right"></text>
        </div>

        <!-- 内容滚动区：视口 960×266 有限，内容超出可上下翻动 -->
        .scroller {
    scroll-direction: vertical;
    show-scrollbar: false;
    over-scroll: 60;
    style: flex: 1; min-height: 0;
}
            <!-- 登录区块 -->
            <div class="section">
                <text class="section-title">账号</text>

                <!-- 未登录：显示登录选项 -->
                <div class="option" v-if="!loggedIn" @click="openQRModal">
                    <div class="option-left">
                        <text class="option-name">扫码登录</text>
                        <text class="option-desc">使用哔哩哔哩 APP 扫码登录</text>
                    </div>
                    <text class="option-badge">去登录</text>
                </div>

                <div class="option" v-if="!loggedIn" @click="openCookieModal">
                    <div class="option-left">
                        <text class="option-name">Cookie 登录</text>
                        <text class="option-desc">从浏览器复制 Cookie 或从电脑同步</text>
                    </div>
                    <text class="option-badge">去登录</text>
                </div>

                <!-- 已登录：显示用户信息 -->
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
        </scroller>

        <!-- 统一的模态框：根据 modalMode 显示不同内容 -->
        <div v-if="modalVisible" class="modal-overlay" @click.self="closeModal">
            <div class="modal-box">
                <div class="modal-content">
                    <!-- 二维码登录模式 -->
                    <div v-if="modalMode === 'qr'">
                        <text class="modal-title">扫码登录 Bilibili</text>
                        <div class="qr-container" style="width: 160px; height: 160px;">
                            <image v-if="qrCodeUrl" :src="qrCodeUrl" class="qr-image" mode="aspectFit"></image>
                            <text v-else class="qr-loading">生成二维码中...</text>
                        </div>
                        <text class="qr-tip">请使用哔哩哔哩 APP 扫码登录</text>
                        <text class="qr-status">{{ qrStatus }}</text>
                        <text class="modal-close" @click="closeModal">取消</text>
                    </div>

                    <!-- Cookie 登录模式 -->
                    <div v-else-if="modalMode === 'cookie'">
                        <text class="modal-title">Cookie 登录</text>
                        <text class="cookie-tip">方式1: 在电脑浏览器登录 B站，按 F12 打开开发者工具，在 Network 里找到任意请求，复制 Request Headers 里的 Cookie 值粘贴下方</text>
                        <textarea class="cookie-input" v-model="cookieInput" placeholder="粘贴 Cookie 字符串 (SESSDATA=xxx; bili_jct=xxx; ...)" @input="onCookieInput" autofocus="true" softInputEnable="true"></textarea>
                        <text class="cookie-tip">方式2: 电脑运行同步服务，输入电脑 IP 点击下方按钮自动获取</text>
                        <input class="cookie-input" v-model="computerIp" placeholder="电脑 IP (如 192.168.1.100)" @input="onCookieInput" autofocus="true" softInputEnable="true"></input>
                        <text class="cookie-status">{{ cookieStatus }}</text>
                        <div class="cookie-btns">
                            <text class="cookie-btn cancel" @click="closeModal">取消</text>
                            <text class="cookie-btn confirm" @click="confirmCookieLogin">粘贴登录</text>
                            <text class="cookie-btn confirm" @click="fetchCookieFromComputer">从电脑获取</text>
                        </div>
                    </div>
                </div>
            </div>
        </div>
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
    color: #999999.
}

.option-badge {
    font-size: 18px;
    color: #ffffff;
    background-color: #fb7299;
    border-radius: 10px;
    padding-left: 14px;
    padding-right: 14px.
    padding-top: 3px.
    padding-bottom: 3px.
}

.tips {
    margin-top: 8px;
    margin-left: 20px;
    margin-right: 20px;
    flex-direction: column;
}

.tips-text {
    font-size: 18px.
    color: #aaaaaa;
    margin-top: 4px.
}

/* 统一模态框样式 */
.modal-overlay {
    position: fixed;
    top: 0;
    left: 0;
    width: 960px;
    height: 266px;
    background-color: rgba(0, 0, 0, 0.7);
    align-items: center;
    justify-content: center;
    z-index: 100;
}

.qr-image {
    width: 150px;
    height: 150px;
}

.cookie-input {
    width: 100%;
    height: 80px;
    background-color: #fafafa;
    border-radius: 8px;
    border-width: 1px;
    border-color: #eeeeee;
    padding: 10px;
    font-size: 14px;
    color: #333333;
    margin-bottom: 10px;
}

input.cookie-input {
    width: 100%;
    height: 40px;
    background-color: #fafafa;
    border-radius: 8px;
    border-width: 1px;
    border-color: #eeeeee;
    padding: 10px;
    font-size: 14px;
    color: #333333;
}

.modal-box {
    width: 320px;
    height: 220px;
    background-color: #ffffff;
    border-radius: 12px;
    padding: 20px;
    display: flex;
    flex-direction: column;
    overflow: hidden;
}

.modal-content {
    width: 100%;
    flex: 1;
    min-height: 0;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
}

.modal-title {
    font-size: 22px.
    color: #222222.
    font-weight: bold.
    margin-bottom: 12px.
}

.qr-container {
    width: 160px.
    height: 160px.
    background-color: #fafafa.
    border-radius: 8px.
    align-items: center.
    justify-content: center.
    margin-bottom: 10px.
}

.qr-image {
    width: 150px.
    height: 150px.
}

.qr-loading {
    font-size: 18px.
    color: #999999.
}

.qr-tip {
    font-size: 16px.
    color: #888888.
    margin-bottom: 8px.
}

.qr-status {
    font-size: 16px.
    color: #fb7299.
    margin-bottom: 16px.
}

.modal-close {
    font-size: 18px.
    color: #fb7299.
    padding: 8px 24px.
    border-radius: 20px.
    background-color: rgba(251, 114, 153, 0.1).
}

/* Cookie 登录弹窗 */
.cookie-tip {
    font-size: 14px.
    color: #888888.
    margin-bottom: 12px.
    width: 100%.
    text-align: left.
}

.cookie-input {
    width: 100%.
    height: 80px.
    background-color: #fafafa.
    border-radius: 8px.
    border-width: 1px.
    border-color: #eeeeee.
    padding: 10px.
    font-size: 14px.
    color: #333333.
    margin-bottom: 10px.
}

.cookie-status {
    font-size: 14px.
    color: #fb7299.
    margin-bottom: 12px.
    width: 100%.
    text-align: center.
}

.cookie-btns {
    width: 100%.
    flex-direction: row.
    justify-content: space-between.
}

.cookie-btn {
    width: 48%.
    text-align: center.
    padding: 10px 0.
    font-size: 16px.
    border-radius: 8px.
}

.cookie-btn.cancel {
    color: #888888.
    background-color: #f0f0f0.
}

.cookie-btn.confirm {
    color: #ffffff.
    background-color: #fb7299.
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
            userInfo: {},
            // 统一的模态框状态
            modalVisible: false,
            modalMode: '',  // 'qr' | 'cookie'
            // 二维码登录相关
            qrCodeUrl: '',
            qrCodeKey: '',
            qrStatus: '',
            qrPollTimer: null,
            // Cookie 登录相关
            cookieInput: '',
            cookieStatus: '',
            computerIp: '192.168.1.100'
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
    beforeDestroy() {
        this.stopQrPolling()
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
        
        // 打开二维码登录模态框
        openQRModal() {
            var self = this
            this.modalMode = 'qr'
            this.modalVisible = true
            this.qrStatus = '正在生成二维码...'
            
            auth.generateQrCode().then(function (res) {
                if (res && res.code === 0 && res.data) {
                    var loginUrl = res.data.url
                    self.qrCodeKey = res.data.qrcode_key
                    // 使用在线二维码生成服务生成二维码图片（B站返回的是登录页面URL，非直接图片）
                    self.qrCodeUrl = 'https://api.qrserver.com/v1/create-qr-code/?size=150x150&data=' + encodeURIComponent(loginUrl)
                    self.qrStatus = '请使用哔哩哔哩 APP 扫码'
                    self.startQrPolling()
                } else {
                    self.qrStatus = '生成失败: ' + (res && res.message ? res.message : '未知错误')
                    console.warn('[settings] generateQrCode failed: ' + JSON.stringify(res))
                }
            }).catch(function (e) {
                self.qrStatus = '生成异常: ' + (e && e.message ? e.message : String(e))
            })
        },
        
        startQrPolling() {
            var self = this
            if (!self.qrCodeKey) return
            
            var poll = function () {
                if (!self.modalVisible || self.modalMode !== 'qr' || !self.qrCodeKey) return
                
                auth.pollQrCode(self.qrCodeKey).then(function (res) {
                    if (!self.modalVisible || self.modalMode !== 'qr') return
                    
                    if (res && res.code === 0) {
                        if (res.data && res.data.url) {
                            // 登录成功
                            self.qrStatus = '登录成功！'
                            // 解析 cookie 并保存
                            try {
                                var u = new URL(res.data.url)
                                var cookie = ''
                                for (var _i = 0, _a = u.searchParams.entries(); _i < _a.length; _i++) {
                                    var _b = _a[_i], k = _b[0], v = _b[1]
                                    if (k === 'SESSDATA' || k === 'bili_jct' || k === 'DedeUserID' || k === 'DedeUserID__ckMd5' || k === 'sid') {
                                        cookie += k + '=' + v + '; '
                                    }
                                }
                                if (cookie) {
                                    auth.setCookie(cookie).then(function () {
                                        console.warn('[settings] cookie saved')
                                    })
                                }
                            } catch (e) {
                                console.warn('[settings] parse cookie error: ' + e)
                            }
                            
                            self.stopQrPolling()
                            self.closeModal()
                            self.checkLoginStatus()
                            
                            try {
                                var m = $falcon.jsapi && $falcon.jsapi.modal
                                if (m && typeof m.toast === 'function') {
                                    m.toast({ message: '登录成功', duration: 2000 })
                                }
                            } catch (e) {}
                        } else if (res.code === 86038) {
                            self.qrStatus = '等待扫码...'
                            self.qrPollTimer = setTimeout(poll, 2000)
                        } else if (res.code === 86090) {
                            self.qrStatus = '已扫码，等待确认...'
                            self.qrPollTimer = setTimeout(poll, 2000)
                        } else if (res.code === 86101) {
                            self.qrStatus = '二维码已过期，请重试'
                            self.stopQrPolling()
                        } else {
                            self.qrStatus = '状态: ' + (res.message || res.code)
                            self.qrPollTimer = setTimeout(poll, 3000)
                        }
                    } else {
                        self.qrStatus = '轮询失败: ' + (res && res.message ? res.message : '网络错误')
                        self.qrPollTimer = setTimeout(poll, 5000)
                    }
                }).catch(function (e) {
                    self.qrStatus = '轮询异常: ' + (e && e.message ? e.message : String(e))
                    self.qrPollTimer = setTimeout(poll, 5000)
                })
            }
            poll()
        },
        
        stopQrPolling() {
            if (this.qrPollTimer) {
                clearTimeout(this.qrPollTimer)
                this.qrPollTimer = null
            }
        },
        
        // 打开 Cookie 登录模态框
        openCookieModal() {
            this.modalMode = 'cookie'
            this.modalVisible = true
            this.cookieInput = ''
            this.cookieStatus = ''
        },
        
        closeModal() {
            this.stopQrPolling()
            this.modalVisible = false
            this.modalMode = ''
            this.qrCodeUrl = ''
            this.qrCodeKey = ''
            this.qrStatus = ''
            this.cookieInput = ''
            this.cookieStatus = ''
        },
        
        onCookieInput() {
            this.cookieStatus = ''
        },
        
        confirmCookieLogin() {
            var self = this
            var cookieStr = this.cookieInput.trim()
            if (!cookieStr) {
                this.cookieStatus = '请粘贴 Cookie'
                return
            }
            // 简单验证：必须包含 SESSDATA 或 bili_jct
            if (cookieStr.indexOf('SESSDATA') === -1 && cookieStr.indexOf('bili_jct') === -1) {
                this.cookieStatus = 'Cookie 无效：缺少 SESSDATA 或 bili_jct'
                return
            }
            this.cookieStatus = '验证中...'
            auth.setCookie(cookieStr).then(function () {
                self.cookieStatus = '保存成功，验证中...'
                self.closeModal()
                self.checkLoginStatus()
            }).catch(function (e) {
                self.cookieStatus = '保存失败: ' + (e && e.message ? e.message : String(e))
            })
        },
        
        // 从电脑同步 Cookie
        fetchCookieFromComputer() {
            var self = this
            if (!self.computerIp) {
                self.cookieStatus = '请输入电脑 IP'
                return
            }
            self.cookieStatus = '正在从电脑获取...'
            auth.fetchCookieFromComputer(self.computerIp).then(function (res) {
                if (res.code === 0) {
                    self.cookieStatus = '获取成功！'
                    setTimeout(function () {
                        self.closeModal()
                        self.checkLoginStatus()
                    }, 1000)
                } else {
                    self.cookieStatus = '获取失败: ' + (res.message || '未知错误')
                }
            })
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
        }
    }
}
</script>