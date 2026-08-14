#include "ControlBar.h"

#include <cmath>
#include <cstring>

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

namespace gstplayer {

// 布局常量（用户空间 960×266；JS 侧 player.vue 保持同一组常量做命中测试）
namespace bargeom {
const double W = 960.0;   // 用户空间宽
const double H = 266.0;   // 用户空间高
const double BAR_TOP = 190.0;   // 控制栏顶（y 190~266）
const double TRACK_Y = 202.0;   // 进度轨道 y
const double TRACK_H = 14.0;
const double TRACK_L = 24.0;    // 轨道左缘
const double TRACK_R = 936.0;   // 轨道右缘
const double BTN_Y = 236.0;     // 按钮行 y
const double BTN_H = 26.0;
const double BACK_L = 24.0;     // 返回按钮
const double BACK_R = 110.0;
const double SBK_L = 350.0;     // 快退 10s
const double SBK_R = 410.0;
const double PLAY_L = 458.0;    // 播放/暂停
const double PLAY_R = 502.0;
const double SFW_L = 550.0;     // 快进 10s
const double SFW_R = 610.0;
const double PINK_R = 0.984;
const double PINK_G = 0.447;
const double PINK_B = 0.6;
}  // namespace bargeom

ControlBar::ControlBar() = default;
ControlBar::~ControlBar()
{
    if (surf_) cairo_surface_destroy(surf_);
    if (data_) delete[] data_;
}

bool ControlBar::init(int canvasW, int canvasH, bool portrait)
{
    if (canvasW <= 0 || canvasH <= 0) return false;
    w_ = canvasW;
    h_ = canvasH;
    portrait_ = portrait;
    stride_ = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w_);
    data_ = new unsigned char[static_cast<size_t>(stride_) * h_];
    std::memset(data_, 0, static_cast<size_t>(stride_) * h_);
    surf_ = cairo_image_surface_create_for_data(data_, CAIRO_FORMAT_ARGB32, w_, h_, stride_);
    if (cairo_surface_status(surf_) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf_);
        surf_ = nullptr;
        delete[] data_;
        data_ = nullptr;
        return false;
    }
    ready_ = true;
    return true;
}

// ================= 用户空间绘制助手 =================
// 坐标映射：canvas (cx, cy) = (userY, userX)
// 画布矩形 (x=userY, y=userX, w=userH, h=userW)

// 用户空间矩形：uRect(userY, userX, userH, userW)
static void uRect(cairo_t* cr, double uy, double ux, double uh, double uw)
{
    cairo_rectangle(cr, uy, ux, uh, uw);
}

// 用户空间圆角矩形（画布空间圆角半径与用户一致）
static void uRoundRect(cairo_t* cr, double uy, double ux, double uh, double uw, double r)
{
    double x = uy, y = ux, w = uh, h = uw;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
    cairo_arc(cr, x + w - r, y + r, r, 1.5 * M_PI, 2 * M_PI);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, 0.5 * M_PI);
    cairo_arc(cr, x + r, y + h - r, r, 0.5 * M_PI, M_PI);
    cairo_close_path(cr);
}

// 用户空间文本：基线起点 (userY, userX)，文字沿用户 x 正向
static void uText(cairo_t* cr, double uy, double ux, const char* text,
                  double size, double r, double g, double b)
{
    cairo_save(cr);
    cairo_translate(cr, uy, ux);
    cairo_rotate(cr, M_PI / 2);
    cairo_set_font_size(cr, size);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_move_to(cr, 0, 0);
    cairo_show_text(cr, text);
    cairo_restore(cr);
}

// 用户空间圆：圆心 (userY, userX)，半径 r
static void uCircle(cairo_t* cr, double uy, double ux, double r)
{
    cairo_arc(cr, uy, ux, r, 0, 2 * M_PI);
}

