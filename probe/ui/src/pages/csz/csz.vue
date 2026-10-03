<template>
  <div class="page">
    <text class="t">4/4 动态改 width/height 能否即时重绘 (点按钮切换)</text>
    <div class="row">
      <div class="cell"><text class="l">{{ 'size ' + pct + '%' }}</text>
        <image class="b" :src="png" :style="{ width: w + 'px', height: h + 'px' }" resize="stretch"></image>
      </div>
      <div class="cell"><text class="l">固定 180x120 (对照)</text><image class="b" :src="png" resize="stretch"></image></div>
      <div class="btn" @click="cycle"><text class="bt">{{ '切到 ' + next + '%' }}</text></div>
    </div>
    <text class="s">判定：点按钮后左图随尺寸刷新且清晰 = 可用"改宽高"实现缩放</text>
  </div>
</template>
<script>
export default {
  data: function () {
    return {
      png: 'https://i0.hdslb.com/bfs/new_dyn/91bf639aec7b2ce6d442d433f089a163174501086.png',
      pct: 100, w: 180, h: 120
    }
  },
  computed: { next: function () { return this.pct === 100 ? 50 : (this.pct === 50 ? 200 : 100) } },
  methods: {
    cycle: function () {
      this.pct = this.next
      var k = this.pct / 100
      this.w = Math.round(180 * k)
      this.h = Math.round(120 * k)
    }
  }
}
</script>
<style scoped>
.page { width: 960px; height: 266px; background-color: #101216; }
.t { font-size: 19px; color: #ffffff; margin-left: 14px; margin-top: 6px; }
.s { font-size: 15px; color: #ffd479; margin-left: 14px; margin-top: 4px; }
.row { flex-direction: row; margin-left: 14px; margin-top: 6px; align-items: flex-start; }
.cell { width: 260px; margin-right: 10px; }
.l { font-size: 15px; color: #8fb8ff; height: 18px; }
.b { width: 180px; height: 120px; background-color: #22262c; }
.btn { height: 34px; padding-left: 14px; padding-right: 14px; background-color: #2b3038; border-radius: 6px; justify-content: center; }
.bt { font-size: 16px; color: #cfd5de; }
</style>
