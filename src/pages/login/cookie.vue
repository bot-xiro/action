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
                    
                    <input class="cookie-input" v-model="computerIp" placeholder="电脑 IP (如 192.168.1.100)" @input="onCookieInput" @focus="onFocus" @click="onFocus" :autofocus="true" :softInputEnable="true" type="text" style="height: 40px;"></input>
                    
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
    margin-bottom: 12px;
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
        
        onCookieInput() {
            this.cookieStatus = ''
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
            // 输入框获得焦点时的处理（HAAS UI 的 input/textarea 组件内部应自动处理软键盘）
            console.warn('[cookie-login] input focused')
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