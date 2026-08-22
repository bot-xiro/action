<template>
    <div class="page" ref="pageRoot">
        <!-- 顶部栏：返回 + 标题 -->
        <div class="topbar">
            <text class="back-btn" @click="goBack">‹ 返回</text>
            <text class="topbar-title">Cookie 登录</text>
            <text class="topbar-right"></text>
        </div>

        <!-- 内容滚动区 -->
        <scroller class="list" scroll-direction="vertical" :show-scrollbar="false" :over-scroll="60" @scroll="onScroll" @scrolltolower="onLoadMore">
            <div class="list-inner">
                <div class="section cookie-section">
                    <text class="cookie-tip">电脑同步服务：请在电脑上运行同步服务，输入电脑 IP 点击下方按钮自动获取 Cookie</text>
                    
                    <!-- IP 输入框 - 点击打开系统键盘 miniapp -->
                    <div class="input-wrapper">
                        <text class="input-label">电脑 IP 地址</text>
                        <div class="ip-display" @click="openKeyboard">
                            <text class="ip-text">{{ computerIp || '点击输入电脑 IP' }}</text>
                            <text class="ip-hint">点击调用系统键盘输入</text>
                        </div>
                    </div>
                    
                    <text class="cookie-status">{{ cookieStatus }}</text>
                    
                    <div class="cookie-btns">
                        <text class="cookie-btn cancel" @click="goBack">取消</text>
                        <text class="cookie-btn confirm" @click="fetchCookieFromComputer">从电脑获取</text>
                    </div>
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

.cookie-section {
    align-items: stretch;
}

.cookie-tip {
    font-size: 14px;
    color: #666666;
    lines: 2;
    margin-bottom: 16px;
}

.input-wrapper {
    flex-direction: column;
}

.input-label {
    font-size: 12px;
    color: #999999;
    margin-bottom: 8px;
}

.ip-display {
    width: 100%;
    height: 48px;
    flex-direction: column;
    justify-content: center;
    padding-left: 16px;
    padding-right: 16px;
    background-color: #fafafa;
    border: 1px solid #e0e0e0;
    border-radius: 8px;
}

.ip-text {
    font-size: 18px;
    color: #333333;
}

.ip-hint {
    font-size: 12px;
    color: #999999;
    margin-top: 2px;
}

.cookie-status {
    font-size: 14px;
    color: #fb7299;
    margin-bottom: 16px;
    min-height: 20px;
    margin-top: 16px;
}

.cookie-btns {
    flex-direction: row;
    justify-content: space-between;
    gap: 12px;
    margin-top: 16px;
}

.cookie-btn {
    flex: 1;
    height: 48px;
    line-height: 48px;
    text-align: center;
    border-radius: 8px;
    font-size: 16px;
}

.cookie-btn.cancel {
    color: #888888;
    background-color: #f0f0f0;
}

.cookie-btn.confirm {
    color: #ffffff;
    background-color: #fb7299;
}
</style>

<script>
import auth from '../../utils/auth.js'