// 用户空间填充三角形：顶点 (userY, userX)
static void uTriangle(cairo_t* cr, double y1, double x1, double y2, double x2, double y3, double x3)
{
    cairo_move_to(cr, y1, x1);
    cairo_line_to(cr, y2, x2);
    cairo_line_to(cr, y3, x3);
    cairo_close_path(cr);
}

// 用户空间直线
static void uLine(cairo_t* cr, double y1, double x1, double y2, double x2)
{
    cairo_move_to(cr, y1, x1);
    cairo_line_to(cr, y2, x2);
}

// ---- 进度轨道 ----
static void drawTrack(cairo_t* cr, double pct)
{
    cairo_set_source_rgba(cr, 1, 1, 1, 0.3);
    uRoundRect(cr, bargeom::TRACK_Y, bargeom::TRACK_L,
               bargeom::TRACK_H, bargeom::TRACK_R - bargeom::TRACK_L, 7);
    cairo_fill(cr);
    if (pct <= 0) return;
    double fw = (bargeom::TRACK_R - bargeom::TRACK_L) * pct;
    if (fw < 2) fw = 2;
    cairo_set_source_rgb(cr, bargeom::PINK_R, bargeom::PINK_G, bargeom::PINK_B);
    uRoundRect(cr, bargeom::TRACK_Y, bargeom::TRACK_L, bargeom::TRACK_H, fw, 7);
    cairo_fill(cr);
    // 圆点（白色，中心在填充右端）
    uCircle(cr, bargeom::TRACK_Y + bargeom::TRACK_H / 2, bargeom::TRACK_L + fw, 8);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_fill(cr);
}

// ---- 播放/暂停图标（中心 userY=cy, userX=cx）----
static void drawPlayIcon(cairo_t* cr, double cy, double cx, double s)
{
    uTriangle(cr, cy, cx + s, cy - s, cx - s, cy + s, cx - s);
}

static void drawPauseIcon(cairo_t* cr, double cy, double cx, double s)
{
    double w = s * 0.42;
    uRect(cr, cy - s, cx - s * 0.9, 2 * s, w);
    uRect(cr, cy - s, cx + s * 0.9 - w, 2 * s, w);
}

// ---- 快进/快退（双三角）----
static void drawSeekIcon(cairo_t* cr, double cy, double cx, double s, bool forward)
{
    for (int i = 0; i < 2; i++) {
        double off = (i - 0.5) * s * 1.1;   // 用户 x 偏移
        if (forward) {
            uTriangle(cr, cy - s, cx + off + s * 0.5, cy, cx + off - s * 0.5, cy + s, cx + off + s * 0.5);
        } else {
            uTriangle(cr, cy - s, cx + off - s * 0.5, cy, cx + off + s * 0.5, cy + s, cx + off - s * 0.5);
        }
    }
}

// ---- 返回箭头 ‹ ----
static void drawBackIcon(cairo_t* cr, double cy, double cx, double s)
{
    cairo_set_line_width(cr, 3);
    uLine(cr, cy - s, cx + s * 0.5, cy, cx - s * 0.5);
    uLine(cr, cy, cx - s * 0.5, cy + s, cx + s * 0.5);
    cairo_stroke(cr);
}

// ---- 错误：红圈 + 叉 ----
static void drawErrorIcon(cairo_t* cr, double cy, double cx, double s)
{
    cairo_set_source_rgb(cr, 0.9, 0.25, 0.25);
    cairo_set_line_width(cr, 3);
    uCircle(cr, cy, cx, s);
    cairo_stroke(cr);
    uLine(cr, cy - s * 0.5, cx - s * 0.5, cy + s * 0.5, cx + s * 0.5);
    uLine(cr, cy - s * 0.5, cx + s * 0.5, cy + s * 0.5, cx - s * 0.5);
    cairo_stroke(cr);
}

