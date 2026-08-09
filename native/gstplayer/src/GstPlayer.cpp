#include "GstPlayer.h"

#include <sstream>
#include <syslog.h>

using namespace JQUTIL_NS;

namespace gstplayer {

#define PLAYER_LOG(fmt, ...) syslog(LOG_ERR, "[gstplayer] " fmt, ##__VA_ARGS__)

namespace {

// 一次性 GStreamer 初始化（在模块加载时即完成，避免首次 open 卡在插件扫描上）
void ensureGstInit()
{
    static bool inited = false;
    if (!inited) {
        PLAYER_LOG("gst_init enter");
        gst_init(nullptr, nullptr);
        PLAYER_LOG("gst_init done");
        inited = true;
    }
}

// ---- JS 参数解析辅助 ----
std::string jsGetString(JSContext* ctx, JSValueConst obj, const char* key)
{
    JSValue v = JS_GetPropertyStr(ctx, obj, key);
    std::string result;
    if (JS_IsString(v)) {
        const char* s = JS_ToCString(ctx, v);
        if (s) result = s;
        JS_FreeCString(ctx, s);
    }
    JS_FreeValue(ctx, v);
    return result;
}

bool jsGetBool(JSContext* ctx, JSValueConst obj, const char* key, bool def)
{
    JSValue v = JS_GetPropertyStr(ctx, obj, key);
    bool result = def;
    if (JS_IsBool(v)) result = JS_ToBool(ctx, v);
    JS_FreeValue(ctx, v);
    return result;
}

int jsGetInt(JSContext* ctx, JSValueConst obj, const char* key, int def)
{
    JSValue v = JS_GetPropertyStr(ctx, obj, key);
    int result = def;
    if (JS_IsNumber(v)) {
        int32_t i = 0;
        if (JS_ToInt32(ctx, &i, v) == 0) result = i;
    }
    JS_FreeValue(ctx, v);
    return result;
}

// ---- KMSSINK 双平面坐标系换算（核心红线，勿改） ----
//
// 两套坐标系的来源（详见 docs/X6PRO_ENV.txt 与 docs/PROJECT_SUMMARY.md 8.1 节）：
//   1) UI 逻辑坐标（JS 层/WebView）：weston 合成器对物理屏做了 rotate-90 变换，
//      逻辑全屏为 960×480，播放页视口占据其上部的 960×266 区域。
//      前端调用 gstPlayer.open({ pos_x, pos_y, pos_w, pos_h }) 传入的即此坐标系中
//      的视频矩形（本页面全屏: <0, 0, 960, 266>）。
//   2) 物理 CRTC 坐标（kmssink render-rectangle 需要）：未旋转的硬件原生坐标，
//      宽 480 × 高 960（竖屏）。
//      注意：kmssink 直出物理 plane，不经 weston 的 rotate-90 变换，
//      所以把逻辑矩形直接填进 render-rectangle 必然错位（旧 bug：宽度 960
//      超出物理宽 480 被钳制，画面贴到右下角）。
//
// 【真机校准 2026-08-09 第二轮】经 DRM debugfs 物证（plane[76] crtc-pos 实测）
// 与用户两次真机反馈交叉验证：
//   1) "显示一半视频" = kmssink 默认 force-aspect-ratio=true，720x1280 视频
//      等比 fit 进 266 宽矩形，实际只渲染 266x472（crtc-pos=266x472+0+244），
//      上下留 244px 黑边。修复：设置 force-aspect-ratio=false 拉伸铺满。
//   2) "翻转错了" = videoflip 必须用 method=3（顺时针），v2 改 method=1 错误。
//   3) 坐标：v2 用逆时针公式（左缘竖条）是 180° 错位，恢复顺时针公式
//      （右缘竖条）。物理 x' = kLogicFullH - (ly+lh) 的映射与 UI 实际方位吻合。
//
// rotate-90 的像素映射（本实现以真机验证为准，勿再按 Wayland 文档猜测方向）：
//     物理 x' = kLogicFullH - (y + h)     ← 逻辑 y 轴反向映射到物理 x 轴
//     物理 y' = x                          ← 逻辑 x 轴映射到物理 y 轴
//     物理宽  = 逻辑高 h
//     物理高  = 逻辑宽 w
//
// 代入本方案（pos_x=0, pos_y=0, pos_w=960, pos_h=266）：
//     physX = 480 - (0 + 266) = 214
//     physY = 0
//     physW = 266
//     physH = 960
// 物理矩形 <214, 0, 266, 960>：覆盖物理屏右侧 266px 宽的全高竖条，
// 用户视角（物理屏逆时针转回横屏观看）恰好是铺满 960×266 的完整画面。
struct KmsRect { int x, y, w, h; };

static KmsRect logicToCrtc(int lx, int ly, int lw, int lh)
{
    // kLogicFullH = weston 逻辑全屏高度 = 物理屏宽度 480（旋转基准，恒定不变）
    constexpr int kLogicFullH = 480;
    KmsRect r;
    r.x = kLogicFullH - (ly + lh);
    r.y = lx;
    r.w = lh;
    r.h = lw;
    return r;
}

}  // namespace

GstPlayer::GstPlayer() = default;

GstPlayer::~GstPlayer()
{
    teardown();
}

// ---- JS 方法 ----

void GstPlayer::open(JQFunctionInfo& info)
{
    JSContext* ctx = info.GetContext();
    if (info.Length() < 1 || !JS_IsObject(info[0])) {
        info.GetReturnValue().ThrowTypeError("open: expected options object");
        return;
    }

    JSValueConst opt = info[0];
    std::string uri = jsGetString(ctx, opt, "uri");
    if (uri.empty()) {
        // 兼容历史字段 filename
        uri = jsGetString(ctx, opt, "filename");
    }
    if (uri.empty()) {
        info.GetReturnValue().ThrowTypeError("open: uri required");
        return;
    }

    bool audio = jsGetBool(ctx, opt, "audio", true);
    int posX = jsGetInt(ctx, opt, "pos_x", 0);
    int posY = jsGetInt(ctx, opt, "pos_y", 0);
    int posW = jsGetInt(ctx, opt, "pos_w", 0);
    int posH = jsGetInt(ctx, opt, "pos_h", 0);
    std::string fill = jsGetString(ctx, opt, "fill");  // "fit"/"crop"/"stretch"，空=fit

#ifdef KMSSINK_TEST
    // KMSSINK 模式：前端传的是 weston UI 逻辑坐标（rotate-90 之后，播放页视口
    // 960×266 内的矩形，如 <0, 0, 960, 266>），而 kmssink 的 render-rectangle
    // 要的是物理 CRTC 坐标（480×960 竖屏）。必须在 C++ 层完成逻辑→物理换算，
    // 否则矩形宽度 960 超出物理屏宽 480，被 kmssink 钳制到右半屏 → 画面错位。
    // 换算公式见匿名命名空间 logicToCrtc()（rotate-90 顺时针映射 + 常量 480）。
    if (posW > 0 && posH > 0) {
        KmsRect p = logicToCrtc(posX, posY, posW, posH);
        PLAYER_LOG("kmss rect LOGIC(%d,%d,%d,%d) -> PHYS(%d,%d,%d,%d)",
            posX, posY, posW, posH, p.x, p.y, p.w, p.h);
        posX = p.x;
        posY = p.y;
        posW = p.w;
        posH = p.h;
    }
#endif

    std::ostringstream rect;
    if (posW > 0 && posH > 0) {
        // GstValueArray 字符串格式：<x, y, width, height>
        rect << "<" << posX << ", " << posY << ", " << posW << ", " << posH << ">";
    }

    // 关闭旧管线
    teardown();

    PLAYER_LOG("open uri=%s audio=%d rect=%s fill=%s", uri.c_str(), audio ? 1 : 0, rect.str().c_str(), fill.c_str());
    bool ok = buildPipeline(uri, audio, rect.str(), fill);
    PLAYER_LOG("open buildPipeline ret=%d", ok ? 1 : 0);
    if (!ok) {
        teardown();
        info.GetReturnValue().ThrowInternalError("open: pipeline build failed");
        return;
    }

    info.GetReturnValue().Set(true);
}

void GstPlayer::start(JQFunctionInfo& info)
{
    PLAYER_LOG("start enter");
    if (!pipeline_) {
        info.GetReturnValue().ThrowInternalError("start: not opened");
        return;
    }
    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    PLAYER_LOG("start set PLAYING");
    info.GetReturnValue().Set(true);
}

void GstPlayer::pause(JQFunctionInfo& info)
{
    if (!pipeline_) {
        info.GetReturnValue().ThrowInternalError("pause: not opened");
        return;
    }
    gst_element_set_state(pipeline_, GST_STATE_PAUSED);
    info.GetReturnValue().Set(true);
}

void GstPlayer::resume(JQFunctionInfo& info)
{
    if (!pipeline_) {
        info.GetReturnValue().ThrowInternalError("resume: not opened");
        return;
    }
    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    info.GetReturnValue().Set(true);
}

void GstPlayer::close(JQFunctionInfo& info)
{
    teardown();
    info.GetReturnValue().Set(true);
}

// ---- 管线构建（手动管线：souphttpsrc → queue → decodebin → 音视频分流）----

bool GstPlayer::buildPipeline(const std::string& uri, bool audio, const std::string& rect, const std::string& fill)
{
    ensureGstInit();

    // 不用 playbin：其内部 souphttpsrc 无法获取（bin 遍历不可见、child proxy 返回 NULL），
    // 而 B站 CDN 必须设置浏览器 UA + Referer 才能过防盗链。
    // 手动构建：souphttpsrc（直接创建并设头）→ queue → decodebin（pad-added 按媒体类型分流）
    pipeline_ = gst_pipeline_new("gstplayer-pipeline");
    if (!pipeline_) {
        PLAYER_LOG("pipeline factory failed");
        return false;
    }

    GstElement* src = gst_element_factory_make("souphttpsrc", "src");
    GstElement* queue = gst_element_factory_make("queue", "qsrc");
    decodebin_ = gst_element_factory_make("decodebin", "decode");
    if (!src || !queue || !decodebin_) {
        PLAYER_LOG("factory failed src=%d queue=%d decodebin=%d",
            src ? 1 : 0, queue ? 1 : 0, decodebin_ ? 1 : 0);
        if (src) gst_object_unref(src);
        if (queue) gst_object_unref(queue);
        if (decodebin_) gst_object_unref(decodebin_);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        decodebin_ = nullptr;
        return false;
    }

    g_object_set(src, "location", uri.c_str(), nullptr);
    g_object_set(src, "user-agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        nullptr);
    // 网络超时：CDN 拉流卡住时快速失败，避免 open/预滚无限挂起
    // （timeout 单位秒，0=无限；15s 无数据即报错，正常播放不受影响）
    g_object_set(src, "timeout", 15, nullptr);
    g_object_set(src, "retries", 0, nullptr);
    // 直接构造 GstStructure 设置 extra-headers（gst_util_set_object_arg 字符串解析
    // 在设备上不可靠，可能解析失败导致 Referer 未生效）
    GstStructure* hdrs = gst_structure_new_empty("headers");
    gst_structure_set(hdrs, "referer", G_TYPE_STRING, "https://www.bilibili.com/", nullptr);
    g_object_set(src, "extra-headers", hdrs, nullptr);
    gst_structure_free(hdrs);
    // 回读验证属性是否真正设置成功
    GstStructure* back = nullptr;
    g_object_get(src, "extra-headers", &back, nullptr);
    if (back) {
        gchar* hs = gst_structure_to_string(back);
        PLAYER_LOG("extra-headers back: %s", hs ? hs : "(null)");
        if (hs) g_free(hs);
        gst_structure_free(back);
    } else {
        PLAYER_LOG("extra-headers back: (null) - SET FAILED");
    }
    PLAYER_LOG("souphttpsrc created, UA+Referer set");

    // 视频输出：kmssink（KMS overlay 双平面直出，Lilo 方案；临时实验分支，
    // 由 -DKMSSINK_TEST=ON 启用，默认仍走 waylandsink）
#ifdef KMSSINK_TEST
    // 视频输出：kmssink → Rockchip DRM Overlay 平面，KMS 双平面直出
    // （UI 走 weston 主平面，视频走硬件叠加平面，两者由 DRM 硬件合成）
    videoSink_ = gst_element_factory_make("kmssink", "vsink");
    if (!videoSink_) {
        PLAYER_LOG("kmssink factory failed");
        teardown();
        return false;
    }
    // 关键：必须显式指定 driver-name=rockchip，否则 kmssink 驱动探测卡死
    g_object_set(videoSink_, "driver-name", "rockchip", nullptr);
    // 双平面架构指定视频 Overlay 平面。
    // 真机 modetest 平面普查（2026-08-09）：可用平面为 54(primary z=1) /
    // 76(overlay z=2) / 90(overlay z=3) / 104(overlay z=4) —— 不存在 plane 75！
    // 之前用 75 导致 kmssink 报 "Could not find a plane for crtc" 打开失败。
    // 76 与历史实验中实测可用的 overlay 平面一致，选用之。
    g_object_set(videoSink_, "plane-id", 76, nullptr);
    // 层级控制：设置 zpos 让视频 Overlay 平面处于较低层级，UI 主平面（weston）
    // 必须在视频之上，控制栏才能悬浮显示且不被视频遮挡。
    // 越低越靠下；0 为 primary 同级值，若目标 plane 的 zpos 本就高于主平面，
    // 此设置可让硬件按“视频在下、UI 在上”合成。
    GParamSpec* zpspec = g_object_class_find_property(G_OBJECT_GET_CLASS(videoSink_), "zpos");
    if (zpspec) {
        g_object_set(videoSink_, "zpos", 0, nullptr);
        PLAYER_LOG("kmssink zpos set: 0");
    } else {
        PLAYER_LOG("kmssink has no zpos property");
    }
    // 不启用 vsync 等待，避免与 weston 的 DRM 提交互相等待
    gboolean skip = true;
    GParamSpec* skipPspec = g_object_class_find_property(G_OBJECT_GET_CLASS(videoSink_), "skip-vsync");
    if (skipPspec) g_object_set(videoSink_, "skip-vsync", skip, nullptr);
    // 【真机物证 2026-08-09】force-aspect-ratio 默认 true：720x1280 视频等比
    // fit 进 266 宽 render-rectangle 后只渲染出 266x472（crtc-pos 实测），
    // 上下 244px 黑边 = 用户看到的"只显示一半视频"。
    // 必须设为 false 强制拉伸铺满整个 render-rectangle（266x960 全高）。
    GParamSpec* arPspec = g_object_class_find_property(G_OBJECT_GET_CLASS(videoSink_), "force-aspect-ratio");
    if (arPspec) {
        g_object_set(videoSink_, "force-aspect-ratio", FALSE, nullptr);
        PLAYER_LOG("kmssink force-aspect-ratio=false (stretch fill)");
    }
    PLAYER_LOG("kmssink created (plane-id=76 driver=rockchip)");
    if (!rect.empty()) {
        // 致命红线：render-rectangle 是 GstValueArray of gint（Write only），
        // g_object_set 传 C 字符串 → GLib 类型不匹配 abort 崩溃！
        // 必须用 gst_util_set_object_arg 解析 "<x, y, w, h>" 字符串为值数组。
        // 此处 rect 已是换算后的物理 CRTC 坐标（如 "<214, 0, 266, 960>"）。
        GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(videoSink_), "render-rectangle");
        if (pspec) {
            gst_util_set_object_arg(G_OBJECT(videoSink_), "render-rectangle", rect.c_str());
            PLAYER_LOG("kmssink render-rectangle set: %s", rect.c_str());
        } else {
            PLAYER_LOG("kmssink has no render-rectangle property");
        }
    }
#else
    videoSink_ = gst_element_factory_make("waylandsink", "vsink");
    if (!videoSink_) {
        PLAYER_LOG("waylandsink factory failed");
        teardown();
        return false;
    }
    PLAYER_LOG("waylandsink created");
    // 填充模式：fit=等比留黑边（默认）、crop=等比裁剪填满、stretch=拉伸变形
    if (!fill.empty() && (fill == "crop" || fill == "stretch")) {
        GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(videoSink_), "fill-mode");
        if (pspec) {
            g_object_set(videoSink_, "fill-mode", fill == "crop" ? 2 : 0, nullptr);
            PLAYER_LOG("fill-mode set: %s", fill.c_str());
        }
    }
    if (!rect.empty()) {
        GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(videoSink_), "render-rectangle");
        if (pspec) {
            // render-rectangle 是 GstValueArray of gint（Write only），
            // g_object_set 传 C 字符串会触发 GLib 类型不匹配 critical/abort 崩溃；
            // gst_util_set_object_arg 会把 "<x, y, w, h>" 字符串解析为值数组
            gst_util_set_object_arg(G_OBJECT(videoSink_), "render-rectangle", rect.c_str());
            PLAYER_LOG("render-rectangle set: %s", rect.c_str());
        }
    }
