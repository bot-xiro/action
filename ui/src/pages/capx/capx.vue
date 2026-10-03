<template>
  <div class="page">
    <div class="bar">
      <div class="bk" @click="back"><text class="bk-t">‹ 返回</text></div>
      <text class="tt">{{ '能力自测: ' + mode }}</text>
      <div class="b" @click="setMode('menu')"><text class="bt">菜单</text></div>
      <div class="b" @click="setMode('canvas')"><text class="bt">canvas</text></div>
      <div class="b" @click="setMode('img')"><text class="bt">网络图</text></div>
      <div class="b" @click="setMode('tf')"><text class="bt">transform</text></div>
      <div class="b" @click="setMode('sz')"><text class="bt">改宽高</text></div>
    </div>

    <div v-if="mode === 'menu'" class="body">
      <text class="h">这是纯文本菜单页 —— 如果连它都显示不出来, 说明页面/路由有问题, 不是组件问题。</text>
      <text class="s">{{ msg }}</text>
    </div>

    <div v-if="mode === 'canvas'" class="body">
      <canvas ref="cv" class="cvv" :width="220" :height="110"></canvas>
      <text class="s">{{ msg }}</text>
    </div>

    <div v-if="mode === 'img'" class="row">
      <div class="cell"><text class="l">net JPG contain</text><image class="im" :src="jpg" resize="contain"></image></div>
      <div class="cell"><text class="l">net PNG contain</text><image class="im" :src="png" resize="contain"></image></div>
      <div class="cell"><text class="l">JPG + @2040w.jpg</text><image class="im" :src="jpgBig" resize="contain"></image></div>
      <div class="cell"><text class="l">本地 png</text><image class="im" :src="png" resize="cover"></image></div>
    </div>

    <div v-if="mode === 'tf'" class="row">
      <div class="cell"><text class="l">原样</text><image class="im" :src="png" resize="stretch"></image></div>
      <div class="cell"><text class="l">transform:scale(.5)</text>
        <div class="clip"><image class="im2" :src="png" resize="stretch"></image></div></div>
      <div class="cell"><text class="l">transform:scale(2)</text>
        <div class="clip"><image class="im3" :src="png" resize="stretch"></image></div></div>
    </div>

    <div v-if="mode === 'sz'" class="row">
      <div class="cell"><text class="l">{{ '动态 ' + pct + '%' }}</text>
        <image class="im" :src="png" :style="{ width: w + 'px', height: h + 'px' }" resize="stretch"></image></div>
      <div class="b2" @click="cycle"><text class="bt">{{ '切到 ' + next + '%' }}</text></div>
    </div>
  </div>
</template>

<script>
// 先验证能力再选实现: canvas / 网络图直载 / CSS transform / 动态改宽高
var JPG = 'https://i0.hdslb.com/bfs/archive/d6f703c433759ca73c5b235ddf9d99d309be36d9.jpg'
var PNG = 'https://i0.hdslb.com/bfs/new_dyn/91bf639aec7b2ce6d442d433f089a163174501086.png'
export default {
  data: function () {
    return {
      mode: 'menu',
      msg: '按上面按钮逐项测',
      jpg: JPG, png: PNG, jpgBig: JPG + '@2040w.jpg',
      pct: 100, w: 180, h: 120
    }
  },
  computed: { next: function () { return this.pct === 100 ? 50 : (this.pct === 50 ? 200 : 100) } },
  methods: {
    back: function () { try { this.$page.finish() } catch (e) {} },
    setMode: function (m) {
      this.mode = m
      this.msg = '模式=' + m
      if (m === 'canvas') { var self = this; setTimeout(function () { self.runCanvas() }, 60) }
    },
    cycle: function () {
      this.pct = this.next
      var k = this.pct / 100
      this.w = Math.round(180 * k); this.h = Math.round(120 * k)
    },
    runCanvas: function () {
      try {
        var cv = this.$refs.cv
        var ctx = null
        if (typeof createCanvasContext === 'function') ctx = createCanvasContext(cv)
        else if (cv && typeof cv.getContext === 'function') ctx = cv.getContext('2d')
        if (!ctx) { this.msg = 'canvas: 拿不到 ctx'; return }
        var out = []
        try { ctx.fillStyle = '#e74c3c'; ctx.fillRect(0, 0, 220, 40); out.push('fillRect ok') } catch (e) { out.push('fillRect ERR') }
        out.push(typeof ctx.drawImage === 'function' ? '有drawImage' : '无drawImage')
        this.msg = 'canvas: ' + out.join(' / ')
      } catch (e) { this.msg = 'canvas ERR: ' + (e && e.message ? e.message : e) }
    }
  }
}
</script>

<style scoped>
.page { position: absolute; left: 0px; top: 0px; width: 960px; height: 266px; background-color: #14161a; }
.bar { height: 40px; flex-direction: row; align-items: center; background-color: #1b1e24; }
.bk { padding-left: 12px; padding-right: 12px; height: 34px; justify-content: center; }
.bk-t { font-size: 18px; color: #cfd5de; }
.tt { font-size: 17px; color: #e6eaf0; margin-right: 10px; }
.b { height: 28px; padding-left: 10px; padding-right: 10px; margin-right: 6px; background-color: #2b3038; border-radius: 6px; justify-content: center; }
.b2 { height: 32px; padding-left: 12px; padding-right: 12px; margin-left: 8px; background-color: #2b3038; border-radius: 6px; justify-content: center; }
.bt { font-size: 15px; color: #cfd5de; }
.body { padding-left: 14px; padding-top: 8px; }
.h { font-size: 18px; color: #dfe4ea; }
.s { font-size: 16px; color: #ffd479; margin-top: 6px; }
.row { flex-direction: row; padding-left: 12px; padding-top: 6px; }
.cell { width: 210px; margin-right: 8px; }
.l { font-size: 14px; color: #8fb8ff; height: 18px; }
.im { width: 180px; height: 120px; background-color: #22262c; }
.im2 { width: 180px; height: 120px; background-color: #22262c; transform: scale(0.5); transform-origin: 0px 0px; }
.im3 { width: 180px; height: 120px; background-color: #22262c; transform: scale(2); transform-origin: 0px 0px; }
.clip { width: 200px; height: 130px; }
.cvv { width: 220px; height: 110px; background-color: #22262c; margin-left: 14px; margin-top: 6px; }
</style>
