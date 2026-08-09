/**
 * 简易持久化封装：搜索历史
 * - 优先用 globalThis.$falcon.storage.set/get（设备如提供则真正持久化）；
 * - 设备未提供时回退到模块内变量（页面生命周期内有效）；
 * - 不抛异常，失败返回空数组。
 */

const KEY = 'bilibili_search_history'
const MAX_ITEMS = 10

let _memory = []

function safeParse(raw) {
    if (!raw) return []
    try {
        var arr = JSON.parse(raw)
        if (Array.isArray(arr)) return arr
        return []
    } catch (e) {
        return []
    }
}

function readRaw() {
    var s = globalThis.$falcon && globalThis.$falcon.storage
    if (s && typeof s.get === 'function') {
        try {
            var v = s.get(KEY)
            return safeParse(v)
        } catch (e) {
            // 设备 storage 不可用时回退内存
            return _memory.slice()
        }
    }
    return _memory.slice()
}

function writeRaw(arr) {
    var raw = JSON.stringify(arr)
    var s = globalThis.$falcon && globalThis.$falcon.storage
    if (s && typeof s.set === 'function') {
        try {
            s.set(KEY, raw)
            _memory = arr.slice()
            return
        } catch (e) {
            _memory = arr.slice()
            return
        }
    }
    _memory = arr.slice()
}

function getHistory() {
    return readRaw()
}

/**
 * 添加一条历史（去重，置顶，超出 10 条截断）
 * @param {string} keyword
 */
function addHistory(keyword) {
    if (!keyword || typeof keyword !== 'string') return []
    var kw = keyword.trim()
    if (!kw) return []
    var list = readRaw()
    var next = [kw]
    for (var i = 0; i < list.length && next.length < MAX_ITEMS; i++) {
        if (list[i] !== kw) next.push(list[i])
    }
    writeRaw(next)
    return next
}

function clearHistory() {
    writeRaw([])
}

export default {
    getHistory: getHistory,
    addHistory: addHistory,
    clearHistory: clearHistory,
    MAX_ITEMS: MAX_ITEMS
}