#endif

    // 音频输出：alsasink（device=speaker）；audio=false 时用 fakesink 静音
    audioSink_ = audio
        ? gst_element_factory_make("alsasink", "asink")
        : gst_element_factory_make("fakesink", "asink");
    if (audioSink_ && audio) {
        g_object_set(audioSink_, "device", "speaker", nullptr);
    }
    PLAYER_LOG("audio-sink created: %s", audio ? "alsasink" : "fakesink");

    // 组装：src → queue → decodebin（动态 pad 分流到 videoSink_/audioSink_）
    gst_bin_add_many(GST_BIN(pipeline_), src, queue, decodebin_, videoSink_, audioSink_, nullptr);
    if (!gst_element_link_many(src, queue, decodebin_, nullptr)) {
        PLAYER_LOG("link src->decodebin failed");
        teardown();
        return false;
    }
    g_signal_connect(decodebin_, "pad-added", G_CALLBACK(GstPlayer::decodebinPadAddedCb), this);
    PLAYER_LOG("pipeline linked");

    // bus 轮询线程（不用 GLib 主循环，规避设备 GLib 差异）
    stopping_ = false;
    busThread_ = std::thread(&GstPlayer::busLoop, this);
    PLAYER_LOG("bus thread started");

    // open 到 PAUSED 预滚，由 start() 切换 PLAYING
    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PAUSED);
    PLAYER_LOG("set_state PAUSED ret=%d", ret);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        teardown();
        return false;
    }

    return true;
}

