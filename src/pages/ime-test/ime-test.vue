<template>
    <div class="page">
        <div class="topbar">
            <text class="back" @click="goBack">‹</text>
            <text class="title">输入法测试</text>
        </div>

        <scroller class="content" scroll-direction="vertical" :show-scrollbar="false">
            <div class="section">
                <text class="section-title">方案1: textarea + softInputEnable=true (系统软键盘)</text>
                <div class="input-box">
                    <textarea
                        ref="textarea1"
                        class="test-input"
                        v-model="input1"
                        placeholder="点击这里测试系统软键盘"
                        :softInputEnable="true"
                        :single-line="true"
                        @input="onInput1"
                        @focus="onFocus1"
                        @blur="onBlur1"
                        @confirm="onConfirm1"
                        style="height: 48px;"
                    ></textarea>
                </div>
                <text class="log">输入: {{ input1 }}</text>
                <text class="log">状态: {{ status1 }}</text>
            </div>

            <div class="section">
                <text class="section-title">方案2: input 组件 (默认弹输入法)</text>
                <div class="input-box">
                    <input
                        ref="input2"
                        class="test-input"
                        v-model="input2"
                        placeholder="点击这里测试 input"
                        type="text"
                        @input="onInput2"
                        @focus="onFocus2"
                        @blur="onBlur2"
                        style="height: 48px;"
                    ></input>
                </div>
                <text class="log">输入: {{ input2 }}</text>
                <text class="log">状态: {{ status2 }}</text>
            </div>

            <div class="section">
                <text class="section-title">方案3: textarea + 手动 focus()</text>
                <div class="input-box">
                    <textarea
                        ref="textarea3"
                        class="test-input"
                        v-model="input3"
                        placeholder="点击按钮聚焦"
                        :softInputEnable="true"
                        :single-line="true"
                        @input="onInput3"
                        @focus="onFocus3"
                        @blur="onBlur3"
                        style="height: 48px;"
                    ></textarea>
                </div>
                <div class="btn-row">
                    <text class="btn" @click="focusTextarea3">调用 focus()</text>
                    <text class="btn" @click="blurTextarea3">调用 blur()</text>
                </div>
                <text class="log">输入: {{ input3 }}</text>
                <text class="log">状态: {{ status3 }}</text>
            </div>

            <div class="section">
                <text class="section-title">方案4: 调用有道输入法 App (8001666679481944)</text>
                <div class="btn-row">
                    <text class="btn" @click="openSystemIME">调用有道输入法</text>
                </div>
                <text class="log">navTo结果: {{ navResult }}</text>
            </div>

            <div class="section">
                <text class="section-title">方案5: 剪贴板监听 (输入法复制后自动填入)</text>
                <div class="btn-row">
                    <text class="btn" @click="startClipboardMonitor">开始监听剪贴板</text>
                    <text class="btn" @click="stopClipboardMonitor">停止监听</text>
                </div>
                <text class="log">剪贴板内容: {{ clipboardContent }}</text>
                <text class="log" style="color: #fb7299;">说明: 在输入法输入完成后点击"复制"，内容会自动填入下方</text>
            </div>

            <div class="section">
                <text class="section-title">方案6: 存储检查 (输入法完成后点击检查)</text>
                <div class="btn-row">
                    <text class="btn" @click="checkAllStorage">检查所有存储</text>
                    <text class="btn" @click="startPollingCheck">开始轮询检查</text>
                    <text class="btn" @click="stopPollingCheck">停止轮询</text>
                </div>
                <text class="log">轮询状态: {{ pollingStatus }}</text>
                <text class="log">检查到内容: {{ checkedContent }}</text>
            </div>

            <div class="section">
                <text class="section-title">回调事件监听</text>
                <div class="btn-row">
                    <text class="btn" @click="testEventListeners">测试事件监听</text>
                </div>
            </div>

            <div class="section">
                <text class="section-title">日志输出</text>
                <div class="log-box">
                    <text v-for="(log, i) in logs" :key="i" class="log-item">{{ log }}</text>
                </div>
                <text class="btn" @click="clearLogs">清空日志</text>
            </div>
        </scroller>
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
    padding-left: 16px;
    padding-right: 16px;
}

