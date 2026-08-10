/**
 * 搜索历史持久化（基于系统 storage JSAPI，异步）
 *
 * 【根因修复 2026-08-10】旧实现用 globalThis.$falcon.storage.get/set，
 * 该对象不存在 → 始终回退内存数组 → 应用关闭后历史丢失。
 * 系统正确 JSAPI：import storage from '$jsapi/storage'
 *   → storage.getStorage(key) / storage.setStorage(key, value)（异步 Promise）
 * 见 docs/STUDY_NOTES.md「关键 JSAPI」表与 refs/haasui-docs storage-kv.md。
 *
 * 对外接口保持同步风格调用（返回 Promise）：
 *   getHistory() → Promise<Array<string>>
 *   addHistory(kw) → Promise<Array<string>>（去重置顶，≤10 条）
 *   clearHistory() → Promise<void>
 * 所有方法内部兜底：设备 storage 异常时回退内存数组，不抛异常。
 */

import storage from '$jsapi/storage'

const KEY = 'bilibili_search_history'
const MAX_ITEMS = 10

// 内存兜底缓存（设备 storage 不可用时，页面生命周期内仍可用）
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

async function readRaw() {
    try {
        if (storage && typeof storage.getStorage === 'function') {
            const v = await storage.getStorage(KEY)
            if (v !== undefined && v !== null) {
                const arr = safeParse(v)
                _memory = arr.slice()
                return arr
            }
        }
        return _memory.slice()
    } catch (e) {
        // 设备 storage 读取失败 → 回退内存
        return _memory.slice()
    }
}

async function writeRaw(arr) {
    _memory = arr.slice()
    try {
        if (storage && typeof storage.setStorage === 'function') {
            await storage.setStorage(KEY, JSON.stringify(arr))
        }
    } catch (e) {
        // 写入失败仅保留内存，不抛异常
    }
}

function getHistory() {
    return readRaw()
}

/**
 * 添加一条历史（去重，置顶，超出 10 条截断）
 * @param {string} keyword
 * @returns {Promise<Array<string>>}
 */
function addHistory(keyword) {
    return readRaw().then(function (list) {
        if (!keyword || typeof keyword !== 'string') return []
        var kw = keyword.trim()
        if (!kw) return list || []
        const next = [kw]
        for (var i = 0; i < list.length && next.length < MAX_ITEMS; i++) {
            if (list[i] !== kw) next.push(list[i])
        }
        return writeRaw(next).then(function () {
            return next
        })
    })
}

function clearHistory() {
    return writeRaw([])
}

export default {
    getHistory: getHistory,
    addHistory: addHistory,
    clearHistory: clearHistory,
    MAX_ITEMS: MAX_ITEMS
}