// decodebin pad-added 静态回调 → 转成员函数（userdata=this）
void GstPlayer::decodebinPadAddedCb(GstElement* element, GstPad* pad, gpointer userdata)
{
    GstPlayer* self = static_cast<GstPlayer*>(userdata);
    self->onDecodebinPadAdded(pad);
}

void GstPlayer::onDecodebinPadAdded(GstPad* pad)
{
    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps) {
        PLAYER_LOG("pad-added: no caps");
        return;
    }
    const GstStructure* s = gst_caps_get_structure(caps, 0);
    const gchar* media = s ? gst_structure_get_name(s) : nullptr;  // "video/x-h264" / "audio/mpeg"
    PLAYER_LOG("pad-added: %s", media ? media : "?");
    GstElement* sink = nullptr;
    if (media && g_str_has_prefix(media, "video/")) {
        // 竖屏视频（高>宽）顺时针旋转 90°，横过来利用超宽屏空间：
        // 动态插入 videoflip（纯旋转不改格式/尺寸，不会引发协商失败），解码 pad → videoflip → waylandsink
        gint w = 0, h = 0;
        gst_structure_get_int(s, "width", &w);
        gst_structure_get_int(s, "height", &h);
#ifdef KMSSINK_TEST
        // kmssink 直出物理 plane（480x960 竖屏），不经过 weston 的 rotate 变换；
        // UI 是 rotate-90（顺时针）后的逻辑画面，所以所有视频内容都必须旋转
        // 与 UI 同向才不至于画面颠倒。
        // method=3 即 CLOCKWISE（顺时针 90°），与 weston rotate-90 同向。
        // 注：v2 曾改用 method=1（逆时针）+ 逆时针坐标公式，真机反馈"翻转错了"，
        // 2026-08-09 第二轮校准：恢复 method=3（顺时针）+ 顺时针坐标公式。
        if (true) {
#else
        if (h > w) {
#endif
            GstElement* flip = gst_element_factory_make("videoflip", "vflip");
            if (flip) {
                gst_bin_add(GST_BIN(pipeline_), flip);
                if (gst_element_link(flip, videoSink_)) {
                    // method=3 即 CLOCKWISE（GstVideoFlipMethod 枚举：
                    // 0=none, 1=counterclockwise, 2=rotate-180, 3=clockwise）。
                    // kmssink 走物理 plane，须与 weston rotate-90（顺时针）同向；
                    // 2026-08-09 真机第二轮校准：恢复 method=3。
                    g_object_set(flip, "method", 3, nullptr);
                    // 动态添加的元素必须同步到父管线状态，否则数据流被阻塞
                    gst_element_sync_state_with_parent(flip);
                    videoFlip_ = flip;
                    sink = flip;
                    PLAYER_LOG("video %dx%d -> videoflip inserted (cw=m3, kmssink all-flip)", w, h);
                } else {
                    gst_bin_remove(GST_BIN(pipeline_), flip);
                    gst_object_unref(flip);
                    sink = videoSink_;
                    PLAYER_LOG("videoflip link failed, keep as-is");
                }
            } else {
                sink = videoSink_;
                PLAYER_LOG("videoflip factory failed, keep as-is");
            }
#ifdef KMSSINK_TEST
        } else {
            sink = videoSink_;
            PLAYER_LOG("video %dx%d -> direct kmssink", w, h);
        }
#else
        } else {
            sink = videoSink_;
            PLAYER_LOG("video landscape %dx%d -> direct waylandsink", w, h);
        }
#endif
    } else if (media && g_str_has_prefix(media, "audio/")) {
        sink = audioSink_;
    }
    if (sink) {
        GstPad* sinkPad = gst_element_get_static_pad(sink, "sink");
        if (sinkPad) {
            GstPadLinkReturn r = gst_pad_link(pad, sinkPad);
            PLAYER_LOG("pad link ret=%d", r);
            gst_object_unref(sinkPad);
        } else {
            PLAYER_LOG("sink pad not found for %s", media ? media : "?");
        }
    } else {
        PLAYER_LOG("no sink for %s", media ? media : "?");
    }
    gst_caps_unref(caps);
}