.back {
    font-size: 30px;
    color: #ffffff;
    padding-top: 2px;
    padding-right: 12px;
}

.title {
    font-size: 20px;
    color: #ffffff;
    font-weight: bold;
}

.content {
    flex: 1;
    padding: 16px;
    flex-direction: column;
}

.section {
    background-color: #ffffff;
    margin-bottom: 16px;
    padding: 16px;
    border-radius: 8px;
    flex-direction: column;
}

.section-title {
    font-size: 14px;
    color: #999999;
    margin-bottom: 12px;
    font-weight: bold;
}

.input-box {
    margin-bottom: 8px;
}

.test-input {
    width: 100%;
    font-size: 16px;
    color: #333333;
    background-color: #fafafa;
    border: 1px solid #e0e0e0;
    border-radius: 8px;
    padding-left: 12px;
    padding-right: 12px;
}

.btn-row {
    flex-direction: row;
    gap: 12px;
    margin-top: 8px;
}

.btn {
    flex: 1;
    height: 40px;
    line-height: 40px;
    text-align: center;
    background-color: #fb7299;
    color: #ffffff;
    border-radius: 6px;
    font-size: 14px;
}

.log {
    font-size: 12px;
    color: #666666;
    margin-top: 4px;
}

.log-box {
    max-height: 200px;
    background-color: #fafafa;
    border-radius: 4px;
    padding: 8px;
    margin-top: 8px;
}

.log-item {
    font-size: 11px;
    color: #333333;
    font-family: monospace;
    margin-bottom: 2px;
}
</style>

