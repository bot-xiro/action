<template>
  <div class="page">
    <text class="title">{{ title }}</text>
    <text class="info">BV: {{ bvid }}</text>
    <text class="hint">播放功能开发中</text>
    <text class="back" @click="goBack">返回</text>
  </div>
</template>

<script>
export default {
  name: 'page',
  data() {
    return {
      bvid: '',
      title: ''
    }
  },
  methods: {
    onShow() {
      const options = this.$page.options || {}
      this.bvid = options.bvid || ''
      this.title = options.title || ''
    },
    goBack() {
      this.$page.finish()
    }
  }
}
</script>

<style scoped>
.page {
  width: 960px;
  height: 266px;
  background-color: #141414;
  display: flex;
  flex-direction: column;
  align-items: center;
}
.title {
  font-size: 26px;
  color: #ffffff;
  margin-top: 30px;
  max-lines: 2;
  text-overflow: ellipsis;
  overflow: hidden;
}
.info {
  font-size: 22px;
  color: #888888;
  margin-top: 16px;
}
.hint {
  font-size: 24px;
  color: #fb7299;
  margin-top: 30px;
}
.back {
  font-size: 24px;
  color: #ffffff;
  background-color: #2c2c2c;
  border-radius: 24px;
  width: 160px;
  height: 48px;
  text-align: center;
  margin-top: 30px;
}
</style>
