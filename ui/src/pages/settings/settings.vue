<template>
  <div class="page">
    <div class="topbar">
      <div class="back" @click="goBack">
        <text class="back-text">‹ 返回</text>
      </div>
      <text class="topbar-title">设置</text>
    </div>

    <scroller class="list" scroll-direction="vertical" :show-scrollbar="true"
              @touchstart="onTouchStart" @touchmove="onTouchMove" @touchend="onTouchEnd">
      <text v-if="status !== ''" class="status">{{ status }}</text>

      <div class="card">
        <text class="card-title">蓝牙音画延迟补偿</text>
        <text class="card-desc">用蓝牙耳机时: 画面超前就加大, 声音超前就减小或设 0。改完重新播放生效。</text>
        <div class="row">
          <div class="btn" @click="bumpAudio(-50)"><text class="btn-text">-50</text></div>
          <text class="value">{{ cfg.btaudioMs }} ms</text>
          <div class="btn" @click="bumpAudio(50)"><text class="btn-text">+50</text></div>
        </div>
        <div class="row">
          <div v-for="p in presets" :key="'p' + p.v" :class="['chip', cfg.btaudioMs === p.v ? 'chip-on' : '']" @click="setAudio(p.v)">
            <text class="chip-text">{{ p.label }}</text>
          </div>
        </div>
      </div>

      <div class="card">
        <text class="card-title">播放时防止息屏</text>
        <text class="card-desc">开启后播放期间每 6 秒点亮一次屏幕 (系统 hal-screen); 关闭则按系统时间自动息屏。</text>
        <div class="row">
          <div :class="['toggle', cfg.keepAwake ? 'toggle-on' : '']" @click="toggleKeepAwake">
            <text class="toggle-text">{{ cfg.keepAwake ? '已开启' : '已关闭' }}</text>
          </div>
        </div>
      </div>

      <div class="card">
        <text class="card-title">图片缓存</text>
        <text class="card-desc">列表封面会缓存本地, 清理后下次进入重新下载 (不影响登录/收藏)。</text>
        <div class="row">
          <div class="btn btn-wide" @click="clearCache"><text class="btn-text">清理图片缓存</text></div>
        </div>
      </div>

      <div class="card">
        <text class="card-title">恢复默认设置</text>
        <div class="row">
          <div class="btn btn-wide" @click="doReset"><text class="btn-text">恢复默认 (补偿 200ms · 防息屏 开)</text></div>
        </div>
      </div>

      <div class="card">
        <text class="card-title">诊断信息</text>
        <text class="info">版本 v{{ version }}</text>
        <text class="info">appid {{ appid }}</text>
        <text class="info">登录 {{ uid ? ('uid ' + uid) : '(未登录)' }}</text>
        <text class="info">日志 {{ logPath }}</text>
        <text class="info">配置 {{ cfgPath }}</text>
      </div>

      <text class="bottom-hint">bilibilipan · 词典笔 mini-app</text>
    </scroller>
  </div>
</template>

<script>
// 设置页: 蓝牙音画补偿 / 播放防息屏 / 清缓存 / 诊断信息
// 配置持久化: services/config.js -> /userdisk/xiro/bilibilipan.cfg.json
import { bilinet } from 'bilinet'
import { pm } from 'pm'
import { loadConfig, setCfg, resetConfig, CONFIG_PATH } from '../../services/config.js'
import { log, logStatus } from '../../services/log.js'
import { getMyInfo } from '../../services/bili.js'
import { getMid } from '../../services/auth.js'

