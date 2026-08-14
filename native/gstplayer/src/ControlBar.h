#pragma once

// 悬浮控制栏：cairo 渲染到 ARGB 缓冲区 → GdkPixbuf → gdkpixbufoverlay 合入视频帧。
//
// 坐标系：用户看到的播放页为 960×266（横向）。KMS 模式下渲染画布为物理方向
// （如 266×960 竖条，与 kmssink render-rectangle 1:1），用户空间需 90° 映射：
//   canvas (cx, cy) = (userY, userX)   （物理 px = 逻辑 ly，物理 py = 逻辑 lx）
// waylandsink 模式下画布即 960×266（横向），无需旋转（portrait=false）。
//
// 性能：只渲染"控制栏条带"（非整幅画布），gdkpixbufoverlay 用 offset/width/height
// 定位到画布底部，逐帧合成面积最小化（2026-08-14 掉帧修复）。
//
// 字节序：cairo ARGB32 内存序为 BGRA，gdk-pixbuf 按 RGBA 读取 → 渲染后原地
// R/B 交换（否则红蓝互换、图标颜色全错，2026-08-14 修复）。

#include <cstdint>

typedef struct _cairo_surface cairo_surface_t;
typedef struct _cairo cairo_t;
typedef struct _GdkPixbuf GdkPixbuf;

namespace gstplayer {

// 模块加载时调用：dlopen cairo/gdk-pixbuf/fontconfig（RTLD_GLOBAL），
// 供本 .so 与 gstgdkpixbuf 插件懒解析；失败返回 false（悬浮栏降级）。
bool ensureOverlayLibsGlobal();

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

    // 条带在画布中的偏移/尺寸（gdkpixbufoverlay offset-x/offset-y/overlay-width/overlay-height）
    int stripOffsetX() const { return stripOffX_; }
    int stripOffsetY() const { return stripOffY_; }
    int stripWidth() const { return w_; }
    int stripHeight() const { return h_; }

private:
    // 用户空间 → 画布坐标映射（portrait 时 90° 交换）
    double mapX(double ux, double uy) const;
    double mapY(double ux, double uy) const;
    void uRect(cairo_t* cr, double uy, double ux, double uh, double uw) const;
    void uRoundRect(cairo_t* cr, double uy, double ux, double uh, double uw, double r) const;
    void uText(cairo_t* cr, double uy, double ux, const char* text,
               double size, double r, double g, double b) const;
    void uCircle(cairo_t* cr, double uy, double ux, double r) const;
    void uTriangle(cairo_t* cr, double y1, double x1, double y2, double x2,
                   double y3, double x3) const;
    void uLine(cairo_t* cr, double y1, double x1, double y2, double x2) const;
    void drawTrack(cairo_t* cr, double pct);
    void drawPlayIcon(cairo_t* cr, double cy, double cx, double s);
    void drawPauseIcon(cairo_t* cr, double cy, double cx, double s);
    void drawSeekIcon(cairo_t* cr, double cy, double cx, double s, bool forward);
    void drawBackIcon(cairo_t* cr, double cy, double cx, double s);
    void drawErrorIcon(cairo_t* cr, double cy, double cx, double s);
    void drawBar(cairo_t* cr);

    cairo_surface_t* surf_ = nullptr;
    unsigned char* data_ = nullptr;
    int w_ = 0;              // 条带宽（画布坐标）
    int h_ = 0;              // 条带高（画布坐标）
    int stride_ = 0;
    int stripOffX_ = 0;      // 条带在画布中的偏移
    int stripOffY_ = 0;
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
