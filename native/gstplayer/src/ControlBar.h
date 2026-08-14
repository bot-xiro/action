#pragma once

// 悬浮控制栏：cairo 渲染到 ARGB 缓冲区 → GdkPixbuf → gdkpixbufoverlay 合入视频帧。
//
// 坐标系：用户看到的播放页为 960×266（横向）。KMS 模式下渲染画布为物理方向
// （如 266×960 竖条，与 kmssink render-rectangle 1:1），用户空间需 90° 映射：
//   canvas (cx, cy) = (userY, userX)   （物理 px = 逻辑 ly，物理 py = 逻辑 lx）
// 本类提供 uRect/uText 等"用户空间"绘制助手，内部自动完成旋转/交换。
// waylandsink 模式下画布即 960×266（横向），无需旋转（portrait=false）。

#include <cstdint>

typedef struct _cairo_surface cairo_surface_t;
typedef struct _cairo cairo_t;
typedef struct _GdkPixbuf GdkPixbuf;

namespace gstplayer {

class ControlBar {
public:
    ControlBar();
    ~ControlBar();

    // canvasW/canvasH：渲染画布尺寸（= sink render-rectangle 尺寸）。
    // portrait：画布为竖条（KMS 物理方向）时 true，绘制自动旋转。
    bool init(int canvasW, int canvasH, bool portrait);

    // 更新状态并重新渲染；返回当前 GdkPixbuf*（新引用，调用方负责 unref）。
    // 未 init 时返回 nullptr。
    GdkPixbuf* render(bool visible, bool playing, bool ended, bool error,
                      double posMs, double durMs);

private:
    void drawBar(cairo_t* cr);

    cairo_surface_t* surf_ = nullptr;
    unsigned char* data_ = nullptr;
    int w_ = 0;
    int h_ = 0;
    int stride_ = 0;
    bool portrait_ = false;
    bool ready_ = false;

    bool visible_ = true;
    bool playing_ = false;
    bool ended_ = false;
    bool error_ = false;
    double posMs_ = 0.0;
    double durMs_ = 0.0;
};

}  // namespace gstplayer
