<template>
  <div class="page">
    <!-- 顶部：名称 + 返回 -->
    <div class="topbar">
      <div class="btn-back" @click="onBack">
        <text class="btn-back-text">返回</text>
      </div>
      <view class="title-wrap">
        <text class="title">{{ title }}</text>
        <text class="subtitle">{{ nativeLabel }}</text>
      </view>
    </div>

    <!-- 视频画面占位（视频经 KMS 硬件图层渲染到屏幕，此处为 UI 层显示状态） -->
    <div class="stage">
      <text class="stage-text" v-if="state === 'idle'">选择视频源后点「播放」</text>
      <text class="stage-text" v-else-if="state === 'loading'">加载中…</text>
      <text class="stage-text" v-else-if="state === 'error'">出错：{{ lastError }}</text>
      <text class="stage-text" v-else>播放中（视频输出到屏幕硬件图层）</text>
    </div>

    <!-- 状态与时间 -->
    <div class="meta-row">
      <text class="meta-label">状态</text>
      <text class="meta-value" :class="state === 'error' ? 'state-err' : 'state-ok'">{{ stateLabel }}</text>
    </div>
    <div class="meta-row">
      <text class="meta-label">时间</text>
      <text class="meta-value">{{ posText }} / {{ durText }}</text>
    </div>

    <!-- 进度条 -->
    <div class="progress-wrap" v-if="!isLiveValue">
      <div class="progress-track" @click="onProgressClick">
        <div class="progress-fill" :style="{ width: progressPct + '%' }"></div>
        <div class="progress-thumb" :style="{ left: thumbLeft + 'px' }"></div>
      </div>
      <div class="progress-times">
        <text class="time-text">{{ posText }}</text>
        <text class="time-text">{{ durText }}</text>
      </div>
    </div>

    <!-- 控制按钮行 -->
    <div class="controls">
      <div class="btn btn-refresh" @click="onSeek(-10)">
        <text class="btn-text"><< 10s</text>
      </div>
      <div class="btn" :class="isPlaying ? 'btn-pause' : 'btn-play'" @click="onTogglePlay">
        <text class="btn-text">{{ isPlaying ? '暂停' : '播放' }}</text>
      </div>
      <div class="btn btn-refresh" @click="onSeek(10)">
        <text class="btn-text">10s >></text>
      </div>
      <div class="btn btn-stop" @click="onStop">
        <text class="btn-text">停止</text>
      </div>
    </div>

    <!-- 视频源列表 -->
    <div class="list-head">
      <text class="list-head-text">在线测试视频源</text>
    </div>
    <scroller class="src-scroller" direction="column">
      <div
        v-for="(s, idx) in sources"
        :key="s.id"
        class="src-item"
        :class="selectedIdx === idx ? 'src-item-selected' : ''"
        @click="selectSource(idx)"
      >
        <text class="src-label">{{ s.label }}</text>
        <text class="src-note">{{ s.note }}</text>
      </div>
    </scroller>

    <!-- 事件日志 -->
    <div class="list-head">
      <text class="list-head-text">事件日志</text>
    </div>
    <scroller class="log-scroller" direction="column">
      <text v-for="(line, i) in logs" :key="i" class="log-line">{{ line }}</text>
    </scroller>
  </div>
</template>

<script>
import PlayerService from "../../services/player-service.js";
import { VIDEO_SOURCES, DEFAULT_SOURCE_ID } from "../../services/video-sources.js";

function ts() {
  const d = new Date();
  const p = (n) => (n < 10 ? "0" + n : "" + n);
  return p(d.getHours()) + ":" + p(d.getMinutes()) + ":" + p(d.getSeconds());
}

function fmtMs(ms) {
  if (ms == null || ms < 0) return "--:--";
  const s = Math.floor(ms / 1000);
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  const p = (n) => (n < 10 ? "0" + n : "" + n);
  if (h > 0) return h + ":" + p(m) + ":" + p(sec);
  return m + ":" + p(sec);
}

