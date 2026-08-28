// 系统输入法封装 v2
// 设备确认: Global 模块含 startTextEdit/closeTextEdit, 用户实测键盘能弹出.
// 回调通道存在两种可能 (历史探索证据):
//   1. Global 实例信号: manager.textEditFinished.on(handler)
//   2. 全局事件: $falcon.on('textEditFinished', handler), 事件包装 {type, timestamp, data}
// 两种都订阅. 回调数据兼容: 字符串 / {data} 包装 / {value|text|content|result} /
// 确认标志 editConfirmed|confirm|confirmed|ok
import globalModule from 'global'

let manager = null
function getManager() {
  if (!manager) {
    manager = new globalModule.Global()
  }
  return manager
}

function asObject(payload) {
  let d = payload
  // FalconEvent 包装
  if (d && typeof d === 'object' && (d.type === 'textEditFinished' || (d.data !== undefined && d.timestamp !== undefined))) {
    d = d.data
  }
  if (typeof d === 'string') {
    try { d = JSON.parse(d) } catch (e) { return { __raw: d } }
  }
  return d
}

function extractText(d) {
  if (typeof d === 'string') return d
  if (!d || typeof d !== 'object') return ''
  const keys = ['value', 'text', 'content', 'result', 'inputText']
  for (let i = 0; i < keys.length; i++) {
    if (typeof d[keys[i]] === 'string') return d[keys[i]]
  }
  if (typeof d.__raw === 'string') return d.__raw
  return ''
}

function extractConfirmed(d) {
  if (!d || typeof d !== 'object') return null
  const trueKeys = ['editConfirmed', 'confirm', 'confirmed', 'ok', 'confirmedResult']
  for (let i = 0; i < trueKeys.length; i++) {
    if (d[trueKeys[i]] === true) return true
    if (d[trueKeys[i]] === false) return false
  }
  return null
}

export function createIME() {
  let currentUuid = null
  let resolver = null
  let destroyed = false
  let debugCb = null

  function debug(msg) {
    console.log('[ime]', msg)
    if (debugCb) { try { debugCb(msg) } catch (e) {} }
  }

  function finish(payloadRaw) {
    const d = asObject(payloadRaw)
    debug('回调原始数据: ' + JSON.stringify(payloadRaw).substring(0, 300))

    // UUID 过滤: 仅当两边都有值且不匹配时才忽略
    const cbUuid = d && typeof d === 'object' && typeof d.uuid === 'string' ? d.uuid : null
    if (currentUuid && cbUuid && cbUuid !== currentUuid) {
      debug('uuid 不匹配, 忽略: ' + cbUuid)
      return
    }

    const text = extractText(d)
    let confirmed = extractConfirmed(d)
    // 无明确确认标志时, 有文本视为确认
    if (confirmed === null) confirmed = text !== ''
    debug('解析: confirmed=' + confirmed + ' text=' + text)

    const r = resolver
    cleanup()
    if (r) r(confirmed ? text : null)
  }

  function cleanup() {
    if (currentUuid !== null) {
      try { getManager().closeTextEdit(currentUuid) } catch (e) {}
      currentUuid = null
    }
    resolver = null
  }

  return {
    /** 调试: 传入 (msg)=>void 接收内部日志 */
    onDebug(cb) { debugCb = cb },

    open(config) {
      return new Promise((resolve, reject) => {
        const g = getManager()
        cleanup()

        try { g.textEditFinished.on(finish) } catch (e) { debug('signal订阅失败: ' + e) }
        try { $falcon.on('textEditFinished', finish) } catch (e) { debug('event订阅失败: ' + e) }

        resolver = resolve

        const cfg = Object.assign({
          text: config.text || '',
          placeholder: config.placeholder || '',
          placeholderColor: '#878A99',
          maxlength: config.maxlength || 64,
          maxLength: config.maxlength || 64,
          inputType: 'ZhCNPreferred',
          autofocus: true,
          showCursor: true,
          cursorColor: '#fb7299',
          cursorSize: 2,
          cursorIndex: 0,
          enterButtonText: '搜索',
          confirmText: '搜索',
          shouldCloseOnConfirm: true,
          closeButtonVisible: true,
          returnButtonVisible: true,
          micInputVisible: false,
          multiLinesEditVisible: false,
          action: 'input',
          type: 'text'
        }, config || {})

        try {
          currentUuid = g.startTextEdit(JSON.stringify(cfg))
          debug('startTextEdit uuid=' + currentUuid)
          if (!currentUuid) {
            throw new Error('startTextEdit 未返回 uuid')
          }
        } catch (e) {
          cleanup()
          try { g.textEditFinished.off(finish) } catch (e2) {}
          try { $falcon.off('textEditFinished', finish) } catch (e2) {}
          reject(e)
        }
      })
    },

    close() { cleanup() },

    destroy() {
      if (destroyed) return
      destroyed = true
      try { getManager().textEditFinished.off(finish) } catch (e) {}
      try { $falcon.off('textEditFinished', finish) } catch (e) {}
      cleanup()
    }
  }
}
