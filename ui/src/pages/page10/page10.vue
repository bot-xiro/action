<script>
import detail from '../page/page.vue'

// 详情页副本: 与 ../page/page.vue 共享同一实现, 仅把推荐跳转轮换到下一副本.
// 固件同名页 navTo 只替换不叠加, 轮换副本页维持返回栈 (见 page.vue openVideo 注释).
export default Object.assign({}, detail, {
  name: 'page10',
  methods: Object.assign({}, detail.methods, {
    openVideo: function (item) {
      $falcon.navTo('page11', { bvid: item.bvid, title: item.title })
    }
  })
})
</script>


