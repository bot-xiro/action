<template>
    <!-- 自绘软键盘（词典笔无系统输入法，QuickJS 子集无输入组件 → 页内自绘）
         variant: 0=字母 1=数字+符号（通过 数字/字母 键切换）
         emits: input(char) / backspace / clear / variant / search -->
    <div class="keyboard">
        <!-- 第一行 -->
        <div class="krow">
            <div v-for="k in row1" :key="k" class="key" @click="onTap(k)">
                <text class="key-text">{{ currentCase(k) }}</text>
            </div>
        </div>
        <!-- 第二行 -->
        <div class="krow">
            <div class="key key-func" @click="onShift">
                <text class="key-text key-func-text">⇧</text>
            </div>
            <div v-for="k in row2" :key="k" class="key" @click="onTap(k)">
                <text class="key-text">{{ currentCase(k) }}</text>
            </div>
            <div class="key key-func" @click="onBackspace">
                <text class="key-text key-func-text">⌫</text>
            </div>
        </div>
        <!-- 第三行 -->
        <div class="krow">
            <div v-for="k in row3" :key="k" class="key" @click="onTap(k)">
                <text class="key-text">{{ currentCase(k) }}</text>
            </div>
        </div>
        <!-- 底部功能行：变体切换 + 空格 + 清空 + 搜索 -->
        <div class="krow">
            <div class="key key-func" @click="onVariant">
                <text class="key-text key-func-text">{{ variant === 0 ? '123' : 'ABC' }}</text>
            </div>
            <div class="key key-space" @click="onTap(' ')">
                <text class="key-text">空格</text>
            </div>
            <div class="key key-func" @click="onClear">
                <text class="key-text key-func-text">清除</text>
            </div>
            <div class="key key-search" @click="onSearch">
                <text class="key-text key-search-text">搜索</text>
            </div>
        </div>
    </div>
</template>

<style scoped>
.keyboard {
    flex-direction: column;
    background-color: #e2e2e2;
}

.krow {
    flex-direction: row;
    justify-content: space-between;
    margin-top: 5px;
    padding-left: 4px;
    padding-right: 4px;
}

.key {
    height: 36px;
    flex: 1;
    margin-left: 2px;
    margin-right: 2px;
    background-color: #ffffff;
    border-radius: 4px;
    align-items: center;
    justify-content: center;
}

.key-text {
    font-size: 18px;
    color: #333333;
    text-align: center;
}

.key-func {
    background-color: #c8c8c8;
}

.key-func-text {
    color: #444444;
    font-size: 16px;
}

.key-space {
    flex: 2;
}

.key-search {
    background-color: #fb7299;
}

.key-search-text {
    color: #ffffff;
    font-size: 18px;
}
</style>

<script>
export default {
    name: 'soft-keyboard',
    props: {
        variant: {
            type: Number,
            default: 0   // 0=字母 1=数字+符号
        }
    },
    data() {
        return {
            shift: false
        }
    },
    computed: {
        row1() {
            if (this.variant === 1) return ['1', '2', '3', '4', '5', '6', '7', '8', '9', '0']
            return ['q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p']
        },
        row2() {
            if (this.variant === 1) return ['-', '/', ':', ';', '(', ')', '$', '&', '@']
            return ['a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l']
        },
        row3() {
            if (this.variant === 1) return ['.', ',', '?', '!', "'", '"']
            return ['z', 'x', 'c', 'v', 'b', 'n', 'm']
        }
    },
    methods: {
        currentCase(k) {
            // 字母受 shift 控制；数字/符号原样
            var isLetter = /[a-zA-Z]/.test(k)
            if (!isLetter) return k
            return this.shift ? k.toUpperCase() : k.toLowerCase()
        },
        onTap(k) {
            this.$emit('input', k)
        },
        onShift() {
            this.shift = !this.shift
        },
        onBackspace() {
            this.$emit('backspace')
        },
        onVariant() {
            this.$emit('variant')
        },
        onClear() {
            this.$emit('clear')
        },
        onSearch() {
            this.$emit('search')
        }
    }
}
</script>