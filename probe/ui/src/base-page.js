// BasePage (videoprobe): 托管 $falcon.on + timeout/interval, onUnload finally 统一释放
// 参考 skill falcon-runtime.md. 页面里不要用裸 setTimeout/setInterval.
class PageRes extends $falcon.Page {
  constructor() {
    super()
    this._subs = []
    this._tos = []
    this._tiv = []
  }
  on(name, cb) {
    var token = $falcon.on(name, cb)
    this._subs.push({ name: name, token: token })
    return token
  }
  off(name, cb) {
    try { $falcon.off(name, cb) } catch (e) {}
    this._subs = this._subs.filter(function (s) { return !(s.name === name && s.token === cb) })
  }
  setTimeout(fn, ms) {
    var self = this
    var token = setTimeout(function () {
      self._tos = self._tos.filter(function (t) { return t !== token })
      fn()
    }, ms)
    this._tos.push(token)
    return token
  }
  setInterval(fn, ms) {
    var token = setInterval(fn, ms)
    this._tiv.push(token)
    return token
  }
  clearTimeout(token) {
    this._tos = this._tos.filter(function (t) { return t !== token })
    clearTimeout(token)
  }
  clearInterval(token) {
    this._tiv = this._tiv.filter(function (t) { return t !== token })
    clearInterval(token)
  }
  release() {
    var i
    for (i = 0; i < this._subs.length; i++) {
      try { $falcon.off(this._subs[i].name, this._subs[i].token) } catch (e) {}
    }
    this._subs = []
    for (i = 0; i < this._tos.length; i++) clearTimeout(this._tos[i])
    this._tos = []
    for (i = 0; i < this._tiv.length; i++) clearInterval(this._tiv[i])
    this._tiv = []
  }
}

export class BasePage extends PageRes {
  onLoad(options) {
    super.onLoad(options)
    this.options = options
  }
  onNewOptions(options) {
    super.onNewOptions(options)
    this.options = options
  }
  onShow() {
    super.onShow()
    if (this.$root && this.$root.onShow) this.$root.onShow()
  }
  onHide() {
    super.onHide()
    if (this.$root && this.$root.onHide) this.$root.onHide()
  }
  onUnload() {
    try {
      super.onUnload()
      if (this.$root && this.$root.onUnload) this.$root.onUnload()
    } finally {
      this.release()
    }
  }
}
