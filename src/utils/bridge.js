/**
 * Bridge 统一访问层
 * 尝试获取宿主注入的 bridge 对象（包含 shell、video 等原生能力）
 * 若不可用则返回模拟实现（回退到应用内播放器）
 */

let _bridge = null
let _bridgeReady = false

/**
 * 尝试获取 bridge 对象
 * 在宿主环境（如 Lilo）中，bridge 会作为 'bridge' 模块注入
 */
function tryLoadBridge() {
    if (_bridgeReady) return _bridge
    _bridgeReady = true

    try {
        // 尝试导入宿主注入的 bridge 模块
        // 这是宿主环境（如 Lilo app）提供的原生桥接对象
        const mod = require('bridge')
        _bridge = mod.default || mod
        console.warn('[bridge] loaded from host:', Object.keys(_bridge || {}))
    } catch (e) {
        console.warn('[bridge] not available in current environment:', e.message)
        _bridge = null
    }
    return _bridge
}

/**
 * 获取 bridge 实例（懒加载）
 */
export function getBridge() {
    if (!_bridgeReady) {
        tryLoadBridge()
    }
    return _bridge
}

/**
 * 调用系统播放器播放视频
 * @param {string} path - 视频路径（本地文件路径或可直链访问的 URL）
 * @returns {Promise<boolean>} 是否成功调用
 */
export async function openSystemVideo(path) {
    const bridge = getBridge()
    if (!bridge || !bridge.shell || typeof bridge.shell.openSystemVideo !== 'function') {
        console.warn('[bridge] shell.openSystemVideo not available')
        return false
    }
    try {
        console.warn('[bridge] calling openSystemVideo:', path)
        // bridge 方法可能是 Promise 或回调风格，统一用 Promise 包装
        const result = await bridge.shell.openSystemVideo(path)
        console.warn('[bridge] openSystemVideo result:', result)
        return true
    } catch (e) {
        console.warn('[bridge] openSystemVideo error:', e.message)
        return false
    }
}

/**
 * 检查是否支持系统播放器
 */
export function isSystemPlayerSupported() {
    const bridge = getBridge()
    return !!(bridge && bridge.shell && typeof bridge.shell.openSystemVideo === 'function')
}

/**
 * 调用应用内播放器（VideoBridge）
 * 这是当前 gstplayer 模块的功能
 * @param {Object} params - { uri, audio, pos_x, pos_y, pos_w, pos_h, fill, loop }
 */
export function playInApp(params) {
    // 这里直接使用 gstplayer 模块，由调用方引入
    // 此函数仅为统一接口，实际实现由 player.vue 调用 gstPlayer.open()
    return params
}

export default {
    getBridge,
    openSystemVideo,
    isSystemPlayerSupported,
    playInApp
}