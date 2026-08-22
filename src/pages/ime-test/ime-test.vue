<template>
    <div class="page">
        <div class="topbar">
            <text class="back" @click="goBack">‹</text>
            <text class="title">输入法测试</text>
        </div>

        <scroller class="content" scroll-direction="vertical" :show-scrollbar="false">
            <div class="section">
                <text class="section-title">方案1: textarea + softInputEnable=true</text>
                <div class="input-box">
                    <textarea
                        ref="textarea1"
                        class="test-input"
                        v-model="input1"
                        placeholder="点击这里测试 textarea"
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
                <text class="section-title">方案4: 通过 $falcon.navTo 跳转系统输入法app</text>
                <div class="btn-row">
                    <text class="btn" @click="navToInputMethod">跳转有道输入法 (8001666679481944)</text>
                </div>
                <text class="log">{{ navResult }}</text>
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
            logs: []
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
            this.status1 = 'focused - 输入法应已弹出'
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

        // 方案4
        navToInputMethod() {
            var self = this
            try {
                $falcon.navTo('falcon://8001666679481944', {})
                this.navResult = 'navTo 调用成功，ret=0'
                this.addLog('navTo 有道输入法 (8001666679481944) 成功')
            } catch (e) {
                this.navResult = 'navTo 失败: ' + (e && e.message ? e.message : String(e))
                this.addLog('navTo 失败: ' + (e && e.message ? e.message : String(e)))
            }
        }
    }
}
</script>