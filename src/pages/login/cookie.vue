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
                    
                    <!-- 写法1: textarea single-line + softInputEnable="true" + autofocus="true" -->
                    <div class="input-wrapper">
                        <text class="input-label">方式1: textarea (single-line)</text>
                        <textarea 
                            ref="textareaRef"
                            class="cookie-input" 
                            v-model="computerIp" 
                            placeholder="电脑 IP (如 192.168.1.100)" 
                            @input="onCookieInput"
                            @focus="onFocus"
                            @blur="onBlur"
                            :autofocus="true"
                            :softInputEnable="true"
                            :single-line="true"
                            style="height: 40px;">
                        </textarea>
                    </div>
                    
                    <!-- 写法2: input type=text + softInputEnable="true" + autofocus="true" -->
                    <div class="input-wrapper" style="margin-top: 16px;">
                        <text class="input-label">方式2: input type=text</text>
                        <input 
                            ref="inputRef"
                            class="cookie-input" 
                            v-model="computerIp" 
                            placeholder="电脑 IP (如 192.168.1.100)" 
                            @input="onCookieInput"
                            @focus="onFocus"
                            @blur="onBlur"
                            :autofocus="true"
                            :softInputEnable="true"
                            type="text"
                            style="height: 40px;">
                        </input>
                    </div>
                    
                    <!-- 写法3: textarea 多行 + softInputEnable="true" + autofocus="true" -->
                    <div class="input-wrapper" style="margin-top: 16px;">
                        <text class="input-label">方式3: textarea 多行</text>
                        <textarea 
                            ref="textareaRef2"
                            class="cookie-input" 
                            v-model="computerIp" 
                            placeholder="电脑 IP (如 192.168.1.100)" 
                            @input="onCookieInput"
                            @focus="onFocus"
                            @blur="onBlur"
                            :autofocus="true"
                            :softInputEnable="true"
                            style="height: 60px;">
                        </textarea>
                    </div>
                    
                    <text class="cookie-status">{{ cookieStatus }}</text>
                    
                    <div class="cookie-btns">
                        <text class="cookie-btn cancel" @click="goBack">取消</text>
                        <text class="cookie-btn confirm" @click="fetchCookieFromComputer">从电脑获取</text>
                    </div>
                    
                    <!-- 测试按钮 -->
                    <div class="test-btns">
                        <text class="test-btn" @click="testKeyboardAPI">测试键盘 API</text>
                        <text class="test-btn" @click="forceFocusAll">强制聚焦所有输入框</text>
                        <text class="test-btn" @click="testModalInput">测试 modal.input</text>
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
    margin-bottom: 16px;
}

.input-label {
    font-size: 12px;
    color: #999999;
    margin-bottom: 4px;
}

