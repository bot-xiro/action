<template>
  <div class="page">
    <text class="t">图片能力自测 · 菜单页（纯文本，必须能正常显示）</text>
    <text class="s">逐项跑：miniapp_cli start 8001812345678999 --cvc / --cimg / --ctf / --csz</text>
    <text class="s">一页只放一个组件：某个组件不被支持时只黑这一页，不会连累其他项。</text>
  </div>
</template>
<script>
export default { data: function () { return {} } }
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
