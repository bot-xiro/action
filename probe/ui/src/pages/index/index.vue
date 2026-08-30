<template>
  <div class="page">
    <!-- hole 挖透: 中间 44..222 竖带; rect=band 时对应物理 video 平面区域.
         底/顶两条 44px 是 UI 区, 不会被打洞, 便于验证 UI 始终可见. -->
    <!-- v-if holeShow 控制 hole 是否真的出现 (测试 hole 是否是必需机制) -->
    <hole v-if="holeShow" class="hole-band"></hole>

    <!-- 顶条: 返回 + 标题 -->
    <div class="bar top">
      <div class="btn btn-back" @click="goBack">
        <text class="btn-text">‹ 返回</text>
      </div>
      <text class="title">videoprobe 分层探测</text>
    </div>

    <!-- 中央: 状态 -->
    <div class="mid">
      <text class="status">{{ status }}</text>
    </div>

    <!-- 底条: 控制按钮行 (多行放得下, 一行 6 个) -->
    <div class="bar bot">
      <div :class="'btn flip' + (flipSel===0?' on':'')" @click="pickFlip(0)"><text class="btn-text">0°</text></div>
      <div :class="'btn flip' + (flipSel===1?' on':'')" @click="pickFlip(1)"><text class="btn-text">+90</text></div>
      <div :class="'btn flip' + (flipSel===3?' on':'')" @click="pickFlip(3)"><text class="btn-text">-90</text></div>
      <div :class="'btn' + (rectMode==='band'?' on':'')" @click="pickRect('band')"><text class="btn-text">条带</text></div>
      <div :class="'btn' + (rectMode==='full'?' on':'')" @click="pickRect('full')"><text class="btn-text">全屏</text></div>
      <div :class="'btn' + (holeShow?' on':'')" @click="toggleHole"><text class="btn-text">hole</text></div>
      <div class="btn btn-play" @click="togglePlay"><text class="btn-text">{{ playing ? '■' : '▶' }}</text></div>
    </div>
  </div>
</template>

<script>
// 硬事实 (本词典笔 profile):
//   logical 960x266, physical DSI-1 480x960 direction=270 yoffset=107
//   屏幕内容按 Falcon 逻辑坐标 ((960,266)) 排版;
//   KMS video 用 physical 480x960 帧; 逻辑->物理映射:
//     p_x = l_y + 107
//     p_y = 959 - (l_x + l_w)
//     p_w = l_h
//     p_h = l_w
// 按 media-kms.md 要求先验证这个映射对不对, flip 用哪个, hole 用不用
import * as videoprobeMod from 'videoprobe'
import { GST_URI } from '../../services/videoprobe.js'

export default {
  data: function () {
    return {
      status: '待播',
      playing: false,
      flipSel: 3,
      rectMode: 'band',
      holeShow: true
    }
  },
  methods: {
    doOpen: function () {
      // 新探索: native open(uri, flip, rectMode), close() 幂等
      var vp = window.__vp
      if (!vp) { this.status = 'no module'; return }
      var mode = this.rectMode === 'band' ? 'band' : 'full'
      var mNum = this.rectMode === 'band' ? 1 : 0
      try { vp.close() } catch (e) {}
      this.status = 'open flip=' + this.flipSel + ' mode=' + mode
      try {
        vp.open(GST_URI, this.flipSel, mNum)
        vp.start()
        this.playing = true
        this.status = 'play flip=' + this.flipSel + ' mode=' + mode
      } catch (e) {
        this.status = 'open fail: ' + (e && e.message ? e.message : e)
      }
    },
    togglePlay: function () {
      var vp = window.__vp
      if (!vp) return
      if (!this.playing) { this.doOpen(); return }
      try { vp.close() } catch (e) {}
      this.playing = false
      this.status = 'stopped'
    },
    pickFlip: function (n) {
      this.flipSel = n
      if (this.playing) { this.doOpen() }
    },
    pickRect: function (m) {
      this.rectMode = m
      if (this.playing) { this.doOpen() }
    },
    toggleHole: function () {
      this.holeShow = !this.holeShow
    },
    goBack: function () {
      var vp = window.__vp
      try { if (vp) vp.close() } catch (e) {}
      this.$page.finish()
    }
  },
  mounted: function () {
    var self = this
    if (!window.__vp) {
      try {
        window.__vp = (videoprobeMod && videoprobeMod.videoprobe) || videoprobeMod
        self.status = 'module ok'
      } catch (e) {
        self.status = 'module import fail: ' + (e && e.message ? e.message : e)
        return
      }
    }
  },
  onUnload: function () {
    try { if (window.__vp) window.__vp.close() } catch (e) {}
  }
}
</script>

<style scoped>
.page { width: 960px; height: 266px; background-color: transparent; }
.hole-band {
  position: absolute;
  left: 0px; top: 44px;
  width: 960px; height: 178px;
}
.bar {
  position: absolute; left: 0px;
  width: 960px; height: 44px;
  background-color: rgba(0, 0, 0, 0.55);
  flex-direction: row; align-items: center;
  padding-left: 8px; padding-right: 8px;
}
.top { top: 0px; }
.bot { top: 222px; }
.title { font-size: 20px; color: #ffffff; margin-left: 8px; flex: 1; }
.status { font-size: 20px; color: #ffd166; }
.btn {
  height: 32px; padding-left: 10px; padding-right: 10px;
  border-radius: 16px;
  background-color: rgba(255, 255, 255, 0.15);
  margin-right: 6px;
  justify-content: center;
  align-items: center;
}
.btn-text { font-size: 20px; color: #ffffff; }
.btn.on { background-color: rgba(64, 158, 255, 0.5); }
.btn-text-mini { font-size: 16px; }
.mid { position: absolute; left: 0px; top: 100px; width: 960px; height: 66px; align-items: center; justify-content: center; }
.btn-back { background-color: rgba(255, 80, 80, 0.6); }
.btn-play { margin-left: 6px; }
.flip { min-width: 50px; }
</style>