.cookie-input {
    width: 100%;
    height: 40px;
    font-size: 14px;
    color: #333333;
    background-color: #fafafa;
    border: 1px solid #e0e0e0;
    border-radius: 6px;
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
    height: 44px;
    line-height: 44px;
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

.test-btns {
    margin-top: 20px;
    flex-direction: column;
    gap: 8px;
}

.test-btn {
    height: 40px;
    line-height: 40px;
    text-align: center;
    font-size: 14px;
    color: #ffffff;
    background-color: #999999;
    border-radius: 6px;
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
        // 页面加载后尝试聚焦第一个输入框
        this.$nextTick(function () {
            this.tryFocusInputs()
        })
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
        
        // 页面加载时尝试聚焦
        tryFocusInputs() {
            console.warn('[cookie-login] trying to focus first input...')
            // 只聚焦第一个输入框，避免焦点快速切换导致键盘隐藏
            var ref = this.$refs.textareaRef
            if (ref) {
                console.warn('[cookie-login] found textareaRef, trying to focus...')
                try {
                    if (typeof ref.focus === 'function') {
                        ref.focus()
                        console.warn('[cookie-login] called focus() on textareaRef')
                    }
                } catch (e) {
                    console.warn('[cookie-login] focus error on textareaRef: ' + e)
                }
            } else {
                console.warn('[cookie-login] textareaRef not found')
            }
        },
        
        // 强制聚焦第一个输入框
        forceFocusAll() {
            console.warn('[cookie-login] forceFocusAll clicked')
            this.tryFocusInputs()
            this.cookieStatus = '已尝试强制聚焦第一个输入框，查看日志'
        },
        
        // 测试键盘 API
        testKeyboardAPI() {
            console.warn('[cookie-login] testKeyboardAPI clicked')
            this.cookieStatus = '正在测试键盘 API...'
            try {
                var jsapi = $falcon.jsapi
                console.warn('[cookie-login] === $falcon.jsapi keys: ' + Object.keys(jsapi || {}).join(', '))
                
                // 列出所有可能的 API 路径
                var paths = [
                    'jsapi.ime',
                    'jsapi.input', 
                    'jsapi.softInput',
                    'jsapi.softKeyboard',
                    'jsapi.keyboard',
                    'jsapi.system',
                    'jsapi.window',
                    'jsapi.textInput',
                    'jsapi.editText',
                    'jsapi.nativeInput',
                ]
                
                for (var i = 0; i < paths.length; i++) {
                    var path = paths[i]
                    var obj = this.getNested(jsapi, path)
                    if (obj) {
                        console.warn('[cookie-login] FOUND: ' + path + ' = ' + JSON.stringify(Object.keys(obj)))
                    } else {
                        console.warn('[cookie-login] NOT FOUND: ' + path)
                    }
                }
                
                // 尝试 modal 相关
                if (jsapi.modal) {
                    console.warn('[cookie-login] modal keys: ' + Object.keys(jsapi.modal))
                    if (jsapi.modal.input) {
                        console.warn('[cookie-login] modal.input FOUND!')
                        try {
                            jsapi.modal.input({
                                title: '输入 IP',
                                placeholder: '192.168.1.100',
                                confirmText: '确定',
                                cancelText: '取消'
                            }).then(function(res) {
                                console.warn('[cookie-login] modal.input result: ' + JSON.stringify(res))
                            })
                        } catch (e) {
                            console.warn('[cookie-login] modal.input error: ' + e)
                        }
                    }
                    if (jsapi.modal.prompt) {
                        console.warn('[cookie-login] modal.prompt FOUND!')
                    }
                }
                
                this.cookieStatus = '已测试，查看日志输出'
            } catch (e) {
                console.warn('[cookie-login] testKeyboardAPI error: ' + e)
                this.cookieStatus = '测试出错: ' + e
            }
        },
        
        // 获取嵌套对象属性
        getNested(obj, path) {
            if (!obj) return null
            var parts = path.split('.')
            var current = obj
            for (var i = 1; i < parts.length; i++) { // 跳过第一个 'jsapi'
                if (current && current[parts[i]]) {
                    current = current[parts[i]]
                } else {
                    return null
                }
            }
            return current
        },
        
        // 测试 modal.input
        testModalInput() {
            console.warn('[cookie-login] testModalInput clicked')
            try {
                var jsapi = $falcon.jsapi
                if (jsapi && jsapi.modal && jsapi.modal.input) {
                    jsapi.modal.input({
                        title: '输入电脑 IP',
                        placeholder: '192.168.1.100',
                        confirmText: '确定',
                        cancelText: '取消'
                    }).then(function(res) {
                        console.warn('[cookie-login] modal.input result: ' + JSON.stringify(res))
                        if (res && res.confirm && res.value) {
                            this.computerIp = res.value
                            this.cookieStatus = '通过 modal.input 输入: ' + res.value
                        }
                    }.bind(this))
                } else {
                    this.cookieStatus = 'modal.input 不存在'
                    console.warn('[cookie-login] modal.input not available')
                }
            } catch (e) {
                console.warn('[cookie-login] testModalInput error: ' + e)
                this.cookieStatus = '测试出错: ' + e
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
        
        onFocus() {
            console.warn('[cookie-login] input focused')
        },
        
        onBlur() {
            console.warn('[cookie-login] input blurred')
        },
        
        onScroll(e) {
        },
        
        onLoadMore() {
        }
    }
}
</script>