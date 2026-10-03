<template>
  <div class="page">
    <text class="t">2/4 网络图直载 (mini-glide) + @2040w.jpg 后缀</text>
    <div class="row">
      <div class="cell"><text class="l">net JPG</text><image class="b" :src="jpg" resize="contain"></image></div>
      <div class="cell"><text class="l">net PNG</text><image class="b" :src="png" resize="contain"></image></div>
      <div class="cell"><text class="l">JPG@2040w.jpg</text><image class="b" :src="jpgBig" resize="contain"></image></div>
      <div class="cell"><text class="l">本地 png</text><image class="b" :src="local" resize="contain"></image></div>
    </div>
  </div>
</template>
<script>
export default {
  data: function () {
    return {
      jpg: 'https://i0.hdslb.com/bfs/archive/d6f703c433759ca73c5b235ddf9d99d309be36d9.jpg',
      png: 'https://i0.hdslb.com/bfs/new_dyn/91bf639aec7b2ce6d442d433f089a163174501086.png',
      jpgBig: 'https://i0.hdslb.com/bfs/archive/d6f703c433759ca73c5b235ddf9d99d309be36d9.jpg@2040w.jpg',
      local: require('../../app_icon.png')
    }
  }
}
</script>
<style scoped>
.page { width: 960px; height: 266px; background-color: #101216; }
.t { font-size: 20px; color: #ffffff; margin-left: 14px; margin-top: 8px; }
.s { font-size: 16px; color: #ffd479; margin-left: 14px; margin-top: 6px; max-lines: 3; }
.row { flex-direction: row; margin-left: 14px; margin-top: 8px; }
.cell { width: 210px; margin-right: 10px; }
.l { font-size: 15px; color: #8fb8ff; height: 20px; }
.b { width: 180px; height: 120px; background-color: #22262c; }
</style>
