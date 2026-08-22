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
                    
                    <!-- 单个 textarea 输入框 - 原生软键盘支持 -->
                    <!-- 移除 autofocus，只在用户点击时聚焦 -->
                    <div class="input-container" @click="onContainerClick">
                        <text class="input-label">电脑 IP 地址</text>
                        <textarea 
                            ref="ipInput"
                            class="cookie-input" 
                            v-model="computerIp" 
                            placeholder="电脑 IP (如 192.168.1.100)" 
                            @input="onCookieInput"
                            @focus="onFocus"
                            @blur="onBlur"
                            @click="onTextareaClick"
                            :softInputEnable="true"
                            :single-line="true"
                            style="height: 48px;">
                        </textarea>
                    </div>
                    
                    <text class="cookie-status">{{ cookieStatus }}</text>
                    
                    <div class="cookie-btns">
                        <text class="cookie-btn cancel" @click="goBack">取消</text>
                        <text class="cookie-btn confirm" @click="fetchCookieFromComputer">从电脑获取</text>
                    </div>
                    
                    <!-- 调试按钮：手动触发键盘 -->
                    <text class="debug-btn" @click="debugKeyboard">🔧 测试键盘调用</text>
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

.cookie-input {
    width: 100%;
    height: 48px;
    font-size: 16px;
    color: #333333;
    background-color: #fafafa;
    border: 1px solid #e0e0e0;
    border-radius: 8px;
    padding-left: 12px;
    padding-right: 12px;
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

/* 调试按钮 */
.debug-btn {
    height: 40px;
    line-height: 40px;
    text-align: center;
    font-size: 14px;
    color: #ffffff;
    background-color: #666666;
    border-radius: 6px;
    margin-top: 20px;
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
        console.warn('[cookie-login] mounted - NOT autofocusing, waiting for user click')
        // 不再自动聚焦，等待用户点击
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
        
        // 显式聚焦输入框 - 触发软键盘
        tryFocusInput() {
            console.warn('[cookie-login] tryFocusInput called - blur first then focus')
            var inputRef = this.$refs.ipInput
            if (inputRef) {
                console.warn('[cookie-login] ipInput ref found, blur then focus')
                try {
                    // 先 blur 再 focus，强制触发软键盘
                    if (typeof inputRef.blur === 'function') {
                        inputRef.blur()
                        console.warn('[cookie-login] blur() called')
                    }
                    // 短暂延迟后 focus
                    setTimeout(function() {
                        if (typeof inputRef.focus === 'function') {
                            inputRef.focus()
                            console.warn('[cookie-login] focus() called successfully after blur')
                        } else if (inputRef.$el && typeof inputRef.$el.focus === 'function') {
                            inputRef.$el.focus()
                            console.warn('[cookie-login] $el.focus() called successfully')
                        } else {
                            console.warn('[cookie-login] no focus method available on ref')
                        }
                    }.bind(this), 50)
                } catch (e) {
                    console.warn('[cookie-login] focus error: ' + e)
                }
            } else {
                console.warn('[cookie-login] ipInput ref NOT found')
            }
        },
        
        // 点击输入框时显式聚焦
        onTextareaClick() {
            console.warn('[cookie-login] onTextareaClick - manually focusing')
            this.tryFocusInput()
        },
        
        // 调试按钮：手动触发键盘
        debugKeyboard() {
            console.warn('[cookie-login] debugKeyboard clicked - blur then focus')
            this.tryFocusInput()
            this.cookieStatus = '已尝试 blur+focus 调用，查看日志'
        },
        
        onCookieInput(val) {
            console.warn('[cookie-login] onCookieInput: ' + val)
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
        
        onFocus() {
            console.warn('[cookie-login] onFocus - input focused')
        },
        
        onBlur() {
            console.warn('[cookie-login] onBlur - input blurred')
        },
        
        goBack() {
            console.log('[cookie-login] goBack')
            try {
                this.$page.finish()
            } catch (e) {
                console.warn('[cookie-login] finish error: ' + (e ? e.message : e))
            }
        },
        
        onCookieInput(val) {
            console.warn('[cookie-login] onCookieInput: ' + val)
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
        
        onFocus() {
            console.warn('[cookie-login] onFocus - input focused')
        },
        
        onBlur() {
            console.warn('[cookie-login] onBlur - input blurred')
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