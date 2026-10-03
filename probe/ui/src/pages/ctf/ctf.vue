<template>
  <div class="page">
    <text class="t">3/4 CSS transform 是否对 image 生效 (左=无, 右=scale 0.5 + translate)</text>
    <div class="row">
      <div class="cell"><text class="l">原样 180x120</text><image class="b" :src="png" resize="stretch"></image></div>
      <div class="cell"><text class="l">transform scale(.5)</text>
        <div class="clip"><image class="b2" :src="png" resize="stretch"></image></div>
      </div>
      <div class="cell"><text class="l">transform scale(2)</text>
        <div class="clip"><image class="b3" :src="png" resize="stretch"></image></div>
      </div>
    </div>
    <text class="s">判定：三张若都一样 = transform 不生效；右侧明显缩小/放大 = 生效（就能纯 UI 做缩放拖动）</text>
  </div>
</template>
<script>
export default {
  data: function () {
    return { png: 'https://i0.hdslb.com/bfs/new_dyn/91bf639aec7b2ce6d442d433f089a163174501086.png' }
  }
}
</script>
<style scoped>
.page { width: 960px; height: 266px; background-color: #101216; }
.t { font-size: 19px; color: #ffffff; margin-left: 14px; margin-top: 6px; }
.s { font-size: 15px; color: #ffd479; margin-left: 14px; margin-top: 4px; }
.row { flex-direction: row; margin-left: 14px; margin-top: 6px; }
.cell { width: 230px; margin-right: 10px; }
.l { font-size: 15px; color: #8fb8ff; height: 18px; }
.b { width: 180px; height: 120px; background-color: #22262c; }
.b2 { width: 180px; height: 120px; background-color: #22262c; transform: scale(0.5); transform-origin: left top; }
.b3 { width: 180px; height: 120px; background-color: #22262c; transform: scale(2); transform-origin: left top; }
.clip { width: 200px; height: 130px; }
</style>
