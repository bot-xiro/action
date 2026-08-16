# bilistream - 独立 Bilibili 在线播放原生模块

与系统播放器以及自研 gstplayer 完全解耦的新模块。

## 构建

```powershell
# 从项目根目录运行
.\\native\\bilistream\\build.ps1
```

产物：native/bilistream/build/libjsapi_bilistream.so

## JS API

```javascript
import { biliStream } from 'bilistream'

await biliStream.open({
  uri: 'https://...durl...',
  audio: true,
  pos_x: 0, pos_y: 0, pos_w: 960, pos_h: 266
})

biliStream.start()
biliStream.pause()
biliStream.resume()
biliStream.seek(positionMs)
biliStream.close()

biliStream.getDuration()
biliStream.getPosition()

biliStream.stateChanged.on(state => {
  // 'playing' / 'paused' / 'ended' / 'error:...'
})
```

## 注意

- SDK 头文件通过 CMake fallback 自动引用 native/gstplayer/third_party/iot-miniapp-sdk/
- 交叉编译命令同 native/gstplayer/
- 前端需在 app.json 的 options.external 添加 bilistream
