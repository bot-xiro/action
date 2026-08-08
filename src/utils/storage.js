/**
 * 本地存储工具（基于 localStorage，兼容小程序环境）
 * 设置项持久化存储
 * 若 localStorage 不可用，退回到内存存储
 */

const STORAGE_KEY = 'bili_settings'

// 默认设置
const DEFAULT_SETTINGS = {
    // 是否使用系统播放器（false = 应用内 GStreamer 播放器，true = 系统默认播放器）
    useSystemPlayer: false
}

// 内存降级存储
let _memoryStorage = {}

/**
 * 检测 localStorage 是否可用（安全检测，避免 ReferenceError）
 */
function isLocalStorageAvailable() {
    try {
        // 先检查全局对象是否有 localStorage 属性
        if (typeof globalThis === 'undefined' || !globalThis.hasOwnProperty('localStorage')) {
            return false
        }
        const test = '__storage_test__'
        globalThis.localStorage.setItem(test, test)
        globalThis.localStorage.removeItem(test)
        return true
    } catch (e) {
        return false
    }
}

const _hasLocalStorage = isLocalStorageAvailable()
console.warn('[storage] localStorage available:', _hasLocalStorage)

/**
 * 获取所有设置
 */
export function getSettings() {
    try {
        if (_hasLocalStorage) {
            const data = globalThis.localStorage.getItem(STORAGE_KEY)
            if (data) {
                const parsed = { ...DEFAULT_SETTINGS, ...JSON.parse(data) }
                console.warn('[storage] getSettings from localStorage:', parsed)
                return parsed
            }
        } else {
            const parsed = { ...DEFAULT_SETTINGS, ..._memoryStorage }
            console.warn('[storage] getSettings from memory:', parsed)
            return parsed
        }
    } catch (e) {
        console.warn('[storage] getSettings error:', e.message)
    }
    return { ...DEFAULT_SETTINGS }
}

/**
 * 保存设置（合并）
 */
export function saveSettings(partial) {
    try {
        const current = getSettings()
        const updated = { ...current, ...partial }
        if (_hasLocalStorage) {
            globalThis.localStorage.setItem(STORAGE_KEY, JSON.stringify(updated))
            console.warn('[storage] saveSettings to localStorage:', updated)
        } else {
            _memoryStorage = updated
            console.warn('[storage] saveSettings to memory:', updated)
        }
        return updated
    } catch (e) {
        console.warn('[storage] saveSettings error:', e.message)
        return getSettings()
    }
}

/**
 * 获取单个设置项
 */
export function getSetting(key) {
    const val = getSettings()[key]
    console.warn('[storage] getSetting', key, ':', val)
    return val
}

/**
 * 设置单个设置项
 */
export function setSetting(key, value) {
    console.warn('[storage] setSetting', key, ':', value)
    return saveSettings({ [key]: value })
}

/**
 * 重置为默认设置
 */
export function resetSettings() {
    try {
        if (_hasLocalStorage) {
            globalThis.localStorage.removeItem(STORAGE_KEY)
        } else {
            _memoryStorage = {}
        }
        console.warn('[storage] resetSettings')
    } catch (e) {
        console.warn('[storage] resetSettings error:', e.message)
    }
    return { ...DEFAULT_SETTINGS }
}

// 默认导出，兼容 import storage from '...'
const storage = {
    getSettings,
    saveSettings,
    getSetting,
    setSetting,
    resetSettings
}
export default storage