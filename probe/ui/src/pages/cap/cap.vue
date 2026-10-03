<template>
  <div class="page">
    <div class="bar">
      <text class="title">图片能力自测 (canvas / image / transform)</text>
      <div class="btn" @click="toggleTr"><text class="btn-t">{{ 'transform:' + (tr ? 'ON' : 'OFF') }}</text></div>
      <div class="btn" @click="cycleSize"><text class="btn-t">{{ '尺寸 ' + sizePct + '%' }}</text></div>
      <div class="btn" @click="draw"><text class="btn-t">重绘</text></div>
      <div class="btn" @click="cycleBig"><text class="btn-t">{{ 'box ' + bigSuffix }}</text></div>
    </div>

    <div class="row">
      <div class="cell">
        <text class="lab">1 canvas 2D</text>
        <canvas ref="cv" class="cv" :width="cvW" :height="cvH"></canvas>
      </div>
      <div class="cell">
        <text class="lab">2 net JPG contain</text>
        <image class="box" :src="jpg" resize="contain"></image>
      </div>
      <div class="cell">
        <text class="lab">3 net PNG contain</text>
        <image class="box" :src="png" resize="contain"></image>
      </div>
      <div class="cell">
        <text class="lab">4 {{ suffix }}</text>
        <image class="box" :src="jpg + suffix" resize="cover"></image>
      </div>
      <div class="cell cell-wide">
        <text class="lab">5 {{ tr ? 'CSS transform' : '动态宽高' }} x{{ sizePct }}%</text>
        <div class="clip">
          <image class="tf" :src="png" :style="tfStyle" resize="stretch"></image>
        </div>
      </div>
    </div>

    <text class="status">{{ status }}</text>
  </div>
</template>

<script>
// 图片查看器「更优实现」的可行性自测:
//   1) <canvas> 到底能不能用 (createCanvasContext 存在吗 / fillRect / drawImage 网络图)
//   2) mini-glide 能否直接吃网络 URL (JPG / PNG), 不必自己下载再落盘
//   3) B 站 @2040w.jpg 原图后缀能否直接被 <image> 加载
//   4) CSS transform 在这台固件上对 <image> 是否生效 (生效 -> 缩放/拖动可以纯 UI 做, 不用每帧编码)
//   5) 动态改 width/height 能否让 <image> 立刻按新尺寸清晰渲染
// 每项都用肉眼 + 状态文案判定, 不做任何"看向就成功"的假设.
var JPG = 'https://i0.hdslb.com/bfs/archive/d6f703c433759ca73c5b235ddf9d99d309be36d9.jpg'
var PNG = 'https://i0.hdslb.com/bfs/new_dyn/91bf639aec7b2ce6d442d433f089a163174501086.png'
var SUFFIXES = ['', '@2040w.jpg', '@800w.jpg']

export default {
  data: function () {
    return {
      jpg: JPG,
      png: PNG,
      suffix: '',
      bigSuffix: 'no-suffix',
      bigIx: 0,
      cvW: 210,
      cvH: 104,
      tr: true,
      sizePct: 100,
      status: 'init',
      _drew: false
    }
  },
  computed: {
    tfStyle: function () {
      var s = this.sizePct / 100
      var base = { width: '150px', height: '82px' }
      if (this.tr) {
        base.transform = 'scale(' + (s * 1.4) + ') translate(24px, 8px)'
        base.transformOrigin = 'left top'
      } else {
        base.width = Math.round(150 * s) + 'px'
        base.height = Math.round(82 * s) + 'px'
      }
      return base
    }
  },
  methods: {
    toggleTr: function () { this.tr = !this.tr; this.report('transform=' + (this.tr ? 'ON' : 'OFF') + ' 尺寸=' + this.sizePct + '%') },
    cycleSize: function () {
      this.sizePct = this.sizePct === 100 ? 50 : (this.sizePct === 50 ? 200 : 100)
      this.report('尺寸=' + this.sizePct + '% transform=' + (this.tr ? 'ON' : 'OFF'))
    },
    cycleBig: function () {
      this.bigIx = (this.bigIx + 1) % SUFFIXES.length
      this.suffix = SUFFIXES[this.bigIx]
      this.bigSuffix = this.suffix === '' ? 'no-suffix' : this.suffix
      this.report('第4格 src 后缀 = ' + this.bigSuffix)
    },
    report: function (s) {
      this.status = s + ' | ' + this._cvMsg
    },
    draw: function () {
      var self = this
      self._cvMsg = ''
      try {
        var cv = self.$refs.cv
        var ctx = null
        if (typeof createCanvasContext === 'function') ctx = createCanvasContext(cv)
        else if (cv && typeof cv.getContext === 'function') ctx = cv.getContext('2d')
        if (!ctx) { self._cvMsg = 'canvas: 无 ctx (createCanvasContext 不存在)'; self.status = self._cvMsg; return }
        var ok = []
        try { ctx.fillStyle = '#1b6ef3'; ctx.fillRect(0, 0, self.cvW, 30); ok.push('fillRect') } catch (e) { ok.push('fillRect ERR') }
        try { ctx.fillStyle = '#2ecc71'; ctx.fillRect(8, 38, 70, 56); ok.push('rect2') } catch (e) { ok.push('rect2 ERR') }
        if (typeof ctx.drawImage === 'function') {
          try { ctx.drawImage(self.png, 96, 38, 106, 56); ok.push('drawImage(net)') }
          catch (e) { ok.push('drawImage ERR:' + (e && e.message ? e.message : e)) }
        } else { ok.push('drawImage 缺失') }
        self._cvMsg = 'canvas OK [' + ok.join(' ') + ']'
        self.status = 'canvas OK [' + ok.join(' ') + ']'
      } catch (e) {
        self._cvMsg = 'canvas ERR ' + (e && e.message ? e.message : e)
        self.status = self._cvMsg
      }
    }
  },
  mounted: function () { if (!this._drew) { this._drew = true; this.draw() } },
  onShow: function () { if (!this._drew) { this._drew = true; this.draw() } }
}
</script>

<style scoped>
.page { width: 960px; height: 266px; background-color: #101216; }
.bar { height: 40px; flex-direction: row; align-items: center; background-color: #1b1e24; }
.title { font-size: 18px; color: #e6eaf0; margin-left: 12px; margin-right: 14px; }
.btn { height: 30px; padding-left: 10px; padding-right: 10px; margin-right: 8px; background-color: #2b3038; border-radius: 6px; justify-content: center; }
.btn-t { font-size: 16px; color: #cfd5de; }
.row { flex-direction: row; padding-left: 10px; padding-top: 6px; }
.cell { width: 180px; margin-right: 8px; }
.cell-wide { width: 260px; }
.lab { font-size: 14px; color: #8fb8ff; height: 18px; }
.cv { width: 210px; height: 104px; background-color: #22262c; }
.box { width: 170px; height: 100px; background-color: #22262c; }
.clip { width: 250px; height: 110px; background-color: #22262c; }
.tf { width: 150px; height: 82px; }
.status { font-size: 15px; color: #ffd479; margin-left: 12px; margin-top: 4px; height: 20px; }
</style>
