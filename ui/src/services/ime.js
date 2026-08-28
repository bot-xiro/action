// 系统输入法封装
// 设备已验证: /etc/miniapp/jsapis/libjsapi_export.so 导出 JSGlobalProxy,
// 包含 startTextEdit / closeTextEdit / clearTextEditContent /
// textEditFinished 信号, 系统输入法为系统 mini-app (appid 8001666679481944).
//
// 状态机 (来自 skill falcon-runtime.md):
// 1. 复用单例 Global 实例, 保存 textEditFinished handler
// 2. 打开前关闭旧 UUID, startTextEdit 同步返回 UUID
// 3. 回调先校验 UUID, 仅 editConfirmed === true 写回文本
// 4. 返回值兼容 string / {value} / {text}
// 5. destroy 时先 off handler, 再关闭残留会话
import globalModule from 'global'

let manager = null
function getManager() {
  if (!manager) {
    manager = new globalModule.Global()
  }
  return manager
}

function normalizeText(value) {
  if (value && typeof value === 'object') {
    if (typeof value.value === 'string') return value.value
    if (typeof value.text === 'string') return value.text
  }
  return typeof value === 'string' ? value : ''
}

/**
 * 创建一个输入法会话管理器.
 * 每个页面一个实例, 页面 onUnload 时必须调用 destroy().
 */
export function createIME() {
  let currentUuid = null
  let resolver = null
  let handler = null
  let destroyed = false

  function cleanup() {
    if (currentUuid !== null) {
      try { getManager().closeTextEdit(currentUuid) } catch (e) { console.log('closeTextEdit err', e) }
      currentUuid = null
    }
    resolver = null
  }

  return {
    /**
     * 打开系统输入法.
     * @param {Object} config { text, placeholder, maxlength }
     * @returns {Promise<string|null>} 确认返回文本, 取消返回 null
     */
    open(config) {
      return new Promise((resolve, reject) => {
        const g = getManager()
        // 关闭旧会话
        cleanup()

        handler = function (event) {
          let data = event
          if (typeof event === 'string') {
            try { data = JSON.parse(event) } catch (e) { data = null }
          }
          if (!data || typeof data !== 'object') return
          // 只处理当前 UUID 的回调
          if (currentUuid !== null && data.uuid && data.uuid !== currentUuid) return

          const confirmed = data.editConfirmed === true
          const text = normalizeText(data)
          const r = resolver
          cleanup()
          if (r) {
            if (confirmed) r(text)
            else r(null)
          }
        }
        g.textEditFinished.on(handler)

        resolver = function (v) {
          resolve(v)
        }

        const cfg = {
          text: config.text || '',
          placeholder: config.placeholder || '',
          maxlength: config.maxlength || 64,
          inputType: 'EnUSPreferred',
          autofocus: true,
          showCursor: true,
          cursorColor: '#fb7299',
          cursorSize: 2,
          multiLinesEditVisible: false,
          enterButtonText: '搜索'
        }
        try {
          // startTextEdit 同步返回 UUID
          currentUuid = g.startTextEdit(JSON.stringify(cfg))
        } catch (e) {
          cleanup()
          if (handler) { try { g.textEditFinished.off(handler) } catch (e2) {} }
          reject(e)
        }
      })
    },

    /** 关闭当前会话 (幂等) */
    close() {
      cleanup()
    },

    /** 页面销毁时调用: 先 off, 再关闭残留会话 */
    destroy() {
      if (destroyed) return
      destroyed = true
      if (handler) {
        try { getManager().textEditFinished.off(handler) } catch (e) {}
        handler = null
      }
      cleanup()
    }
  }
}
