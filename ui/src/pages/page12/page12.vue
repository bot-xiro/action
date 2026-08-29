<!-- 详情页副本 page12 : 渲染共享详情组件, nextPage 指向轮换环的下一站.
     固件同名页 navTo 只替换不叠加, 多个副本维持返回栈 (page->page2->...->page12->page). -->
<template>
  <DetailPage ref="d" :next-page="'page'" />
</template>

<script>
import DetailPage from '../page/page.vue'

export default {
  name: 'page12',
  components: { DetailPage: DetailPage },
  methods: {
    // BasePage 只向页面根组件转发 onShow/onHide/onUnload, 这里继续转给详情组件
    forward: function (hook) {
      var d = this.$refs && this.$refs.d
      if (d && typeof d[hook] === 'function') {
        try { d[hook]() } catch (e) { console.log('[page12] forward ' + hook + ' error: ' + (e && e.message ? e.message : e)) }
      }
    },
    onShow: function () { this.forward('onShow') },
    onHide: function () { this.forward('onHide') },
    onUnload: function () { this.forward('onUnload') }
  }
}
</script>