export default {
    name: 'cookie-login',
    data() {
        return {
            computerIp: '192.168.1.100',
            cookieStatus: ''
        }
    },
    mounted() {
        console.warn('[cookie-login] mounted')
    },
    methods: {
        goBack() {
            console.log('[cookie-login] goBack')
            try {
                this.$page.finish()
            } catch (e) {
                console.warn('[cookie-login] finish error: ' + (e ? e.message : e))
            }
        },
        
        // 打开系统键盘 miniapp - 顺序尝试多种参数
        openKeyboard() {
            console.warn('[cookie-login] openKeyboard')
            this.tryKeyboard0()
        },
        
        tryKeyboard0() {
            var self = this
            $falcon.navTo('keyboard', { text: this.computerIp }).then(function(res) {
                self.handleKeyboardResult(res, 1)
            }).catch(function(e) {
                console.warn('[cookie-login] method 1 error: ' + e)
                self.tryKeyboard1()
            })
        },
        tryKeyboard1() {
            var self = this
            $falcon.navTo('keyboard', { inputText: this.computerIp }).then(function(res) {
                self.handleKeyboardResult(res, 2)
            }).catch(function(e) {
                self.tryKeyboard2()
            })
        },
        tryKeyboard2() {
            var self = this
            $falcon.navTo('keyboard', { initialValue: this.computerIp }).then(function(res) {
                self.handleKeyboardResult(res, 3)
            }).catch(function(e) {
                self.tryKeyboard3()
            })
        },
        tryKeyboard3() {
            var self = this
            $falcon.navTo('keyboard', { value: this.computerIp }).then(function(res) {
                self.handleKeyboardResult(res, 4)
            }).catch(function(e) {
                self.tryKeyboard4()
            })
        },
        tryKeyboard4() {
            var self = this
            $falcon.navTo('8001666679481944', { text: this.computerIp }).then(function(res) {
                self.handleKeyboardResult(res, 5)
            }).catch(function(e) {
                self.tryKeyboard5()
            })
        },
        tryKeyboard5() {
            var self = this
            $falcon.navTo('8001666679481944', { inputText: this.computerIp }).then(function(res) {
                self.handleKeyboardResult(res, 6)
            }).catch(function(e) {
                self.tryKeyboard6()
            })
        },
        tryKeyboard6() {
            var self = this
            $falcon.navTo('keyboard', {}).then(function(res) {
                self.handleKeyboardResult(res, 7)
            }).catch(function(e) {
                self.tryKeyboard7()
            })
        },
        tryKeyboard7() {
            var self = this
            $falcon.navTo('keyboard', { text: this.computerIp, type: 'text' }).then(function(res) {
                self.handleKeyboardResult(res, 8)
            }).catch(function(e) {
                self.tryKeyboard8()
            })
        },
        tryKeyboard8() {
            var self = this
            $falcon.navTo('keyboard', { text: this.computerIp, mode: 'text' }).then(function(res) {
                self.handleKeyboardResult(res, 9)
            }).catch(function(e) {
                self.tryKeyboard9()
            })
        },
        tryKeyboard9() {
            var self = this
            $falcon.navTo('keyboard', { text: this.computerIp, keyboardType: 'number' }).then(function(res) {
                self.handleKeyboardResult(res, 10)
            }).catch(function(e) {
                console.warn('[cookie-login] all keyboard methods failed')
                self.cookieStatus = '所有键盘调用方式均失败'
            })
        },
        
        handleKeyboardResult(res, methodNum) {
            console.warn('[cookie-login] keyboard method ' + methodNum + ' result: ' + JSON.stringify(res))
            if (res && (res.text || res.value || (res.data && res.data.text) || res.inputText || res.result || res.data)) {
                var text = res.text || res.value || (res.data && res.data.text) || res.inputText || res.result || res.data
                this.computerIp = text.trim()
                this.cookieStatus = '已输入: ' + text
            } else {
                console.warn('[cookie-login] keyboard returned empty')
            }
        },
        
        onCookieInput(val) {
            this.cookieStatus = ''
            this.computerIp = val
        },
        
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
                    try {
                        var m = $falcon.jsapi && $falcon.jsapi.modal
                        if (m && typeof m.toast === 'function') {
                            m.toast({ message: 'Cookie 获取成功', duration: 2000 })
                        }
                    } catch (e) {}
                    setTimeout(function () {
                        self.goBack()
                    }, 1000)
                } else {
                    self.cookieStatus = '获取失败: ' + (res.message || '未知错误')
                }
            }).catch(function (e) {
                self.cookieStatus = '获取异常: ' + (e && e.message ? e.message : String(e))
            })
        },
        
        goBack() {
            console.log('[cookie-login] goBack')
            try {
                this.$page.finish()
            } catch (e) {
                console.warn('[cookie-login] finish error: ' + (e ? e.message : e))
            }
        },
        
        onScroll(e) {
        },
        
        onLoadMore() {
        }
    }
}
</script>