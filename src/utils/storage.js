/**
 * 本地存储工具（基于 localStorage，兼容小程序环境）
 * 设置项持久化存储
 */

const STORAGE_KEY = 'bili_settings'

// 默认设置
const DEFAULT_SETTINGS = {
    // 是否使用系统播放器（false = 应用内 GStreamer 播放器，true = 系统默认播放器）
    useSystemPlayer: false
}

/**
 * 获取所有设置
 */
export function getSettings() {
    try {
        const data = localStorage.getItem(STORAGE_KEY)
        if (data) {
            return { ...DEFAULT_SETTINGS, ...JSON.parse(data) }
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
        localStorage.setItem(STORAGE_KEY, JSON.stringify(updated))
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
    return getSettings()[key]
}

/**
 * 设置单个设置项
 */
export function setSetting(key, value) {
    return saveSettings({ [key]: value })
}

/**
 * 重置为默认设置
 */
export function resetSettings() {
    try {
        localStorage.removeItem(STORAGE_KEY)
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