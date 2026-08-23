<template>
    <div class="page">
        <div class="topbar">
            <text class="back" @click="goBack">‹</text>
            <text class="title">系统键盘测试 (global.startTextEdit)</text>
        </div>

        <scroller class="content" scroll-direction="vertical" :show-scrollbar="false">
            <div class="section">
                <text class="section-title">正确用法: global.startTextEdit() (官方协议)</text>
                <div class="btn-row">
                    <text class="btn" @click="openKeyboard">打开系统键盘 (startTextEdit)</text>
                </div>
                <text class="log">状态: {{ status }}</text>
                <text class="log">输入结果: {{ result }}</text>
                <text class="log">UUID: {{ uuid }}</text>
            </div>

            <div class="section">
                <text class="section-title">配置选项</text>
                <div class="input-box">
                    <textarea
                        ref="placeholderInput"
                        class="test-input"
                        v-model="config.placeholder"
                        placeholder="占位符文本"
                        :single-line="true"
                        style="height: 48px;"
                    ></textarea>
                </div>
                <div class="input-box">
                    <textarea
                        ref="defaultTextInput"
                        class="test-input"
                        v-model="config.defaultText"
                        placeholder="默认文本"
                        :single-line="true"
                        style="height: 48px;"
                    ></textarea>
                </div>
                <div class="btn-row">
                    <text class="btn" @click="setInputType('ZhCNPreferred')">中文输入</text>
                    <text class="btn" @click="setInputType('EnUSPreferred')">英文输入</text>
                </div>
            </div>

            <div class="section">
                <text class="section-title">进阶测试</text>
                <div class="btn-row">
                    <text class="btn" @click="testWithDefaultText">带默认文本打开</text>
                    <text class="btn" @click="closeKeyboard">关闭键盘</text>
                </div>
                <div class="btn-row">
                    <text class="btn" @click="testZhCN">中文输入</text>
                    <text class="btn" @click="testEnUS">英文输入</text>
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
    font-weight: bold.
}

.content {
    flex: 1;
    padding: 16px;
    flex-direction: column.
}

.section {
    background-color: #ffffff.
    margin-bottom: 16px.
    padding: 16px.
    border-radius: 8px.
    flex-direction: column.
}

.section-title {
    font-size: 14px.
    color: #999999.
    margin-bottom: 12px.
    font-weight: bold.
}

.input-box {
    margin-bottom: 8px.
}

.test-input {
    width: 100%.
    font-size: 16px.
    color: #333333.
    background-color: #fafafa.
    border: 1px solid #e0e0e0.
    border-radius: 8px.
    padding-left: 12px.
    padding-right: 12px.
}

.btn-row {
    flex-direction: row.
    gap: 12px.
    margin-top: 8px.
}

.btn {
    flex: 1.
    height: 40px.
    line-height: 40px.
    text-align: center.
    background-color: #fb7299.
    color: #ffffff.
    border-radius: 6px.
    font-size: 14px.
}

.log {
    font-size: 12px.
    color: #666666.
    margin-top: 4px.
}

.log-box {
    max-height: 200px.
    background-color: #fafafa.
    border-radius: 4px.
    padding: 8px.
    margin-top: 8px.
}

.log-item {
    font-size: 11px.
    color: #333333.
    font-family: monospace.
    margin-bottom: 2px.
}
</style>

