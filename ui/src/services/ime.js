// 系统输入法封装 v3 (崩溃修复版)
// 设备确认: Global 模块含 startTextEdit/closeTextEdit, 用户实测键盘能弹出.
// 回调通道存在两种可能 (历史探索证据):
//   1. Global 实例信号: manager.textEditFinished.on(handler)
//   2. 全局事件: $falcon.on('textEditFinished', handler), 事件包装 {type, timestamp, data}
// 两种都订阅. 回调数据兼容: 字符串 / {data} 包装 / {value|text|content|result} /
// 确认标志 editConfirmed|confirm|confirmed|ok
// v3 修复: 按键盘「搜索」键后整个进程闪退
//   - finish() 回调内不再直接 JSON.stringify 原生 payload (可能是循环引用的
//     原生对象, stringify 抛异常会直接杀死 Framework 进程), 改用 safeStringify
//   - finish() 整体 try/catch 兜底, 任何异常都不再逃逸到原生派发器
//   - shouldCloseOnConfirm 置 false, 会话由我们拿到结果后自己 closeTextEdit,
//     避免输入法先销毁会话、我们又 close 一个已失效 UUID 触发原生崩溃
//   - 订阅/退订严格配对: 成功结束的回调也会 off, 不再每次 open 重复累加订阅
import globalModule from 'global'

let manager = null
function getManager() {
  if (!manager) {
    manager = new globalModule.Global()
  }
  return manager
}

function safeStringify(v) {
  try {
    return JSON.stringify(v)
  } catch (e) {
    return '[unserializable: ' + v + ']'
  }
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
  let subscribed = false

  function debug(msg) {
    console.log('[ime]', msg)
    if (debugCb) { try { debugCb(msg) } catch (e) {} }
  }

  function subscribe() {
    if (subscribed) return
    subscribed = true
    try { getManager().textEditFinished.on(finish) } catch (e) { debug('signal订阅失败: ' + e) }
    try { $falcon.on('textEditFinished', finish) } catch (e) { debug('event订阅失败: ' + e) }
  }

  function unsubscribe() {
    if (!subscribed) return
    subscribed = false
    try { getManager().textEditFinished.off(finish) } catch (e) {}
    try { $falcon.off('textEditFinished', finish) } catch (e) {}
  }

  function closeSession() {
    if (currentUuid !== null) {
      const uuid = currentUuid
      currentUuid = null
      try { getManager().closeTextEdit(uuid) } catch (e) { debug('closeTextEdit 异常: ' + e) }
    }
  }

  function finish(payloadRaw) {
    // 顶层兜底: 回调里任何异常都必须消化在这里,
    // 一旦逃回原生信号派发器就是整个 miniapp 进程崩溃 (用户看到的闪退)
    try {
      // 无等待中的会话则忽略 (防重复回调)
      if (resolver === null) return

      const d = asObject(payloadRaw)
      debug('回调原始数据: ' + safeStringify(d).substring(0, 300))

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
      resolver = null
      unsubscribe()
      closeSession()
      if (r) r(confirmed ? text : null)
    } catch (e) {
      debug('finish 异常(已兜底): ' + e)
      const r = resolver
      resolver = null
      try { unsubscribe() } catch (e2) {}
      try { closeSession() } catch (e2) {}
      if (r) r(null)
    }
  }

  function cleanup() {
    unsubscribe()
    closeSession()
    resolver = null
  }

  return {
    /** 调试: 传入 (msg)=>void 接收内部日志 */
    onDebug(cb) { debugCb = cb },

    open(config) {
      return new Promise((resolve, reject) => {
        try { getManager() } catch (e) { reject(e); return }
        cleanup()

        subscribe()
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
          // 不让输入法自行销毁会话, 由 finish 拿到结果后统一关闭
          shouldCloseOnConfirm: false,
          closeButtonVisible: true,
          returnButtonVisible: true,
          micInputVisible: false,
          multiLinesEditVisible: false,
          action: 'input',
          type: 'text'
        }, config || {})

        try {
          currentUuid = getManager().startTextEdit(safeStringify(cfg))
          debug('startTextEdit uuid=' + currentUuid)
          if (!currentUuid) {
            throw new Error('startTextEdit 未返回 uuid')
          }
        } catch (e) {
          cleanup()
          reject(e)
        }
      })
    },

    close() { cleanup() },

    destroy() {
      if (destroyed) return
      destroyed = true
      cleanup()
    }
  }
}