export default {
  name: 'settings',
  data() {
    return {
      cfg: { btaudioMs: 200, keepAwake: true },
      presets: [
        { v: 0, label: '0 关闭' },
        { v: 150, label: '150' },
        { v: 200, label: '200 默认' },
        { v: 250, label: '250' },
        { v: 300, label: '300' }
      ],
      version: '',
      appid: '8001812345678901',
      uid: '',
      status: '',
      logPath: '/userdisk/xiro/bilibili.log',
      cfgPath: CONFIG_PATH,
      _statusTimer: null
    }
  },
  mounted() {
    this.reload()
  },
  onShow() {
    this.cfg = loadConfig()   // 从别的页返回时配置可能已变
  },
  methods: {
    reload() {
      this.cfg = loadConfig()
      try {
        const info = pm.getPackageInfo(this.appid)
        if (info && info.version) this.version = info.version
      } catch (e) {}
      try { this.logPath = logStatus() } catch (e) {}
      try { this.uid = getMid() || '' } catch (e) {}
      if (!this.uid) {
        const self = this
        getMyInfo().then(function (info) { if (info && info.mid) self.uid = String(info.mid) }).catch(function () {})
      }
    },
    goBack() {
      try { $falcon.navigateBack() } catch (e) { try { $falcon.navTo('index') } catch (e2) {} }
    },
    tip(msg) {
      const self = this
      this.status = msg
      if (this._statusTimer) clearTimeout(this._statusTimer)
      this._statusTimer = setTimeout(function () { self.status = '' }, 3000)
    },
    bumpAudio(delta) {
      this.cfg = setCfg('btaudioMs', this.cfg.btaudioMs + delta)
      this.tip('蓝牙音画补偿 = ' + this.cfg.btaudioMs + ' ms (重新播放生效)')
    },
    setAudio(v) {
      this.cfg = setCfg('btaudioMs', v)
      this.tip('蓝牙音画补偿 = ' + this.cfg.btaudioMs + ' ms')
    },
    toggleKeepAwake() {
      this.cfg = setCfg('keepAwake', !this.cfg.keepAwake)
      this.tip(this.cfg.keepAwake ? '播放防息屏已开启' : '播放防息屏已关闭')
    },
    clearCache() {
      try {
        const out = bilinet.exec('miniapp_cli trimImageCache')
        log('设置', '清理图片缓存: ' + out)
        this.tip('图片缓存已清理')
      } catch (e) { this.tip('清理失败: ' + (e && e.message ? e.message : e)) }
    },
    doReset() {
      this.cfg = resetConfig()
      this.tip('已恢复默认设置')
    },
    onTouchStart(e) { this._ty0 = this._ty(e); this._armed = false },
    onTouchMove(e) { if (this._ty(e) - this._ty0 > 55) this._armed = true },
    onTouchEnd() { if (this._armed) { this.reload(); this.tip('已刷新') } this._armed = false },
    _ty(e) {
      try {
        const t = (e && e.changedTouches && e.changedTouches[0]) || (e && e.touches && e.touches[0]) || e
        if (t && typeof t.pageY === 'number') return t.pageY
      } catch (err) {}
      return 0
    }
  }
}
</script>

<style scoped>
.page {
  width: 960px;
  height: 266px;
  background-color: #16181c;
  flex-direction: column;
}
.topbar {
  width: 960px;
  height: 34px;
  flex-direction: row;
  align-items: center;
  background-color: #1f1f1f;
}
.back {
  padding-left: 16px;
  padding-right: 16px;
  height: 34px;
  justify-content: center;
}
.back-text {
  font-size: 20px;
  color: #ffffff;
}
.topbar-title {
  font-size: 22px;
  color: #ffffff;
  margin-left: 8px;
}
.list {
  width: 960px;
  height: 232px;
}
.status {
  width: 100%;
  text-align: center;
  font-size: 20px;
  color: #fb7299;
  margin-top: 6px;
}
.card {
  width: 920px;
  margin-left: 20px;
  margin-top: 12px;
  padding-bottom: 12px;
  background-color: #1f1f1f;
  border-radius: 12px;
  flex-direction: column;
}
.card-title {
  font-size: 22px;
  color: #ffffff;
  margin-left: 16px;
  margin-top: 12px;
}
.card-desc {
  font-size: 17px;
  color: #999999;
  margin-left: 16px;
  margin-right: 16px;
  margin-top: 6px;
}
.row {
  flex-direction: row;
  align-items: center;
  margin-left: 16px;
  margin-top: 10px;
}
.btn {
  padding-left: 18px;
  padding-right: 18px;
  padding-top: 8px;
  padding-bottom: 8px;
  background-color: #2f3238;
  border-radius: 8px;
  justify-content: center;
}
.btn-wide {
  padding-left: 24px;
  padding-right: 24px;
}
.btn-text {
  font-size: 19px;
  color: #ffffff;
}
.value {
  font-size: 24px;
  color: #fb7299;
  margin-left: 22px;
  margin-right: 22px;
}
.chip {
  padding-left: 14px;
  padding-right: 14px;
  padding-top: 6px;
  padding-bottom: 6px;
  background-color: #2f3238;
  border-radius: 14px;
  margin-right: 10px;
  justify-content: center;
}
.chip-on {
  background-color: #fb7299;
}
.chip-text {
  font-size: 18px;
  color: #ffffff;
}
.toggle {
  padding-left: 22px;
  padding-right: 22px;
  padding-top: 8px;
  padding-bottom: 8px;
  background-color: #2f3238;
  border-radius: 8px;
  justify-content: center;
}
.toggle-on {
  background-color: #fb7299;
}
.toggle-text {
  font-size: 19px;
  color: #ffffff;
}
.info {
  font-size: 17px;
  color: #888888;
  margin-left: 16px;
  margin-top: 6px;
}
.bottom-hint {
  width: 100%;
  text-align: center;
  font-size: 16px;
  color: #555555;
  margin-top: 14px;
  margin-bottom: 16px;
}
</style>