export default {
  name: "PagePlayer",
  data() {
    return {
      sources: VIDEO_SOURCES,
      selectedIdx: 0,
      state: "idle",
      title: "未加载",
      positionMs: 0,
      durationMs: 0,
      lastError: "",
      isLive: false,
      nativeLabel: "…",
      logs: [],
      unsubscribe: null,
      pollTimer: 0,
      loading: false,
    };
  },
  computed: {
    stateLabel() {
      const map = { idle: "空闲", loading: "加载中", playing: "播放中", paused: "已暂停", error: "错误" };
      return map[this.state] || this.state;
    },
    isPlaying() {
      return this.state === "playing";
    },
    isLiveValue() {
      return this.isLive;
    },
    posText() {
      return fmtMs(this.positionMs);
    },
    durText() {
      return this.isLive ? "直播" : fmtMs(this.durationMs);
    },
    progressPct() {
      if (this.isLive || this.durationMs <= 0) return 0;
      return Math.min(100, (this.positionMs / this.durationMs) * 100).toFixed(1);
    },
    thumbLeft() {
      return Math.max(0, this.progressPct / 100 * (this.trackWidthEstimate() - 16));
    },
  },
  created() {
    const idx = this.sources.findIndex((s) => s.id === DEFAULT_SOURCE_ID);
    if (idx >= 0) this.selectedIdx = idx;
    this.nativeLabel = PlayerService.hasNative()
      ? "native pvplayer v" + PlayerService.getVersion()
      : "native 未加载（mock）";
  },
  mounted() {
    this.startEventSub();
    this.pushLog("页面就绪");
  },
  methods: {
    onShow() {
      this.startEventSub();
    },
    onHide() {
      this.stopPoll();
      this.onStop();
    },
    onUnload() {
      this.stopPoll();
      this.onStop();
    },
    beforeDestroy() {
      this.stopPoll();
      this.stopEventSub();
    },

    pushLog(text) {
      this.logs.push("[" + ts() + "] " + text);
      if (this.logs.length > 60) this.logs.shift();
    },

    startEventSub() {
      if (this.unsubscribe) return;
      this.unsubscribe = PlayerService.subscribe((jsonStr) => {
        try {
          const ev = typeof jsonStr === "string" ? JSON.parse(jsonStr) : jsonStr;
          this.pushLog("事件: " + JSON.stringify(ev));
          if (ev && ev.event === "state" && ev.state) this.state = ev.state;
        } catch (e) {
          this.pushLog("事件解析失败");
        }
      });
    },
    stopEventSub() {
      if (this.unsubscribe) {
        this.unsubscribe();
        this.unsubscribe = null;
      }
    },

    selectSource(idx) {
      this.selectedIdx = idx;
      const s = this.sources[idx];
      this.title = s.label;
      this.pushLog("选择: " + s.label);
    },

    trackWidthEstimate() {
      // Falcon 屏幕 960 宽，页面 padding 24*2，左右进度条占满 – 边距 24
      return 960 - 48 - 48;
    },

    async onTogglePlay() {
      if (this.state === "playing") await this.onPause();
      else if (this.state === "paused") await this.onResume();
      else await this.onStartSelected();
    },

    async onStartSelected() {
      const s = this.sources[this.selectedIdx];
      if (!s || this.loading) return;
      this.loading = true;
      try {
        const v = PlayerService.validate(s.url, s.type);
        if (!v.ok) {
          this.lastError = v.reason || "invalid";
          this.pushLog("预检失败: " + this.lastError);
          return;
        }
        this.isLive = !!v.isLive;
        this.pushLog("加载: " + s.label);
        const r = await PlayerService.load(s.url, s.type);
        if (r.res && r.res.success && r.res.status) {
          this.applyStatus(r.res.status);
          this.pushLog("已加载，state=" + r.res.status.state);
          await this.onPlay();
          this.startPoll();
        } else if (r.source === "mock") {
          this.pushLog("mock 加载（无 native）");
          this.state = "paused";
          this.title = s.label;
        } else {
          this.lastError = (r.res && r.res.error) || "load failed";
          this.state = "error";
          this.pushLog("加载失败: " + this.lastError);
        }
      } catch (e) {
        this.lastError = String(e && e.message ? e.message : e);
        this.state = "error";
        this.pushLog("加载异常: " + this.lastError);
      } finally {
        this.loading = false;
      }
    },

    async onPlay() {
      try {
        const r = await PlayerService.play();
        if (r.res && r.res.status) this.applyStatus(r.res.status);
        this.pushLog("播放");
      } catch (e) {
        this.pushLog("播放异常: " + String(e));
      }
    },
    async onPause() {
      try {
        const r = await PlayerService.pause();
        if (r.res && r.res.status) this.applyStatus(r.res.status);
        this.pushLog("暂停");
      } catch (e) {
        this.pushLog("暂停异常: " + String(e));
      }
    },
    async onResume() {
      try {
        const r = await PlayerService.resume();
        if (r.res && r.res.status) this.applyStatus(r.res.status);
        this.pushLog("恢复");
      } catch (e) {
        this.pushLog("恢复异常: " + String(e));
      }
    },
    async onStop() {
      this.stopPoll();
      try {
        await PlayerService.stop();
      } catch (e) {
        this.pushLog("停止异常: " + String(e));
      }
      this.state = "idle";
      this.positionMs = 0;
      this.pushLog("停止");
    },

    async onSeek(deltaSec) {
      if (this.isLive) {
        this.pushLog("直播流不支持快进");
        return;
      }
      const target = Math.max(0, (this.positionMs + deltaSec * 1000) / 1000);
      try {
        const r = await PlayerService.seek(target);
        if (r.res && r.res.status) this.applyStatus(r.res.status);
        this.pushLog("快进到 " + fmtMs(target * 1000));
      } catch (e) {
        this.pushLog("快进异常: " + String(e));
      }
    },

    onProgressClick(e) {
      if (this.isLive || this.durationMs <= 0) return;
      // Falcon 事件坐标与屏幕一致，需要减去进度条左偏移：页面 padding24 + meta/控制区上方高度
      // 简化：按 x 比例近似映射（真机微调）
      const x = e && typeof e.x === "number" ? e.x : 0;
      const startX = 24; // 页面左右 padding
      const trackW = this.trackWidthEstimate();
      const ratio = Math.max(0, Math.min(1, (x - startX) / trackW));
      const sec = (ratio * this.durationMs) / 1000;
      this.onSeek(sec - this.positionMs / 1000);
    },

    onBack() {
      this.pushLog("返回");
      if (this.$page && this.$page.back) this.$page.back();
      else if (typeof $falcon !== "undefined" && $falcon.exit) $falcon.exit();
    },

    applyStatus(st) {
      const n = normalizeStatusForUI(st);
      this.state = n.state;
      this.positionMs = n.positionMs;
      this.durationMs = n.durationMs;
      if (n.lastError) this.lastError = n.lastError;
      if (n.title) this.title = n.title;
    },

    startPoll() {
      this.stopPoll();
      this.pollTimer = this.$page.setInterval(async () => {
        if (this.state === "playing" || this.state === "paused") {
          try {
            const r = await PlayerService.refresh();
            if (r.res && r.res.status) this.applyStatus(r.res.status);
          } catch (e) { /* 静默 */ }
        }
      }, 500);
    },
    stopPoll() {
      if (this.pollTimer) {
        this.$page.clearInterval(this.pollTimer);
        this.pollTimer = 0;
      }
    },
  },
};

