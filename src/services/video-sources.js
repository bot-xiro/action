// 在线测试视频源清单：公开可直链的 HLS / MPEG-TS / MP4 测试流。
// 设备 GStreamer 有 hlsdemux + tsdemux + qtdemux + souphttpsrc。
// 注意：优先使用 HTTP（非 TLS）源，规避设备 libsoup 的 CA/TLS 兼容差异。

export const VIDEO_SOURCES = [
  {
    id: 'hls-apple',
    label: 'HLS · Apple bipbop (1080p)',
    type: 'hls',
    url: 'https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8',
    note: 'Apple 官方 HLS 测试流（HTTPS）',
  },
  {
    id: 'hls-mux',
    label: 'HLS · Mux test stream',
    type: 'hls',
    url: 'https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8',
    note: 'Mux 公开 HLS 测试流（HTTPS）',
  },
  {
    id: 'ts-akamai',
    label: 'MPEG-TS · Akamai live',
    type: 'ts',
    url: 'https://cph-p2p-msl.akamaized.net/hls/live/2000341/test/master.m3u8',
    note: 'Akamai 直播 TS 流（HTTPS）',
  },
  {
    id: 'mp4-local',
    label: 'MP4 · 大厂样本片段',
    type: 'mp4',
    url: 'https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4',
    note: 'BigBuckBunny 公开 MP4（HTTPS，H.264）',
  },
];

// 默认选中的测试源
export const DEFAULT_SOURCE_ID = 'hls-mux';

export default VIDEO_SOURCES;