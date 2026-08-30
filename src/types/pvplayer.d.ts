// ambient declaration: `import { PlayerModule } from 'pvplayer'`
// 名字与 JSAPI.cpp 的 setModuleExport / registerCModuleLoader 一致。
// jsapi 名 == .so 去掉 libjsapi_ 和 .so == JS import 名。

declare class PlayerModuleClass {
  // sync 状态
  static getVersion(): string;
  static getStatus(): {
    state: number;
    positionMs: number;
    durationMs: number;
    lastError: string;
    title: string;
  };
  static validate(arg: { url: string; type: string }): {
    ok: boolean;
    reason: string;
    isLive: boolean;
  };

  // Promise 控制
  static loadP(arg: { url: string; type: string }): Promise<any>;
  static playP(): Promise<any>;
  static pauseP(): Promise<any>;
  static resumeP(): Promise<any>;
  static stopP(): Promise<any>;
  static seekP(arg: { seconds: number }): Promise<any>;
  static refreshP(): Promise<any>;

  // 事件订阅
  static on(event: 'pvevent', cb: (json: string) => void): void;
  static off?(event: 'pvevent', cb: (json: string) => void): void;
}

export const PlayerModule: PlayerModuleClass;
export {};