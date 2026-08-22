<template>
    <div class="page">
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
                    
                    <!-- IP 输入框 - 点击显示自定义数字键盘 -->
                    <div class="input-wrapper" @click="showKeyboard">
                        <input class="cookie-input" 
                               :value="computerIp" 
                               placeholder="电脑 IP (如 192.168.1.100)" 
                               @input="onCookieInput" 
                               readonly 
                               style="height: 40px;"></input>
                        <text class="keyboard-hint">点击输入 IP 地址</text>
                    </div>
                    
                    <text class="cookie-status">{{ cookieStatus }}</text>
                    
                    <div class="cookie-btns">
                        <text class="cookie-btn cancel" @click="goBack">取消</text>
                        <text class="cookie-btn confirm" @click="fetchCookieFromComputer">从电脑获取</text>
                    </div>
                </div>
            </div>
        </scroller>

        <!-- 自定义数字键盘 -->
        <div v-if="showKeyboard" class="keyboard-overlay" @click="hideKeyboard">
            <div class="keyboard-panel" @click.stop>
                <div class="keyboard-header">
                    <text class="keyboard-title">输入电脑 IP 地址</text>
                    <text class="keyboard-close" @click="hideKeyboard">完成</text>
                </div>
                
                <div class="keyboard-display">
                    <input class="keyboard-input" :value="computerIp" readonly></input>
                </div>
                
                <div class="keyboard-keys">
                    <div class="keyboard-row">
                        <text class="key" @click="addDigit('1')">1</text>
                        <text class="key" @click="addDigit('2')">2</text>
                        <text class="key" @click="addDigit('3')">3</text>
                    </div>
                    <div class="keyboard-row">
                        <text class="key" @click="addDigit('4')">4</text>
                        <text class="key" @click="addDigit('5')">5</text>
                        <text class="key" @click="addDigit('6')">6</text>
                    </div>
                    <div class="keyboard-row">
                        <text class="key" @click="addDigit('7')">7</text>
                        <text class="key" @click="addDigit('8')">8</text>
                        <text class="key" @click="addDigit('9')">9</text>
                    </div>
                    <div class="keyboard-row">
                        <text class="key key-dot" @click="addDot">.</text>
                        <text class="key" @click="addDigit('0')">0</text>
                        <text class="key key-delete" @click="deleteLast">⌫</text>
                    </div>
                    <div class="keyboard-row">
                        <text class="key key-clear" @click="clearInput">清空</text>
                        <text class="key key-confirm" @click="confirmInput">确定</text>
                    </div>
                </div>
            </div>
        </div>
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
    margin-bottom: 12px;
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

.keyboard-hint {
    font-size: 12px;
    color: #999999;
    margin-top: 4px;
}

.cookie-status {
    font-size: 14px;
    color: #fb7299;
    margin-bottom: 16px;
    min-height: 20px;
}

.cookie-btns {
    flex-direction: row;
    justify-content: space-between;
    gap: 12px;
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

/* 自定义数字键盘样式 */
.keyboard-overlay {
    position: fixed;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    background-color: rgba(0, 0, 0, 0.5);
    z-index: 1000;
    justify-content: flex-end;
}

.keyboard-panel {
    width: 100%;
    background-color: #ffffff;
    border-top-left-radius: 16px;
    border-top-right-radius: 16px;
    flex-direction: column;
    padding-bottom: 20px;
}

.keyboard-header {
    flex-direction: row;
    justify-content: space-between;
    align-items: center;
    padding: 16px;
    border-bottom: 1px solid #eeeeee;
}

.keyboard-title {
    font-size: 18px;
    color: #333333;
    font-weight: bold;
}

.keyboard-close {
    font-size: 16px;
    color: #fb7299;
}

.keyboard-display {
    padding: 16px;
    background-color: #f5f6f7;
    border-bottom: 1px solid #eeeeee;
}

.keyboard-input {
    width: 100%;
    height: 40px;
    font-size: 18px;
    color: #333333;
    background-color: #ffffff;
    border: 1px solid #e0e0e0;
    border-radius: 8px;
    padding-left: 12px;
    padding-right: 12px;
}

.keyboard-keys {
    flex: 1;
    padding: 16px;
    flex-direction: column;
    justify-content: space-between;
}

.keyboard-row {
    flex-direction: row;
    justify-content: space-between;
    margin-bottom: 12px;
}

.keyboard-row:last-child {
    margin-bottom: 0;
}

.key {
    flex: 1;
    height: 56px;
    line-height: 56px;
    text-align: center;
    font-size: 24px;
    color: #333333;
    background-color: #fafafa;
    border: 1px solid #eeeeee;
    border-radius: 8px;
    margin-left: 8px;
    margin-right: 8px;
}

.key:first-child {
    margin-left: 0;
}

.key:last-child {
    margin-right: 0;
}

.key:active {
    background-color: #f0f0f0;
}

.key-dot {
    font-size: 28px;
}

.key-delete {
    color: #fb7299;
    font-size: 22px;
}

.key-clear {
    background-color: #fff0f0;
    color: #fb7299;
    font-size: 16px;
}

.key-confirm {
    background-color: #fb7299;
    color: #ffffff;
    font-size: 16px;
}
</style>

<script>
import auth from '../../utils/auth.js'

export default {
    name: 'cookie-login',
    data() {
        return {
            computerIp: '192.168.1.100',
            cookieStatus: '',
            showKeyboard: false
        }
    },
    mounted() {
        console.warn('[cookie-login] mounted')
    },
    methods: {
        goBack() {
            console.log('[cookie-login] goBack')
            if (this.showKeyboard) {
                this.hideKeyboard()
                return
            }
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
        
        // 显示自定义数字键盘
        showKeyboard() {
            this.showKeyboard = true
        },
        
        // 隐藏自定义数字键盘
        hideKeyboard() {
            this.showKeyboard = false
        },
        
        // 添加数字
        addDigit(digit) {
            if (this.computerIp.length >= 15) return // IP 最大长度限制
            this.computerIp += digit
            this.cookieStatus = ''
        },
        
        // 添加点号
        addDot() {
            if (this.computerIp.length >= 15) return
            // 防止连续输入点号
            if (this.computerIp.endsWith('.')) return
            this.computerIp += '.'
            this.cookieStatus = ''
        },
        
        // 删除最后一位
        deleteLast() {
            this.computerIp = this.computerIp.slice(0, -1)
            this.cookieStatus = ''
        },
        
        // 清空输入
        clearInput() {
            this.computerIp = ''
            this.cookieStatus = ''
        },
        
        // 确定输入
        confirmInput() {
            this.hideKeyboard()
        },
        
        onFocus() {
            // 兼容旧调用
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