<script>
export default {
    name: 'system-keyboard-test',
    data() {
        return {
            status: '待测试',
            result: '',
            uuid: '',
            logs: [],
            config: {
                placeholder: '请输入内容',
                defaultText: '',
                confirmText: '确定',
                inputType: 'ZhCNPreferred',
                micInputVisible: false,
                autofocus: true,
                showCursor: true,
                cursorColor: '#28C7B2',
                cursorSize: 2,
                cursorIndex: 0,
                enterButtonText: '确定',
                micInputVisible: false
            }
        }
    },
    mounted() {
        this.addLog('[system-keyboard] 页面加载，监听 textEditFinished 事件')
        if (typeof $falcon !== 'undefined' && $falcon.on) {
            $falcon.on('textEditFinished', this.onTextEditFinished.bind(this))
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

        // 核心方法：使用 global.startTextEdit() 打开系统键盘
        openKeyboard() {
            var self = this
            self.addLog('[keyboard] >>> 调用 global.startTextEdit() <<<')
            
            // 必须传字符串，不能传对象 (文档强调: 传对象会报 contents should be string)
            var cfg = JSON.stringify({
                text: self.config.defaultText || '',
                placeholder: self.config.placeholder,
                placeholderColor: '#878A99',
                autofocus: self.config.autofocus,
                showCursor: self.config.showCursor,
                cursorColor: self.config.cursorColor,
                cursorSize: self.config.cursorSize,
                cursorIndex: self.config.cursorIndex,
                enterButtonText: self.config.enterButtonText,
                inputType: self.config.inputType,
                micInputVisible: self.config.micInputVisible,
                confirmButtonDisabledOnTextEmpty: false,
                shouldCloseOnConfirm: true,
                closeButtonVisible: true,
                returnButtonVisible: true,
                capsLockSwitchOn: false,
                confirmButtonDisabledOnTextEmpty: false,
                supportTopPanel: false,
                micInputVisible: false,
                hint: '',
                defaultText: self.config.defaultText,
                text: self.config.defaultText,
                confirmText: '确定',
                maxLength: 30,
                action: 'input',
                type: 'text',
                hint: '请输入内容',
                search_keyInput_confirm: true
            })
            
            self.addLog('[keyboard] startTextEdit cfg: ' + cfg)
            
            try {
                // 核心调用：global.startTextEdit() 必须传字符串
                var uuid = global.startTextEdit(cfg)
                self.uuid = uuid
                self.addLog('[keyboard] startTextEdit returned uuid: ' + uuid)
                self.status = '键盘已打开，UUID: ' + uuid
            } catch (e) {
                self.addLog('[keyboard] startTextEdit error: ' + (e && e.message ? e.message : String(e)))
            }
        },

        // 配置预设
        setInputType(type) {
            this.config.inputType = type
            self.addLog('[config] inputType 设置为: ' + type)
        },
        
        testWithDefaultText() {
            this.config.defaultText = '默认测试文本'
            this.openKeyboard()
        },
        
        testZhCN() {
            self.config.inputType = 'ZhCNPreferred'
            self.openKeyboard()
        },
        
        testEnUS() {
            self.config.inputType = 'EnUSPreferred'
            self.openKeyboard()
        },

        closeKeyboard() {
            if (this.uuid) {
                self.addLog('[keyboard] 调用 closeTextEdit, uuid: ' + self.uuid)
                try {
                    global.closeTextEdit(self.uuid)
                    self.addLog('[keyboard] closeTextEdit called')
                } catch (e) {
                    self.addLog('[keyboard] closeTextEdit error: ' + (e && e.message ? e.message : String(e)))
                }
                self.uuid = ''
                self.status = '键盘已关闭'
            } else {
                self.addLog('[keyboard] 无有效 UUID，无法关闭')
            }
        },

        // 处理键盘回调 - 核心回调处理
        onTextEditFinished(result) {
            self.addLog('[keyboard] onTextEditFinished received: ' + JSON.stringify(result))
            
            // 关键：必须按 uuid 过滤 (文档强调: 必须按 uuid 过滤)
            // 这里假设回调会带上 uuid，或者我们用最后一次的 uuid
            if (result && result.uuid && result.uuid !== self.uuid) {
                self.addLog('[keyboard] 忽略他人 uuid: ' + result.uuid + ' (期望: ' + self.uuid + ')')
                return
            }
            
            var text = ''
            var confirmed = false
            
            if (result) {
                // 兼容多种字段名
                text = result.text || result.value || result.result || result.content || ''
                confirmed = result.editConfirmed === true || result.confirm === true
            }
            
            self.addLog('[keyboard] 解析结果: text=' + text + ', confirmed=' + confirmed)
            
            if (confirmed && text) {
                self.result = text
                self.addLog('[keyboard] 确认输入: ' + text)
            } else if (!confirmed) {
                self.addLog('[keyboard] 用户取消输入')
            }
            
            self.uuid = ''
            self.status = '键盘已关闭'
        },

        clearLogs() {
            self.logs = []
        },
        
        goBack() {
            self.$page.finish()
        }
    }
}
</script>