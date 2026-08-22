<template>
    <div class="page">
        <!-- 顶部栏：返回 + 标题 -->
        <div class="topbar">
            <text class="back-btn" @click="goBack">‹ 返回</text>
            <text class="topbar-title">扫码登录 Bilibili</text>
            <text class="topbar-right"></text>
        </div>

        <!-- 内容滚动区 -->
        <scroller class="list" scroll-direction="vertical" :show-scrollbar="false" :over-scroll="60" @scroll="onScroll" @scrolltolower="onLoadMore">
            <div class="list-inner">
                <div class="section qr-section">
                    <text class="qr-tip">请使用哔哩哔哩 APP 扫码登录</text>
                    
                    <div class="qr-container">
                        <image v-if="qrCodeUrl" :src="qrCodeUrl" class="qr-image" mode="aspectFit" style="width: 180px; height: 180px;"></image>
                        <text v-else class="qr-loading">生成二维码中...</text>
                    </div>
                    
                    <text class="qr-status">{{ qrStatus }}</text>
                    
                    <text class="qr-tip small">二维码 3 分钟内有效，过期请返回重试</text>
                    
                    <text class="btn cancel" @click="goBack">取消</text>
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
    background-color: #ffffff;
    margin-top: 12px;
    padding: 20px 16px;
    flex-direction: column;
}

.qr-section {
    align-items: center;
}

.qr-container {
    width: 200px;
    height: 200px;
    align-items: center;
    justify-content: center;
    margin-top: 20px;
    margin-bottom: 16px;
}

.qr-image {
    width: 180px;
    height: 180px;
}

.qr-loading {
    font-size: 16px;
    color: #999999;
}

.qr-tip {
    font-size: 16px;
    color: #333333;
    margin-bottom: 12px;
    text-align: center;
}

.qr-tip.small {
    font-size: 13px;
    color: #999999;
    margin-top: 8px;
    margin-bottom: 20px;
}

.qr-status {
    font-size: 15px;
    color: #fb7299;
    margin-top: 12px;
    margin-bottom: 12px;
    text-align: center;
}

.btn {
    width: 200px;
    height: 44px;
    line-height: 44px;
    text-align: center;
    border-radius: 8px;
    font-size: 16px;
    margin-top: 16px;
}

.btn.cancel {
    color: #888888;
    background-color: #f0f0f0;
}

.btn.confirm {
    color: #ffffff;
    background-color: #fb7299;
}
</style>

<script>
import auth from '../../utils/auth.js'

export default {
    name: 'qr-login',
    data() {
        return {
            qrCodeUrl: '',
            qrCodeKey: '',
            qrStatus: '',
            qrPollTimer: null
        }
    },
    mounted() {
        console.warn('[qr-login] mounted')
        this.generateQrCode()
    },
    beforeDestroy() {
        this.stopQrPolling()
    },
    methods: {
        goBack() {
            console.log('[qr-login] goBack')
            try {
                this.$page.finish()
            } catch (e) {
                console.warn('[qr-login] finish error: ' + (e ? e.message : e))
            }
        },
        
        generateQrCode() {
            var self = this
            this.qrStatus = '正在生成二维码...'
            
            auth.generateQrCode().then(function (res) {
                if (res && res.code === 0 && res.data) {
                    var loginUrl = res.data.url
                    self.qrCodeKey = res.data.qrcode_key
                    self.qrCodeUrl = 'https://api.qrserver.com/v1/create-qr-code/?size=150x150&data=' + encodeURIComponent(loginUrl)
                    self.qrStatus = '请使用哔哩哔哩 APP 扫码'
                    self.startQrPolling()
                } else {
                    self.qrStatus = '生成失败: ' + (res && res.message ? res.message : '未知错误')
                    console.warn('[qr-login] generateQrCode failed: ' + JSON.stringify(res))
                }
            }).catch(function (e) {
                self.qrStatus = '生成异常: ' + (e && e.message ? e.message : String(e))
            })
        },
        
        startQrPolling() {
            var self = this
            if (!self.qrCodeKey) return
            
            var poll = function () {
                if (!self.qrCodeKey) return
                
                auth.pollQrCode(self.qrCodeKey).then(function (res) {
                    if (!self.qrCodeKey) return
                    
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
                                        console.warn('[qr-login] cookie saved')
                                    })
                                }
                            } catch (e) {
                                console.warn('[qr-login] parse cookie error: ' + e)
                            }
                            
                            self.stopQrPolling()
                            
                            try {
                                var m = $falcon.jsapi && $falcon.jsapi.modal
                                if (m && typeof m.toast === 'function') {
                                    m.toast({ message: '登录成功', duration: 2000 })
                                }
                            } catch (e) {}
                            
                            // 延迟返回上一页，让用户看到成功提示
                            setTimeout(function () {
                                self.goBack()
                            }, 1500)
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
        
        onScroll(e) {
            // 空实现，防止 scroller 报错
        },
        
        onLoadMore() {
            // 空实现
        }
    }
}
</script>