void GstPlayer::teardown()
{
    PLAYER_LOG("teardown enter");
    stopping_ = true;
    if (busThread_.joinable()) {
        busThread_.join();
    }
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
    }
    pipeline_ = nullptr;
    decodebin_ = nullptr;
    videoSink_ = nullptr;
    audioSink_ = nullptr;
    videoFlip_ = nullptr;
    PLAYER_LOG("teardown done");
}

// ---- bus 消息循环（独立线程）----

void GstPlayer::busLoop()
{
    PLAYER_LOG("busLoop enter");
    GstBus* bus = gst_element_get_bus(pipeline_);
    if (!bus) {
        PLAYER_LOG("busLoop get_bus failed");
        return;
    }

    while (!stopping_) {
        GstMessage* msg = gst_bus_timed_pop_filtered(
            bus, 100 * GST_MSECOND,
            static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR | GST_MESSAGE_STATE_CHANGED));
        if (!msg) continue;

        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            PLAYER_LOG("bus EOS");
            emitState("ended");
            break;
        case GST_MESSAGE_ERROR: {
            gchar* debug = nullptr;
            GError* err = nullptr;
            gst_message_parse_error(msg, &err, &debug);
            std::string emsg = err && err->message ? err->message : "unknown";
            PLAYER_LOG("bus ERROR: %s", emsg.c_str());
            if (debug) g_free(debug);
            if (err) g_error_free(err);
            emitState("error:" + emsg);
            break;
        }
        case GST_MESSAGE_STATE_CHANGED: {
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline_)) {
                GstState oldState, newState;
                gst_message_parse_state_changed(msg, &oldState, &newState, nullptr);
                PLAYER_LOG("bus state %s -> %s",
                    gst_element_state_get_name(oldState), gst_element_state_get_name(newState));
                if (newState == GST_STATE_PLAYING) {
                    emitState("playing");
                } else if (newState == GST_STATE_PAUSED) {
                    emitState("paused");
                }
            }
            break;
        }
        default:
            break;
        }
        gst_message_unref(msg);
    }

    gst_object_unref(bus);
    PLAYER_LOG("busLoop exit");
}