void ControlBar::drawBar(cairo_t* cr)
{
    // 背景条（半透明黑）
    cairo_set_source_rgba(cr, 0, 0, 0, 0.55);
    uRect(cr, bargeom::BAR_TOP, 0, bargeom::H - bargeom::BAR_TOP, bargeom::W);
    cairo_fill(cr);

    // 进度轨道
    double pct = 0;
    if (durMs_ > 0) {
        pct = posMs_ / durMs_;
        if (pct < 0) pct = 0;
        if (pct > 1) pct = 1;
    }
    drawTrack(cr, pct);

    // 时间文本（右下角，右对齐）
    char tbuf[64];
    auto fmt = [](char* out, size_t n, double ms) {
        int total = static_cast<int>(ms / 1000.0);
        int h = total / 3600;
        int m = (total % 3600) / 60;
        int s = total % 60;
        if (h > 0)
            snprintf(out, n, "%d:%02d:%02d", h, m, s);
        else
            snprintf(out, n, "%02d:%02d", m, s);
    };
    fmt(tbuf, sizeof(tbuf), posMs_);
    size_t plen = strlen(tbuf);
    tbuf[plen] = ' '; tbuf[plen + 1] = '/'; tbuf[plen + 2] = ' ';
    fmt(tbuf + plen + 3, sizeof(tbuf) - plen - 3, durMs_);
    double tw = strlen(tbuf) * 8.5;   // 粗估 8.5px/字符 @16px
    uText(cr, bargeom::BTN_Y + 6, bargeom::TRACK_R - tw, tbuf, 16, 1, 1, 1);

    // 返回按钮（图标 + 文字）
    cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
    drawBackIcon(cr, bargeom::BTN_Y + bargeom::BTN_H / 2 - 2, bargeom::BACK_L + 12, 7);
    uText(cr, bargeom::BTN_Y + 6, bargeom::BACK_L + 26, "返回", 14, 1, 1, 1);

    // 快退/快进
    cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
    drawSeekIcon(cr, bargeom::BTN_Y + bargeom::BTN_H / 2, (bargeom::SBK_L + bargeom::SBK_R) / 2, 7, false);
    drawSeekIcon(cr, bargeom::BTN_Y + bargeom::BTN_H / 2, (bargeom::SFW_L + bargeom::SFW_R) / 2, 7, true);

    // 播放/暂停（粉色圆底 + 图标）
    double pcy = bargeom::BTN_Y + bargeom::BTN_H / 2;
    double pcx = (bargeom::PLAY_L + bargeom::PLAY_R) / 2;
    cairo_set_source_rgb(cr, bargeom::PINK_R, bargeom::PINK_G, bargeom::PINK_B);
    uCircle(cr, pcy, pcx, 13);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    if (playing_ && !ended_) {
        drawPauseIcon(cr, pcy, pcx, 7);
        cairo_fill(cr);
    } else {
        drawPlayIcon(cr, pcy, pcx, 7);
        cairo_fill(cr);
    }

    if (error_) {
        drawErrorIcon(cr, 140, bargeom::W / 2 - 40, 20);
        uText(cr, 116, bargeom::W / 2 - 10, "播放错误", 16, 1, 0.9, 0.9);
    } else if (ended_) {
        uText(cr, bargeom::BTN_Y + 6, bargeom::TRACK_R - tw - 90, "已结束", 14, 1, 1, 1);
    }
}

GdkPixbuf* ControlBar::render(bool visible, bool playing, bool ended, bool error,
                              double posMs, double durMs)
{
    if (!ready_) return nullptr;
    visible_ = visible;
    playing_ = playing;
    ended_ = ended;
    error_ = error;
    posMs_ = posMs;
    durMs_ = durMs;

    cairo_t* cr = cairo_create(surf_);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    if (visible_) {
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        drawBar(cr);
    }
    cairo_destroy(cr);

    return gdk_pixbuf_new_from_data(data_, GDK_COLORSPACE_RGB, TRUE, 8,
                                    w_, h_, stride_, nullptr, nullptr);
}

}  // namespace gstplayer