// 服务端 status 的本地归一（与 PlayerService.normalizeStatus 约定一致，但 UI 内联一份避免循环引用）
function normalizeStatusForUI(raw) {
  if (!raw || typeof raw !== "object") return { state: "idle", positionMs: 0, durationMs: 0, lastError: "", title: "" };
  const labels = { 0: "idle", 1: "loading", 2: "playing", 3: "paused", 4: "error" };
  return {
    state: labels[raw.state] || "idle",
    positionMs: raw.positionMs || 0,
    durationMs: raw.durationMs || 0,
    lastError: raw.lastError || "",
    title: raw.title || "",
  };
}
</script>

<style lang="less" scoped>
.page {
  flex: 1;
  flex-direction: column;
  background-color: #101418;
  padding: 24px;
}

.topbar {
  flex-direction: row;
  align-items: center;
  margin-bottom: 16px;
}

.btn-back {
  padding: 12px 20px;
  border-radius: 10px;
  background-color: #37474f;
  margin-right: 16px;
}

.btn-back:active { opacity: 0.6; }

.btn-back-text {
  font-size: 26px;
  color: #ffffff;
}

.title-wrap {
  flex: 1;
  flex-direction: column;
}

.title {
  font-size: 40px;
  color: #ffffff;
  font-weight: bold;
}