void GstPlayer::emitState(const std::string& state)
{
    // JQSignal 线程安全：非本线程 emit 自动 post 到 JS 线程
    stateChanged.emit(state);
}

// ---- 模块导出 ----

static JSValue createGstPlayer(JQModuleEnv* env)
{
    // 预热 GStreamer：模块加载（app 启动 import gstplayer）即完成 gst_init，
    // 把插件扫描开销从首次 open 播放路径上移走，缩短首帧延迟
    ensureGstInit();

    JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "gstPlayer");
    tpl->InstanceTemplate()->setObjectCreator([]() {
        static GstPlayer* player = []() {
            GstPlayer* instance = new GstPlayer();
            instance->REF();
            return instance;
        }();
        return player;
    });

    tpl->SetProtoMethod("open", &GstPlayer::open);
    tpl->SetProtoMethod("start", &GstPlayer::start);
    tpl->SetProtoMethod("pause", &GstPlayer::pause);
    tpl->SetProtoMethod("resume", &GstPlayer::resume);
    tpl->SetProtoMethod("close", &GstPlayer::close);

    // JS 侧: gstPlayer.stateChanged.on(cb) / .off(cb)
    tpl->InstanceTemplate()->Set("stateChanged", &GstPlayer::stateChanged);

    return tpl->CallConstructor();
}

void gstplayer_init(JQModuleEnv* env)
{
    env->setModuleExport("gstPlayer", createGstPlayer(env));
}

}  // namespace gstplayer
