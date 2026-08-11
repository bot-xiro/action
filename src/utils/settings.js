/**
 * 播放器设置持久化（基于系统 storage JSAPI，异步）
 *
 * 【2026-08-11】用户要求：设置界面可切换播放器（自研 gstplayer / 系统播放器），
 * 且本地保存。复用 storage.js 已验证的 storage JSAPI 模式：
 *   import storage from '$jsapi/storage'
 *   → storage.getStorage(key) / storage.setStorage(key, value)（异步 Promise）
 *
 * 模式定义：
 *   'gst'    = 自研 gstplayer（KMS 双平面，视频独立平面，控制栏悬浮受限）
 *   'system' = 系统播放器 CVPlayer（框架原生，视频在 UI 层内合成，控制栏悬浮）
 */

import storage from '$jsapi/storage'

const KEY = 'bilibili_player_mode'
const MODE_GST = 'gst'
const MODE_SYSTEM = 'system'
const DEFAULT_MODE = MODE_GST

// 内存兜底缓存（设备 storage 不可用时，页面生命周期内仍可用）
let _memory = DEFAULT_MODE

function isValid(mode) {
    return mode === MODE_GST || mode === MODE_SYSTEM
}

/**
 * 读取当前播放器模式 → Promise<'gst'|'system'>
 */
async function getMode() {
    try {
        if (storage && typeof storage.getStorage === 'function') {
            const v = await storage.getStorage(KEY)
            if (isValid(v)) {
                _memory = v
                return v
            }
        }
        return _memory
    } catch (e) {
        return _memory
    }
}

/**
 * 保存播放器模式（无效值回退默认）→ Promise<'gst'|'system'>
 */
async function setMode(mode) {
    if (!isValid(mode)) mode = DEFAULT_MODE
    _memory = mode
    try {
        if (storage && typeof storage.setStorage === 'function') {
            await storage.setStorage(KEY, mode)
        }
    } catch (e) {
        // 写入失败仅保留内存，不抛异常
    }
    return mode
}

export default {
    getMode: getMode,
    setMode: setMode,
    MODE_GST: MODE_GST,
    MODE_SYSTEM: MODE_SYSTEM,
    DEFAULT_MODE: DEFAULT_MODE
}