<script>
export default {
    name: 'ime-test',
    data() {
        return {
            input1: '',
            status1: '待测试',
            input2: '',
            status2: '待测试',
            input3: '',
            status3: '待测试',
            navResult: '',
            logs: [],
            clipboardContent: '',
            clipboardPolling: null,
            pollingCheck: null,
            pollingStatus: '未开始',
            checkedContent: ''
        }
    },
    mounted() {
        // 监听有道输入法回调事件 (反编译确认: confirm/finish/returnClicked/finishApp/search_keyInput_confirm)
        if (typeof $falcon !== 'undefined' && $falcon.on) {
            $falcon.on('confirm', this.onImeConfirm.bind(this))
            $falcon.on('finish', this.onImeFinish.bind(this))
            $falcon.on('returnClicked', this.onImeCancel.bind(this))
            $falcon.on('finishApp', this.onImeCancel.bind(this))
            $falcon.on('search_keyInput_confirm', this.onImeConfirm.bind(this))
            $falcon.on('confirmAndReturn', this.onImeConfirm.bind(this))
            $falcon.on('cancelAndReturn', this.onImeCancel.bind(this))
            $falcon.on('textEditFinished', this.onImeResult.bind(this))
            $falcon.on('imeResult', this.onImeResult.bind(this))
            $falcon.on('inputResult', this.onImeResult.bind(this))
        }
    },
    methods: {
        addLog(msg) {
            var time = new Date().toLocaleTimeString()
            this.logs.unshift('[' + time + '] ' + msg)
            if (this.logs.length > 50) this.logs.pop()
        },
        clearLogs() {
            this.logs = []
        },
        goBack() {
            this.$page.finish()
        },

        // 方案1
        onInput1(val) {
            this.input1 = val
            this.addLog('textarea1 input: ' + val)
        },
        onFocus1() {
            this.status1 = 'focused - 系统软键盘触发'
            this.addLog('textarea1 focus - 系统输入法触发')
        },
        onBlur1() {
            this.status1 = 'blurred - 输入法已隐藏'
            this.addLog('textarea1 blur')
        },
        onConfirm1() {
            this.status1 = 'confirm - 完成输入'
            this.addLog('textarea1 confirm')
        },

        // 方案2
        onInput2(val) {
            this.input2 = val
            this.addLog('input2 input: ' + val)
        },
        onFocus2() {
            this.status2 = 'focused - 输入法应已弹出'
            this.addLog('input2 focus')
        },
        onBlur2() {
            this.status2 = 'blurred - 输入法已隐藏'
            this.addLog('input2 blur')
        },

        // 方案3
        onInput3(val) {
            this.input3 = val
            this.addLog('textarea3 input: ' + val)
        },
        onFocus3() {
            this.status3 = 'focused'
            this.addLog('textarea3 focus')
        },
        onBlur3() {
            this.status3 = 'blurred'
            this.addLog('textarea3 blur')
        },
        focusTextarea3() {
            var ref = this.$refs.textarea3
            if (ref) {
                if (typeof ref.focus === 'function') {
                    ref.focus()
                    this.addLog('手动调用 textarea3.focus()')
                } else if (ref.$el && typeof ref.$el.focus === 'function') {
                    ref.$el.focus()
                    this.addLog('手动调用 textarea3.$el.focus()')
                } else {
                    this.addLog('textarea3 无 focus 方法')
                }
            } else {
                this.addLog('textarea3 ref 不存在')
            }
        },
        blurTextarea3() {
            var ref = this.$refs.textarea3
            if (ref && typeof ref.blur === 'function') {
                ref.blur()
                this.addLog('手动调用 textarea3.blur()')
            }
        },

        // 方案4: 调用有道输入法 App
        openSystemIME() {
            var self = this
            console.warn('[ime-test] >>> openSystemIME CLICKED <<<')
            this.addLog('[ime-test] >>> openSystemIME CLICKED <<<')
            
            // 尝试多种 callback URL 格式
            var callbackUrls = [
                'falcon://8001812345678901/ime-callback',
                'falcon://8001812345678901?callback=ime-callback',
                'falcon://8001812345678901/return',
                'falcon://8001812345678901/result'
            ]
            var paramsBase = {
                action: 'input',
                type: 'text',
                hint: '测试输入法',
                defaultText: '',
                maxLength: 30,
                confirmText: '确认',
                search_keyInput_confirm: true
            }
            var self = this
            var tryNextUrl = function(index) {
                if (index >= callbackUrls.length) {
                    self.addLog('[ime-test] all callback URLs tried, navTo failed')
                    return
                }
                var callbackUrl = callbackUrls[index]
                var params = Object.assign({}, paramsBase, {
                    callback: callbackUrl,
                    returnUrl: callbackUrl
                })
                self.addLog('[ime-test] navTo try url[' + index + ']: ' + callbackUrl)
                self.addLog('[ime-test] navTo params: ' + JSON.stringify(params))
                try {
                    var ret = $falcon.navTo('falcon://8001666679481944', params)
                    self.addLog('[ime-test] navTo ret[' + index + ']: ' + JSON.stringify(ret))
                    if (ret && ret.ret === 0) {
                        self.addLog('[ime-test] navTo success with url[' + index + ']')
                        return
                    } else {
                        self.addLog('[ime-test] navTo failed with url[' + index + ']: ' + JSON.stringify(ret))
                        tryNextUrl(index + 1)
                    }
                } catch (e) {
                    self.addLog('[ime-test] navTo error with url[' + index + ']: ' + (e && e.message ? e.message : String(e)))
                    tryNextUrl(index + 1)
                }
            }
            tryNextUrl(0)
        },

        // 处理 navTo 回调 - 页面被重新激活时调用
        onNewOptions(options) {
            this.addLog('[ime-test] onNewOptions received: ' + JSON.stringify(options))
            if (options && (options.text || options.value || options.result || options.content)) {
                var text = options.text || options.value || options.result || options.content
                this.addLog('[ime-test] onNewOptions got text: ' + text)
            }
        },
        // 兼容：onLoad 也可能接收回调参数
        onLoad(options) {
            this.addLog('[ime-test] onLoad received: ' + JSON.stringify(options))
            if (options && (options.text || options.value || options.result || options.content)) {
                var text = options.text || options.value || options.result || options.content
                this.addLog('[ime-test] onLoad got text: ' + text)
            }
        },

        // 有道输入法回调事件
        onImeConfirm(result) {
            this.addLog('[ime-test] onImeConfirm received: ' + JSON.stringify(result))
            if (result && (result.text || result.value || result.result || options.content)) {
                var text = result.text || result.value || result.result || options.content || ''
                if (text) {
                    this.addLog('[ime-test] onImeConfirm got text: ' + text)
                }
            }
        },
        onImeFinish(result) {
            this.addLog('[ime-test] onImeFinish received: ' + JSON.stringify(result))
            if (result && (result.text || result.value || result.result || result.content)) {
                var text = result.text || result.value || result.result || options.content || ''
                if (text) {
                    this.addLog('[ime-test] onImeFinish got text: ' + text)
                }
            }
        },
        onImeCancel(result) {
            this.addLog('[ime-test] onImeCancel received: ' + JSON.stringify(result))
        },
        onImeResult(result) {
            this.addLog('[ime-test] onImeResult received: ' + JSON.stringify(result))
            if (result && (result.text || result.value || result.content)) {
                var text = result.text || result.value || result.content
                if (text) {
                    this.addLog('[ime-test] onImeResult got text: ' + text)
                }
            }
        },

        // 方案3
        onInput3(val) {
            this.input3 = val
            this.addLog('textarea3 input: ' + val)
        },
        onFocus3() {
            this.status3 = 'focused'
            this.addLog('textarea3 focus')
        },
        onBlur3() {
            this.status3 = 'blurred'
            this.addLog('textarea3 blur')
        },
        focusTextarea3() {
            var ref = this.$refs.textarea3
            if (ref) {
                if (typeof ref.focus === 'function') {
                    ref.focus()
                    this.addLog('手动调用 textarea3.focus()')
                } else if (ref.$el && typeof ref.$el.focus === 'function') {
                    ref.$el.focus()
                    this.addLog('手动调用 textarea3.$el.focus()')
                } else {
                    this.addLog('textarea3 无 focus 方法')
                }
            } else {
                this.addLog('textarea3 ref 不存在')
            }
        },
        blurTextarea3() {
            var ref = this.$refs.textarea3
            if (ref && typeof ref.blur === 'function') {
                ref.blur()
                this.addLog('手动调用 textarea3.blur()')
            }
        },

        testEventListeners() {
            this.addLog('[ime-test] 测试事件监听已注册')
        },

        // 剪贴板监听方案
        startClipboardMonitor() {
            var self = this
            self.addLog('[ime-test] 开始监听剪贴板...')
            self.clipboardPolling = setInterval(function() {
                try {
                    // 尝试读取剪贴板内容
                    if (typeof $falcon !== 'undefined' && $falcon.getClipboardData) {
                        $falcon.getClipboardData().then(function(data) {
                            if (data && data !== self.clipboardContent) {
                                self.clipboardContent = data
                                self.addLog('[ime-test] 剪贴板变化: ' + data.substring(0, 50))
                                // 自动填入到方案1的输入框
                                self.input1 = data
                            }
                        }).catch(function(e) {
                            self.addLog('[ime-test] 读取剪贴板失败: ' + (e && e.message ? e.message : String(e)))
                        })
                    }
                } catch(e) {
                    // ignore errors in setInterval
                }
            }.bind(self), 1000)
            self.addLog('[ime-test] 剪贴板监听已启动 (每秒检查)')
        },
        stopClipboardMonitor() {
            if (this.clipboardPolling) {
                clearInterval(this.clipboardPolling)
                this.clipboardPolling = null
                this.addLog('[ime-test] 剪贴板监听已停止')
            }
        },

        // 存储检查方案
        checkAllStorage() {
            var self = this
            self.addLog('[ime-test] 开始检查所有存储...')
            var found = false
            
            // 检查 localStorage
            try {
                var keys = Object.keys(localStorage)
                for (var i = 0; i < keys.length; i++) {
                    var key = keys[i]
                    var val = localStorage.getItem(key)
                    if (val && val.length > 0) {
                        self.addLog('[ime-test] localStorage[' + key + ']: ' + val.substring(0, 50))
                        if (!self.checkedContent) self.checkedContent = val
                        found = true
                    }
                }
            } catch(e) {
                self.addLog('[ime-test] localStorage 检查失败: ' + (e && e.message ? e.message : String(e)))
            }

            // 检查 sessionStorage
            try {
                var keys = Object.keys(sessionStorage)
                for (var i = 0; i < keys.length; i++) {
                    var key = keys[i]
                    var val = sessionStorage.getItem(key)
                    if (val && val.length > 0) {
                        self.addLog('[ime-test] sessionStorage[' + key + ']: ' + val.substring(0, 50))
                        if (!self.checkedContent) self.checkedContent = val
                        found = true
                    }
                }
            } catch(e) {
                self.addLog('[ime-test] sessionStorage 检查失败: ' + (e && e.message ? e.message : String(e)))
            }

            // 检查 cookie
            try {
                var cookies = document.cookie.split(';')
                for (var i = 0; i < cookies.length; i++) {
                    var cookie = cookies[i].trim()
                    if (cookie.length > 0) {
                        self.addLog('[ime-test] cookie: ' + cookie.substring(0, 50))
                        found = true
                    }
                }
            } catch(e) {
                self.addLog('[ime-test] cookie 检查失败: ' + (e && e.message ? e.message : String(e)))
            }

            if (!found) {
                self.addLog('[ime-test] 未在任何存储中发现新内容')
            } else {
                self.checkedContent = '发现内容，请查看日志'
            }
        },

        startPollingCheck() {
            var self = this
            if (this.pollingCheck) {
                self.addLog('[ime-test] 轮询已在进行中')
                return
            }
            self.pollingStatus = '轮询中...'
            self.addLog('[ime-test] 开始轮询检查 (每2秒)...')
            this.pollingCheck = setInterval(function() {
                try {
                    // 检查 localStorage 变化
                    var keys = Object.keys(localStorage)
                    for (var i = 0; i < keys.length; i++) {
                        var key = keys[i]
                        var val = localStorage.getItem(key)
                        if (val && val.length > 0 && val.length > 2) {
                            self.addLog('[ime-test] 轮询发现 localStorage[' + key + ']: ' + val.substring(0, 50))
                            self.checkedContent = val
                            self.stopPollingCheck()
                            return
                        }
                    }
                    // 检查 sessionStorage
                    var sKeys = Object.keys(sessionStorage)
                    for (var i = 0; i < sKeys.length; i++) {
                        var key = sKeys[i]
                        var val = sessionStorage.getItem(key)
                        if (val && val.length > 0 && val.length > 2) {
                            self.addLog('[ime-test] 轮询发现 sessionStorage[' + key + ']: ' + val.substring(0, 50))
                            self.checkedContent = val
                            self.stopPollingCheck()
                            return
                        }
                    }
                } catch(e) {
                    // ignore errors
                }
            }.bind(this), 2000)
            this.pollingStatus = '轮询中... (每2秒)'
            this.addLog('[ime-test] 轮询检查已启动 (每2秒)')
        },
        stopPollingCheck() {
            if (this.pollingCheck) {
                clearInterval(this.pollingCheck)
                this.pollingCheck = null
                this.pollingStatus = '已停止'
                this.addLog('[ime-test] 轮询检查已停止')
            }
        },

        addLog(msg) {
            var time = new Date().toLocaleTimeString()
            this.logs.unshift('[' + time + '] ' + msg)
            if (this.logs.length > 50) this.logs.pop()
        },
        clearLogs() {
            this.logs = []
        },
        goBack() {
            this.$page.finish()
        }
    },
    beforeDestroy() {
        // 清理剪贴板监听
        if (this.clipboardPolling) {
            clearInterval(this.clipboardPolling)
        }
        // 清理输入法回调事件监听
        if (typeof $falcon !== 'undefined' && $falcon.off) {
            $falcon.off('confirm', this.onImeConfirm)
            $falcon.off('finish', this.onImeFinish)
            $falcon.off('returnClicked', this.onImeCancel)
            $falcon.off('finishApp', this.onImeCancel)
            $falcon.off('search_keyInput_confirm', this.onImeConfirm)
            $falcon.off('confirmAndReturn', this.onImeConfirm)
            $falcon.off('cancelAndReturn', this.onImeCancel)
            $falcon.off('textEditFinished', this.onImeResult)
            $falcon.off('imeResult', this.onImeResult)
            $falcon.off('inputResult', this.onImeResult)
        }
    }
}
</script>