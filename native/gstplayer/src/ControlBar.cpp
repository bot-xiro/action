#include "ControlBar.h"

#include <cmath>
#include <cstring>

#include <dlfcn.h>
#include <syslog.h>

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <fontconfig/fontconfig.h>

namespace gstplayer {

// 【文字渲染修复 2026-08-14】设备 fontconfig 默认配置不含任何字体路径
// （框架字体在 /etc/miniapp/resources/fonts/，仅框架自用）→ cairo toy 字体
// "sans" 解析不到字形，文字不渲染。这里把设备自带字体显式注册进 FcConfig，
// 使 cairo 可用系统字体绘制（时间/返回/错误文案）。
static void ensureFonts()
{
    static bool done = false;
    if (done) return;
    done = true;
    const char* fonts[] = {
        "/etc/miniapp/resources/fonts/NotoSansSC-Regular.otf",
        "/etc/miniapp/resources/fonts/HarmonyOS_Sans_SC_Regular.ttf",
        "/etc/miniapp/resources/fonts/NotoSans-Regular.ttf",
        nullptr
    };
    FcConfig* cfg = FcConfigGetCurrent();
    for (int i = 0; fonts[i]; i++) {
        if (FcConfigAppFontAddFile(cfg, (const FcChar8*)fonts[i]) == FcTrue) {
            syslog(LOG_LOCAL7 | LOG_ERR, "[gstplayer] font registered: %s", fonts[i]);
        } else {
            syslog(LOG_LOCAL7 | LOG_ERR, "[gstplayer] font register failed: %s", fonts[i]);
        }
    }
}

// 【崩溃红线 2026-08-14】本 .so 交叉编译自 x86 宿主，直接链接的 cairo/gdk-pixbuf
// 符号在设备上采用 lazy 解析：若 miniapp 进程未加载对应动态库，首次调用即
// SIGSEGV（真机实证：open() 在 audio-sink 之后无任何日志即整进程闪退）。
// 必须在此处主动 dlopen + RTLD_GLOBAL（把符号注入全局作用域供本 .so 懒解析），
// 与前期 libdrm dlopen 处理同款（见 GstPlayer.cpp setPlaneZpos 注释）。
// 任一失败 → 返回 false，调用方安全降级（无悬浮栏，视频照常播放）。
static bool ensureOverlayLibs()
{
    static bool inited = false;
    static bool ok = false;
    if (inited) return ok;
    inited = true;
    const char* libs[] = {
        "libcairo.so.2", "libcairo.so",
        "libgdk_pixbuf-2.0.so.0", "libgdk_pixbuf-2.0.so",
        "libfontconfig.so.1", "libfontconfig.so",
        nullptr
    };
    for (int i = 0; libs[i]; i++) {
        void* h = dlopen(libs[i], RTLD_NOW | RTLD_GLOBAL);
        if (h) {
            syslog(LOG_LOCAL7 | LOG_ERR, "[gstplayer] dlopen %s ok", libs[i]);
        } else {
            syslog(LOG_LOCAL7 | LOG_ERR, "[gstplayer] dlopen %s failed: %s", libs[i], dlerror());
        }
    }
    // cairo 与 gdk-pixbuf 必须可用；fontconfig 缺失时仅文字不渲染（图标仍可画）
    void* cairoLib = dlopen("libcairo.so.2", RTLD_NOW | RTLD_GLOBAL);
    if (!cairoLib) cairoLib = dlopen("libcairo.so", RTLD_NOW | RTLD_GLOBAL);
    void* gdkLib = dlopen("libgdk_pixbuf-2.0.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (!gdkLib) gdkLib = dlopen("libgdk_pixbuf-2.0.so", RTLD_NOW | RTLD_GLOBAL);
    ok = (cairoLib != nullptr) && (gdkLib != nullptr);
    syslog(LOG_LOCAL7 | LOG_ERR, "[gstplayer] overlay libs %s (cairo=%p gdk=%p)",
        ok ? "OK" : "UNAVAILABLE", (void*)cairoLib, (void*)gdkLib);
    return ok;
}

// 供 GstPlayer 模块加载时提前调用（见 createGstPlayer）
bool ensureOverlayLibsGlobal()
{
    return ensureOverlayLibs();
}

// 布局常量（用户空间 960×266；JS 侧 player.vue 保持同一组常量做命中测试）
// 【2026-08-14 放大】按键与 seek 轨道加大（用户需求）：轨道 14→22 高、
// 按钮行 26→36 高、图标 7→10、播放圆底 13→17；按钮水平命中区同步放宽。
namespace bargeom {
const double W = 960.0;   // 用户空间宽
const double H = 266.0;   // 用户空间高
const double BAR_TOP = 190.0;   // 控制栏顶（y 190~266）
const double TITLE_H = 40.0;    // 顶部标题条高（y 0~40）
const double TRACK_Y = 196.0;   // 进度轨道 y（196~218）
const double TRACK_H = 22.0;
const double TRACK_L = 24.0;    // 轨道左缘
const double TRACK_R = 936.0;   // 轨道右缘
const double BTN_Y = 226.0;     // 按钮行 y（226~262）
const double BTN_H = 36.0;
const double BACK_L = 24.0;     // 返回按钮
const double BACK_R = 140.0;
const double SBK_L = 330.0;     // 快退 10s
const double SBK_R = 430.0;
const double PLAY_L = 448.0;    // 播放/暂停
const double PLAY_R = 512.0;
const double SFW_L = 530.0;     // 快进 10s
const double SFW_R = 630.0;
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

bool ControlBar::init(int canvasW, int canvasH, bool portrait, const char* kind)
{
    if (canvasW <= 0 || canvasH <= 0) return false;
    if (!ensureOverlayLibs()) return false;   // 库不可用 → 降级（无悬浮栏）
    ensureFonts();                            // 注册设备字体（时间/文字渲染）
    portrait_ = portrait;
    titleMode_ = (kind && strcmp(kind, "title") == 0);

    // 只渲染"条带"（非整幅画布），减小逐帧合成面积：
    //   bar（底部控制栏）：portrait 条带=画布 x∈[BAR_TOP,W] y 全幅；landscape 对称
    //   title（顶部标题条，2026-08-14）：portrait 条带=画布 x∈[0,TITLE_H] y 全幅；landscape 对称
    int regionTop = titleMode_ ? static_cast<int>(bargeom::TITLE_H) : static_cast<int>(bargeom::BAR_TOP);
    if (portrait_) {
        w_ = titleMode_ ? regionTop : canvasW - regionTop;
        h_ = canvasH;
        stripOffX_ = titleMode_ ? 0 : regionTop;
        stripOffY_ = 0;
    } else {
        w_ = canvasW;
        h_ = titleMode_ ? regionTop : canvasH - regionTop;
        stripOffX_ = 0;
        stripOffY_ = titleMode_ ? 0 : regionTop;
    }
    if (w_ <= 0 || h_ <= 0) return false;

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

// ================= 用户空间 → 画布坐标映射 =================
// portrait: canvas (cx, cy) = (userY, userX)；landscape: canvas = 用户原坐标

double ControlBar::mapX(double ux, double uy) const
{
    return portrait_ ? uy : ux;
}

double ControlBar::mapY(double ux, double uy) const
{
    return portrait_ ? ux : uy;
}

// 用户矩形 (uy, ux, uh, uw) → 画布
void ControlBar::uRect(cairo_t* cr, double uy, double ux, double uh, double uw) const
{
    if (portrait_) {
        cairo_rectangle(cr, uy, ux, uh, uw);
    } else {
        cairo_rectangle(cr, ux, uy, uw, uh);
    }
}

void ControlBar::uRoundRect(cairo_t* cr, double uy, double ux, double uh, double uw, double r) const
{
    double x = mapX(ux, uy), y = mapY(ux, uy);
    double w = portrait_ ? uh : uw;
    double h = portrait_ ? uw : uh;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
    cairo_arc(cr, x + w - r, y + r, r, 1.5 * M_PI, 2 * M_PI);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, 0.5 * M_PI);
    cairo_arc(cr, x + r, y + h - r, r, 0.5 * M_PI, M_PI);
    cairo_close_path(cr);
}

void ControlBar::uText(cairo_t* cr, double uy, double ux, const char* text,
                       double size, double r, double g, double b) const
{
    cairo_save(cr);
    if (portrait_) {
        // 【文字渲染修复 2026-08-14】屏幕映射：画布 cx→屏幕 y、cy→屏幕 x。
        // 文字需用【反射】(x,y)→(y,x)（与图标 mapX/mapY 同款）才能正常显示。
        // 注意：不能用 cairo_set_matrix（会整体替换 CTM、丢掉条带 translate(-190,0)，
        // 导致文字画到条带缓冲区外被裁剪——真机"无时间显示"根因），
        // 必须 cairo_translate 定位 + cairo_transform 后乘纯交换矩阵。
        cairo_translate(cr, uy, ux);
        cairo_matrix_t swap;
        cairo_matrix_init(&swap, 0, 1, 1, 0, 0, 0);
        cairo_transform(cr, &swap);
    } else {
        cairo_translate(cr, ux, uy);
    }
    cairo_set_font_size(cr, size);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_move_to(cr, 0, 0);
    cairo_show_text(cr, text);
    cairo_restore(cr);
}

void ControlBar::uCircle(cairo_t* cr, double uy, double ux, double r) const
{
    cairo_arc(cr, mapX(ux, uy), mapY(ux, uy), r, 0, 2 * M_PI);
}

void ControlBar::uTriangle(cairo_t* cr, double y1, double x1, double y2, double x2,
                           double y3, double x3) const
{
    cairo_move_to(cr, mapX(x1, y1), mapY(x1, y1));
    cairo_line_to(cr, mapX(x2, y2), mapY(x2, y2));
    cairo_line_to(cr, mapX(x3, y3), mapY(x3, y3));
    cairo_close_path(cr);
}

void ControlBar::uLine(cairo_t* cr, double y1, double x1, double y2, double x2) const
{
    cairo_move_to(cr, mapX(x1, y1), mapY(x1, y1));
    cairo_line_to(cr, mapX(x2, y2), mapY(x2, y2));
}

// ---- 进度轨道（2026-08-14 放大：轨道更高、圆点更大）----
void ControlBar::drawTrack(cairo_t* cr, double pct)
{
    cairo_set_source_rgba(cr, 1, 1, 1, 0.3);
    uRoundRect(cr, bargeom::TRACK_Y, bargeom::TRACK_L,
               bargeom::TRACK_H, bargeom::TRACK_R - bargeom::TRACK_L, 10);
    cairo_fill(cr);
    if (pct <= 0) return;
    double fw = (bargeom::TRACK_R - bargeom::TRACK_L) * pct;
    if (fw < 2) fw = 2;
    cairo_set_source_rgb(cr, bargeom::PINK_R, bargeom::PINK_G, bargeom::PINK_B);
    uRoundRect(cr, bargeom::TRACK_Y, bargeom::TRACK_L, bargeom::TRACK_H, fw, 10);
    cairo_fill(cr);
    // 圆点（白色，中心在填充右端）
    uCircle(cr, bargeom::TRACK_Y + bargeom::TRACK_H / 2, bargeom::TRACK_L + fw, 11);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_fill(cr);
}

// ---- 播放/暂停图标（中心 userY=cy, userX=cx）----
void ControlBar::drawPlayIcon(cairo_t* cr, double cy, double cx, double s)
{
    uTriangle(cr, cy, cx + s, cy - s, cx - s, cy + s, cx - s);
}

void ControlBar::drawPauseIcon(cairo_t* cr, double cy, double cx, double s)
{
    double w = s * 0.42;
    uRect(cr, cy - s, cx - s * 0.9, 2 * s, w);
    uRect(cr, cy - s, cx + s * 0.9 - w, 2 * s, w);
}

// ---- 快进/快退（双三角；2026-08-14 修正方向：apex 朝前）----
void ControlBar::drawSeekIcon(cairo_t* cr, double cy, double cx, double s, bool forward)
{
    for (int i = 0; i < 2; i++) {
        double off = (i - 0.5) * s * 1.1;   // 用户 x 偏移
        if (forward) {
            // ⏩ apex 在右 (+0.5s)，底边在左
            uTriangle(cr, cy, cx + off + s * 0.5, cy - s, cx + off - s * 0.5, cy + s, cx + off - s * 0.5);
        } else {
            // ⏪ apex 在左 (-0.5s)，底边在右
            uTriangle(cr, cy, cx + off - s * 0.5, cy - s, cx + off + s * 0.5, cy + s, cx + off + s * 0.5);
        }
    }
}

// ---- 返回箭头 ‹ ----
void ControlBar::drawBackIcon(cairo_t* cr, double cy, double cx, double s)
{
    cairo_set_line_width(cr, 3);
    uLine(cr, cy - s, cx + s * 0.5, cy, cx - s * 0.5);
    uLine(cr, cy, cx - s * 0.5, cy + s, cx + s * 0.5);
    cairo_stroke(cr);
}

// ---- 错误：红圈 + 叉 ----
void ControlBar::drawErrorIcon(cairo_t* cr, double cy, double cx, double s)
{
    cairo_set_source_rgb(cr, 0.9, 0.25, 0.25);
    cairo_set_line_width(cr, 3);
    uCircle(cr, cy, cx, s);
    cairo_stroke(cr);
    uLine(cr, cy - s * 0.5, cx - s * 0.5, cy + s * 0.5, cx + s * 0.5);
    uLine(cr, cy - s * 0.5, cx + s * 0.5, cy + s * 0.5, cx - s * 0.5);
    cairo_stroke(cr);
}

// 顶部标题条（2026-08-14）：半透明黑底 + 视频标题（左对齐，超长被条带边界裁剪）
void ControlBar::drawTitle(cairo_t* cr)
{
    cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
    uRect(cr, 0, 0, bargeom::TITLE_H, bargeom::W);
    cairo_fill(cr);
    if (title_.empty()) return;
    uText(cr, bargeom::TITLE_H - 14, 16, title_.c_str(), 20, 1, 1, 1);
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
    double tw = strlen(tbuf) * 9.5;   // 粗估 9.5px/字符 @18px
    uText(cr, bargeom::BTN_Y + 8, bargeom::TRACK_R - tw, tbuf, 18, 1, 1, 1);

    // 返回按钮（图标 + 文字）
    cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
    drawBackIcon(cr, bargeom::BTN_Y + bargeom::BTN_H / 2 - 2, bargeom::BACK_L + 14, 10);
    uText(cr, bargeom::BTN_Y + 8, bargeom::BACK_L + 32, "返回", 16, 1, 1, 1);

    // 快退/快进
    cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
    drawSeekIcon(cr, bargeom::BTN_Y + bargeom::BTN_H / 2, (bargeom::SBK_L + bargeom::SBK_R) / 2, 10, false);
    drawSeekIcon(cr, bargeom::BTN_Y + bargeom::BTN_H / 2, (bargeom::SFW_L + bargeom::SFW_R) / 2, 10, true);

    // 播放/暂停（粉色圆底 + 图标）
    double pcy = bargeom::BTN_Y + bargeom::BTN_H / 2;
    double pcx = (bargeom::PLAY_L + bargeom::PLAY_R) / 2;
    cairo_set_source_rgb(cr, bargeom::PINK_R, bargeom::PINK_G, bargeom::PINK_B);
    uCircle(cr, pcy, pcx, 17);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    if (playing_ && !ended_) {
        drawPauseIcon(cr, pcy, pcx, 10);
        cairo_fill(cr);
    } else {
        drawPlayIcon(cr, pcy, pcx, 10);
        cairo_fill(cr);
    }

    if (error_) {
        drawErrorIcon(cr, 140, bargeom::W / 2 - 40, 20);
        uText(cr, 116, bargeom::W / 2 - 10, "播放错误", 16, 1, 0.9, 0.9);
    } else if (ended_) {
        uText(cr, bargeom::BTN_Y + 8, bargeom::TRACK_R - tw - 90, "已结束", 16, 1, 1, 1);
    }
}

GdkPixbuf* ControlBar::render(bool visible, bool playing, bool ended, bool error,
                              double posMs, double durMs, const char* title)
{
    if (!ready_) return nullptr;
    visible_ = visible;
    playing_ = playing;
    ended_ = ended;
    error_ = error;
    posMs_ = posMs;
    durMs_ = durMs;
    if (title) title_ = title;
    else title_.clear();

    cairo_t* cr = cairo_create(surf_);
    // 画布坐标 → 条带缓冲坐标（条带位于画布 (stripOffX_, stripOffY_)）
    cairo_translate(cr, -stripOffX_, -stripOffY_);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    if (visible_) {
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        if (titleMode_) drawTitle(cr);
        else drawBar(cr);
    }
    cairo_destroy(cr);

    // 【行序翻转 2026-08-14 真机实证】gdkpixbufoverlay 合成时 pixbuf 行序反转
    // （半红半蓝按长度分半的测试条"左蓝右红"证实：row 0 显示在屏幕右端）。
    // 渲染后把缓冲行序倒置，抵消该镜像；视频帧不受影响（无 overlay 参与）。
    for (int y = 0; y < h_ / 2; y++) {
        unsigned char* rowA = data_ + static_cast<size_t>(y) * stride_;
        unsigned char* rowB = data_ + static_cast<size_t>(h_ - 1 - y) * stride_;
        for (int x = 0; x < stride_; x++) {
            unsigned char t = rowA[x];
            rowA[x] = rowB[x];
            rowB[x] = t;
        }
    }

    // 【字节序修复 2026-08-14】cairo ARGB32 内存序为 BGRA，gdk-pixbuf 按 RGBA 读取
    // → 原地交换 R/B，否则颜色红蓝互换（粉色图标变蓝、文字错色）
    for (int y = 0; y < h_; y++) {
        unsigned char* row = data_ + static_cast<size_t>(y) * stride_;
        for (int x = 0; x < w_; x++) {
            unsigned char* px = row + x * 4;
            unsigned char t = px[0];
            px[0] = px[2];
            px[2] = t;
        }
    }

    return gdk_pixbuf_new_from_data(data_, GDK_COLORSPACE_RGB, TRUE, 8,
                                    w_, h_, stride_, nullptr, nullptr);
}

}  // namespace gstplayer