.subtitle {
  font-size: 22px;
  color: #8a94a6;
  margin-top: 4px;
}

.stage {
  height: 320px;
  background-color: #0d1116;
  border-radius: 12px;
  align-items: center;
  justify-content: center;
  margin-bottom: 14px;
}

.stage-text {
  font-size: 26px;
  color: #6a7684;
}

.meta-row {
  flex-direction: row;
  margin-bottom: 8px;
}

.meta-label {
  font-size: 24px;
  color: #8a94a6;
  width: 80px;
}

.meta-value {
  font-size: 24px;
  color: #ffffff;
}

	.state-ok {
  color: #37d67a;
}

.state-err {
  color: #ff5c5c;
}

.progress-wrap {
  margin-top: 6px;
  margin-bottom: 14px;
}

.progress-track {
  height: 16px;
  background-color: #2a333d;
  border-radius: 8px;
  position: relative;
}

.progress-fill {
  height: 16px;
  background-color: #2f81f7;
  border-radius: 8px;
}

.progress-thumb {
  position: absolute;
  top: -4px;
  width: 16px;
  height: 24px;
  background-color: #ffffff;
  border-radius: 8px;
}

.progress-times {
  flex-direction: row;
  justify-content: space-between;
  margin-top: 6px;
}

.time-text {
  font-size: 22px;
  color: #8a94a6;
}

.controls {
  flex-direction: row;
  margin-bottom: 14px;
}

.btn {
  flex: 1;
  margin-right: 10px;
  padding: 18px 8px;
  border-radius: 10px;
  align-items: center;
  justify-content: center;
}

.btn:last-child { margin-right: 0; }

.btn-play { background-color: #2f81f7; }
.btn-pause { background-color: #f0ad4e; }
.btn-stop { background-color: #c62828; }
.btn-refresh { background-color: #37474f; }

.btn-text {
  font-size: 26px;
  color: #ffffff;
}

.btn:active { opacity: 0.6; }

.list-head {
  margin-bottom: 8px;
}

.list-head-text {
  font-size: 24px;
  color: #8a94a6;
}

.src-scroller {
  height: 220px;
  background-color: #1b222b;
  border-radius: 12px;
  margin-bottom: 12px;
}

.src-item {
  padding: 14px;
  border-bottom-width: 1px;
  border-bottom-color: #2a333d;
}

.src-item-selected { background-color: #233446; }

.src-label {
  font-size: 26px;
  color: #e6edf3;
}

.src-note {
  font-size: 20px;
  color: #7d8899;
  margin-top: 4px;
}

.log-scroller {
  height: 140px;
  background-color: #0d1116;
  border-radius: 10px;
  padding: 8px;
}

.log-line {
  font-size: 18px;
  color: #6dd5a0;
  margin-bottom: 4px;
}
</style>