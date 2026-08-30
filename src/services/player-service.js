// 播放器服务适配器：把 native pvplayer 模块能力检测、入参转换、返回归一化收口。
// 真机走 `import { PlayerModule } from 'pvplayer'`；无 native 时（模拟器）走 mock。
// 业务页面只消费稳定接口，不在模板里堆 if(native) else(mock)。

let PlayerModuleImpl = null;
let resolved = false;

function resolveNative() {
  if (resolved) return PlayerModuleImpl;
  resolved = true;
  try {
    // eslint-disable-next-line
    const m = require('pvplayer');
    if (m && m.PlayerModule) PlayerModuleImpl = m.PlayerModule;
  } catch (e) {
    PlayerModuleImpl = null;
  }
  return PlayerModuleImpl;
}

// 归一化状态枚举 -> 稳定字符串
export const STATE_LABELS = {
  0: 'idle',
  1: 'loading',
  2: 'playing',
  3: 'paused',
  4: 'error',
};

export function normalizeStatus(raw) {
  if (!raw || typeof raw !== 'object') {
    return { state: 'idle', positionMs: 0, durationMs: 0, lastError: '', title: '' };
  }
  const stateNum = typeof raw.state === 'number' ? raw.state : 0;
  return {
    state: STATE_LABELS[stateNum] || 'unknown',
    positionMs: raw.positionMs != null ? raw.positionMs : 0,
    durationMs: raw.durationMs != null ? raw.durationMs : 0,
    lastError: raw.lastError || '',
    title: raw.title || '',
  };
}

// mock 状态（模拟器/无 native 用）
function mockState(patch) {
  PlayerService._mockStatus = Object.assign({}, PlayerService._mockStatus, patch || {});
  return PlayerService._mockStatus;
}

// 统一播放器接口（native / mock 都实现同一形状）
export class PlayerService {
  static hasNative() {
    return !!resolveNative();
  }

  static getVersion() {
    const m = resolveNative();
    if (m && m.getVersion) return m.getVersion();
    return 'mock-1.0.0';
  }

  static getStatus() {
    const m = resolveNative();
    if (m && m.getStatus) return normalizeStatus(m.getStatus());
    return normalizeStatus(mockState());
  }

  static validate(url, type) {
    const m = resolveNative();
    if (m && m.validate) return m.validate({ url, type });
    const ok = /^https?:\/\//.test(url) && ['hls', 'ts', 'mp4'].indexOf(type) >= 0;
    return { ok, reason: ok ? '' : 'invalid url or type', isLive: type === 'ts' };
  }

  static async load(url, type) {
    const m = resolveNative();
    if (m && m.loadP) return { source: 'native', res: await m.loadP({ url, type }) };
    mockState({ state: 3, title: url, durationMs: 64000, positionMs: 0 });
    return { source: 'mock', res: { success: true, status: mockState({ title: url }) } };
  }

  static async play() {
    const m = resolveNative();
    if (m && m.playP) return { source: 'native', res: await m.playP() };
    return { source: 'mock', res: { success: true, status: mockState({ state: 2 }) } };
  }

  static async pause() {
    const m = resolveNative();
    if (m && m.pauseP) return { source: 'native', res: await m.pauseP() };
    return { source: 'mock', res: { success: true, status: mockState({ state: 3 }) } };
  }

  static async resume() {
    const m = resolveNative();
    if (m && m.resumeP) return { source: 'native', res: await m.resumeP() };
    return { source: 'mock', res: { success: true, status: mockState({ state: 2 }) } };
  }

  static async stop() {
    const m = resolveNative();
    if (m && m.stopP) return { source: 'native', res: await m.stopP() };
    return { source: 'mock', res: { success: true, status: mockState({ state: 0, positionMs: 0, title: '' }) } };
  }

  static async seek(seconds) {
    const m = resolveNative();
    if (m && m.seekP) return { source: 'native', res: await m.seekP({ seconds }) };
    mockState({ positionMs: seconds * 1000 });
    return { source: 'mock', res: { success: true, status: mockState() } };
  }

  static async refresh() {
    const m = resolveNative();
    if (m && m.refreshP) return { source: 'native', res: await m.refreshP() };
    return { source: 'mock', res: { success: true, status: mockState() } };
  }

  // 订阅 native 事件：返回取消函数
  static subscribe(cb) {
    const m = resolveNative();
    if (m && m.on) {
      m.on('pvevent', cb);
      return () => m.off && m.off('pvevent', cb);
    }
    return () => {};
  }
}

PlayerService._mockStatus = { state: 0, positionMs: 0, durationMs: 0, lastError: '', title: '' };

export default PlayerService;