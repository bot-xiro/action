<template>
  <div class="page">
    <text class="t">1/4 canvas 2D 测试</text>
    <canvas ref="cv" class="cvv" :width="220" :height="110"></canvas>
    <text class="s">{{ msg }}</text>
  </div>
</template>
<script>
export default {
  data: function () { return { msg: 'canvas: 未执行' } },
  methods: {
    run: function () {
      try {
        var cv = this.$refs.cv
        var ctx = null
        if (typeof createCanvasContext === 'function') ctx = createCanvasContext(cv)
        else if (cv && typeof cv.getContext === 'function') ctx = cv.getContext('2d')
        if (!ctx) { this.msg = 'canvas: 拿不到 ctx (createCanvasContext 不存在)'; return }
        var out = []
        try { ctx.fillStyle = '#e74c3c'; ctx.fillRect(0, 0, 220, 40); out.push('fillRect ok') } catch (e) { out.push('fillRect ERR') }
        try { ctx.fillStyle = '#2ecc71'; ctx.fillRect(16, 52, 70, 44); out.push('rect2 ok') } catch (e) { out.push('rect2 ERR') }
        out.push(typeof ctx.drawImage === 'function' ? '有 drawImage' : '无 drawImage')
        this.msg = 'canvas: ' + out.join(' / ')
      } catch (e) { this.msg = 'canvas ERR: ' + (e && e.message ? e.message : e) }
    }
  },
  mounted: function () { this.run() },
  onShow: function () { this.run() }
}
</script>
<style scoped>
.page { width: 960px; height: 266px; background-color: #101216; }
.t { font-size: 20px; color: #ffffff; margin-left: 14px; margin-top: 8px; }
.s { font-size: 16px; color: #ffd479; margin-left: 14px; margin-top: 6px; }
.cvv { width: 220px; height: 110px; margin-left: 14px; margin-top: 6px; background-color: #22262c; }
</style>
