#include "BiliStreamPlayer.h"
#include <cstdio>
#include <sstream>
#include <syslog.h>
#include <cstring>

#ifdef KMSSINK_TEST
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#endif

using namespace JQUTIL_NS;

namespace bilistream {

#define PLAYER_LOG(fmt, ...) syslog(LOG_LOCAL7 | LOG_ERR, "[bilistream] " fmt, ##__VA_ARGS__)

namespace {

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

#ifdef KMSSINK_TEST
static bool setPlaneZpos(int planeId, uint32_t zpos)
{
    void* drmLib = dlopen("libdrm.so.2", RTLD_NOW | RTLD_GLOBAL);
    if (!drmLib) drmLib = dlopen("libdrm.so", RTLD_NOW | RTLD_GLOBAL);
    if (!drmLib) {
        PLAYER_LOG("setPlaneZpos: dlopen libdrm.so failed: %s", dlerror());
        return false;
    }
    typedef drmModeObjectPropertiesPtr (*GetPropsFn)(int, uint32_t, uint32_t);
    typedef void (*FreePropsFn)(drmModeObjectPropertiesPtr);
    typedef drmModePropertyPtr (*GetPropFn)(int, uint32_t);
    typedef void (*FreePropFn)(drmModePropertyPtr);
    typedef int (*SetPropFn)(int, uint32_t, uint32_t, uint32_t, uint64_t);
    typedef void* (*AtomicAllocFn)();
    typedef void (*AtomicFreeFn)(void*);
    typedef int (*AtomicAddFn)(void*, uint32_t, uint32_t, uint64_t);
    typedef int (*AtomicCommitFn)(int, void*, uint32_t, void*);

    auto pGetProps = (GetPropsFn)dlsym(drmLib, "drmModeObjectGetProperties");
    auto pFreeProps = (FreePropsFn)dlsym(drmLib, "drmModeFreeObjectProperties");
    auto pGetProp = (GetPropFn)dlsym(drmLib, "drmModeGetProperty");
    auto pFreeProp = (FreePropFn)dlsym(drmLib, "drmModeFreeProperty");
    auto pSetProp = (SetPropFn)dlsym(drmLib, "drmModeObjectSetProperty");
    auto pAtomicAlloc = (AtomicAllocFn)dlsym(drmLib, "drmModeAtomicAlloc");
    auto pAtomicFree = (AtomicFreeFn)dlsym(drmLib, "drmModeAtomicFree");
    auto pAtomicAdd = (AtomicAddFn)dlsym(drmLib, "drmModeAtomicAddProperty");
    auto pAtomicCommit = (AtomicCommitFn)dlsym(drmLib, "drmModeAtomicCommit");

    if (!pGetProps || !pFreeProps || !pGetProp || !pFreeProp || !pSetProp ||
        !pAtomicAlloc || !pAtomicFree || !pAtomicAdd || !pAtomicCommit) {
        PLAYER_LOG("setPlaneZpos: libdrm symbol missing");
        dlclose(drmLib);
        return false;
    }
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        PLAYER_LOG("setPlaneZpos: open /dev/dri/card0 failed: %s", strerror(errno));
        dlclose(drmLib);
        return false;
    }
    uint32_t zpropId = 0;
    drmModeObjectPropertiesPtr props = pGetProps(fd, planeId, DRM_MODE_OBJECT_PLANE);
    if (props) {
        for (uint32_t i = 0; i < props->count_props && !zpropId; i++) {
            drmModePropertyPtr prop = pGetProp(fd, props->props[i]);
            if (prop && strcmp(prop->name, "zpos") == 0) zpropId = prop->prop_id;
            if (prop) pFreeProp(prop);
        }
        pFreeProps(props);
    }
    if (!zpropId) {
        PLAYER_LOG("setPlaneZpos: zpos property not found on plane %d", planeId);
        close(fd);
        dlclose(drmLib);
        return false;
    }
    void* atomic = pAtomicAlloc();
    if (!atomic) {
        PLAYER_LOG("setPlaneZpos: atomic alloc failed");
        close(fd);
        dlclose(drmLib);
        return false;
    }
    pAtomicAdd(atomic, planeId, zpropId, zpos);
    int rc = pAtomicCommit(fd, atomic, 0, nullptr);
    pAtomicFree(atomic);
    close(fd);
    dlclose(drmLib);
    PLAYER_LOG("setPlaneZpos: plane=%d zpos=%d rc=%d", planeId, zpos, rc);
    return rc == 0;
}
#endif

} // anonymous namespace

BiliStreamPlayer::BiliStreamPlayer() = default;

BiliStreamPlayer::~BiliStreamPlayer()
{
    teardown();
}

void BiliStreamPlayer::open(JQFunctionInfo& info)
{
    JSContext* ctx = info.GetContext();
    if (info.Length() < 1 || !JS_IsObject(info[0])) {
        info.GetReturnValue().ThrowTypeError("open: options object required");
        return;
    }

    JSValue jUri = JS_GetPropertyStr(ctx, info[0], "uri");
    if (!JS_IsString(jUri)) {
        info.GetReturnValue().ThrowTypeError("open: uri (string) required");
        JS_FreeValue(ctx, jUri);
        return;
    }
    size_t len = 0;
    const char* uriC = JS_ToCStringLen(ctx, &len, jUri);
    std::string uri(uriC ? uriC : "", len);
    JS_FreeCString(ctx, uriC);
    JS_FreeValue(ctx, jUri);

    bool audio = true;
    JSValue jAudio = JS_GetPropertyStr(ctx, info[0], "audio");
    if (JS_IsBool(jAudio)) audio = JS_ToBool(ctx, jAudio);
    JS_FreeValue(ctx, jAudio);

    int posX = 0, posY = 0, posW = 960, posH = 266;
    JSValue jx = JS_GetPropertyStr(ctx, info[0], "pos_x");
    if (JS_IsNumber(jx)) JS_ToInt32(ctx, &posX, jx);
    JS_FreeValue(ctx, jx);
    JSValue jy = JS_GetPropertyStr(ctx, info[0], "pos_y");
    if (JS_IsNumber(jy)) JS_ToInt32(ctx, &posY, jy);
    JS_FreeValue(ctx, jy);
    JSValue jw = JS_GetPropertyStr(ctx, info[0], "pos_w");
    if (JS_IsNumber(jw)) JS_ToInt32(ctx, &posW, jw);
    JS_FreeValue(ctx, jw);
    JSValue jh = JS_GetPropertyStr(ctx, info[0], "pos_h");
    if (JS_IsNumber(jh)) JS_ToInt32(ctx, &posH, jh);
    JS_FreeValue(ctx, jh);

    std::stringstream rect;
    rect << "<" << posX << "," << posY << "," << posW << "," << posH << ">";

    // 关闭旧管线（幂等）
    teardown();

    ensureGstInit();

    // 一次性预处理：UI 平面层级（与 gstplayer 相同修复 2026-08-09）
#ifdef KMSSINK_TEST
    static std::atomic<bool> preheated{false};
    if (!preheated.load()) {
        setPlaneZpos(54, 0);
        preheated.store(true);
    }
#endif

    pipeline_ = gst_pipeline_new("bilistream-pipeline");
    if (!pipeline_) {
        PLAYER_LOG("pipeline create failed");
        info.GetReturnValue().Set(false);
        return;
    }

    bool ok = buildPipeline(uri, audio, rect.str(), std::string("fit"), posW, posH);
    info.GetReturnValue().Set(ok);
    if (ok) {
        emitState("opened");
    } else {
        PLAYER_LOG("open: buildPipeline failed for uri=%s", uri.c_str());
    }
}

bool BiliStreamPlayer::buildPipeline(const std::string& uri, bool audio,
                                      const std::string& rect,
                                      const std::string& fill, int canvasW, int canvasH)
{
    // 元素创建
    GstElement* src = gst_element_factory_make("souphttpsrc", "src");
    GstElement* queue = gst_element_factory_make("queue", "qsrc");
    GstElement* demux = gst_element_factory_make("qtdemux", "demux");
    GstElement* vparse = gst_element_factory_make("h264parse", "vparse");
    GstElement* vdec = gst_element_factory_make("mppvideodec", "vdec");
    GstElement* vqueue = gst_element_factory_make("queue", "vqueue");
    GstElement* vconv = gst_element_factory_make("videoconvert", "vconv");
    GstElement* vscale = gst_element_factory_make("videoscale", "vscale");
    GstElement* vcaps = gst_element_factory_make("capsfilter", "vcaps");
    GstElement* vbox = gst_element_factory_make("videobox", "vbox");
    GstElement* vconv2 = gst_element_factory_make("videoconvert", "vconv2");
    GstElement* decodebin = gst_element_factory_make("decodebin", "adec");
    GstElement* aconv = gst_element_factory_make("audioconvert", "aconv");
    GstElement* ares = gst_element_factory_make("audioresample", "ares");
    GstElement* acaps = gst_element_factory_make("capsfilter", "acaps");
    GstElement* aqueue = gst_element_factory_make("queue", "aqueue");
    GstElement* avol = gst_element_factory_make("volume", "avol");
    GstElement* asink = audio ? gst_element_factory_make("alsasink", "asink") : gst_element_factory_make("fakesink", "asink");

#ifdef KMSSINK_TEST
    GstElement* vsink = gst_element_factory_make("kmssink", "vsink");
#else
    GstElement* vsink = gst_element_factory_make("waylandsink", "vsink");
#endif

    if (!src || !queue || !demux || !vparse || !vdec || !vqueue || !vconv ||
        !vscale || !vcaps || !vbox || !vconv2 || !decodebin || !aconv ||
        !ares || !acaps || !aqueue || !avol || !asink || !vsink) {
        PLAYER_LOG("factory failed");
        if (pipeline_) gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

    // souphttpsrc：B 站 CDN 必需 UA + Referer
    const char* kUA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
    GstStructure* hdrs = gst_structure_new_empty("headers");
    gst_structure_set(hdrs, "referer", G_TYPE_STRING, "https://www.bilibili.com/", NULL);
    g_object_set(src, "location", uri.c_str(), NULL);
    g_object_set(src, "extra-headers", hdrs, NULL);
    gst_structure_free(hdrs);
    g_object_set(src, "user-agent", kUA, NULL);
    g_object_set(src, "timeout", 15, NULL);

    // 队列参数
    g_object_set(queue, "max-size-buffers", 200, "max-size-bytes", 16*1024*1024, "max-size-time", 0, NULL);
    g_object_set(vqueue, "max-size-buffers", 4, "max-size-bytes", 2*1024*1024, "max-size-time", 0, NULL);
    g_object_set(aqueue, "max-size-buffers", 200, "max-size-bytes", 2*1024*1024, "max-size-time", 0, NULL);

    // 音频锁 S16LE/2ch/interleaved
    GstCaps* acapsCaps = gst_caps_new_simple("audio/x-raw",
        "format", G_TYPE_STRING, "S16LE",
        "channels", G_TYPE_INT, 2,
        "layout", G_TYPE_STRING, "interleaved", NULL);
    g_object_set(acaps, "caps", acapsCaps, NULL);
    gst_caps_unref(acapsCaps);

    // 音频缓冲扩容（亮屏静音修复）
    if (audio) {
        g_object_set(asink, "buffer-time", (gint64)200000, "latency-time", (gint64)100000, NULL);
    }

#ifdef KMSSINK_TEST
    // kmssink 视频直出 overlay plane 76
    g_object_set(vsink, "driver-name", "rockchip", "plane-id", 76, NULL);
    {
        GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(vsink), "skip-vsync");
        if (pspec) g_object_set(vsink, "skip-vsync", false, NULL);
    }
    {
        GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(vsink), "qos");
        if (pspec) g_object_set(vsink, "qos", true, NULL);
    }
    {
        GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(vsink), "max-lateness");
        if (pspec) g_object_set(vsink, "max-lateness", (gint64)-1, NULL);
    }
    {
        GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(vsink), "sync");
        if (pspec) g_object_set(vsink, "sync", true, NULL);
    }
    if (!rect.empty()) {
        GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(vsink), "render-rectangle");
        if (pspec) {
            gst_util_set_object_arg(G_OBJECT(vsink), "render-rectangle", rect.c_str());
            PLAYER_LOG("kmssink render-rectangle: %s", rect.c_str());
        }
    }
#else
    if (!rect.empty()) {
        GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(vsink), "render-rectangle");
        if (pspec) {
            gst_util_set_object_arg(G_OBJECT(vsink), "render-rectangle", rect.c_str());
        }
    }
#endif

    // 静态链接视频后端
    if (!gst_element_link_many(vparse, vdec, vqueue, vconv, vscale, vcaps, vbox, vconv2, vsink, NULL)) {
        PLAYER_LOG("link video chain failed");
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

    // 静态链接音频后端
    if (!gst_element_link_many(acaps, aqueue, aconv, ares, avol, asink, NULL)) {
        PLAYER_LOG("link audio chain failed");
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

    // 组装进 pipeline
    gst_bin_add_many(GST_BIN(pipeline_), src, queue, demux, vparse, vdec,
        vqueue, vconv, vscale, vcaps, vbox, vconv2, vsink,
        decodebin, aconv, ares, acaps, aqueue, avol, asink, NULL);

    if (!gst_element_link_many(src, queue, demux, NULL)) {
        PLAYER_LOG("link src->demux failed");
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

#ifdef KMSSINK_TEST
    // mppvideodec 硬件旋转：rotation=270 对应顺时针 90°
    {
        GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(vdec), "rotation");
        if (pspec) g_object_set(vdec, "rotation", 270, NULL);
    }
#endif

    // 保存元素引用
    demux_ = demux;
    vparse_ = vparse;
    vdec_ = vdec;
    vqueue_ = vqueue;
    vconvert_ = vconv;
    vscale_ = vscale;
    vcaps_ = vcaps;
    vbox_ = vbox;
    vconvert2_ = vconv2;
    videoSink_ = vsink;
    decodebin_ = decodebin;
    aconvert_ = aconv;
    aresample_ = ares;
    acaps_early_ = acaps;
    aqueue_ = aqueue;
    avolume_ = avol;
    audioSink_ = asink;

    canvasW_ = canvasW;
    canvasH_ = canvasH;
    if (canvasW > 0 && canvasH > 0) {
        applyCanvasContent(1280, 720);
    }

    g_signal_connect(demux, "pad-added", G_CALLBACK(BiliStreamPlayer::qtdemuxPadAddedCb), this);
    g_signal_connect(decodebin, "pad-added", G_CALLBACK(BiliStreamPlayer::decodebinPadAddedCb), this);

    PLAYER_LOG("pipeline linked");
    return true;
}

void BiliStreamPlayer::start(JQFunctionInfo& info)
{
    if (!pipeline_) {
        info.GetReturnValue().Set(false);
        return;
    }
    // 阻塞等待预滚完成（避免 ASYNC 未完成切 PLAYING 导致死锁）
    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        PLAYER_LOG("start: STATE_CHANGE_FAILURE");
        info.GetReturnValue().Set(false);
        return;
    }
    // 等待 bus 报告 PLAYING 或超时
    for (int i = 0; i < 100; ++i) {
        GstState state = GST_STATE_NULL;
        gst_element_get_state(pipeline_, &state, NULL, 100000);
        if (state == GST_STATE_PLAYING) break;
        if (stopping_.load()) break;
    }
    info.GetReturnValue().Set(true);
    emitState("playing");
}

void BiliStreamPlayer::pause(JQFunctionInfo& info)
{
    if (!pipeline_) {
        info.GetReturnValue().Set(false);
        return;
    }
    gst_element_set_state(pipeline_, GST_STATE_PAUSED);
    info.GetReturnValue().Set(true);
    emitState("paused");
}

void BiliStreamPlayer::resume(JQFunctionInfo& info)
{
    if (!pipeline_) {
        info.GetReturnValue().Set(false);
        return;
    }
    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    info.GetReturnValue().Set(true);
    emitState("playing");
}

void BiliStreamPlayer::seek(JQFunctionInfo& info)
{
    if (!pipeline_) {
        info.GetReturnValue().Set(false);
        return;
    }
    if (info.Length() < 1 || !JS_IsNumber(info[0])) {
        info.GetReturnValue().ThrowTypeError("seek: position(ms) required");
        return;
    }
    double ms = 0;
    JS_ToFloat64(info.GetContext(), &ms, info[0]);
    if (ms < 0) ms = 0;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        seeking_ = true;
        seekTargetMs_ = ms;
    }
    gboolean ok = gst_element_seek_simple(pipeline_, GST_FORMAT_TIME, GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), (gint64)(ms * GST_MSECOND));
    info.GetReturnValue().Set(ok == TRUE);
}

void BiliStreamPlayer::close(JQFunctionInfo& info)
{
    teardown();
    info.GetReturnValue().Set(true);
    emitState("closed");
}

void BiliStreamPlayer::getDuration(JQFunctionInfo& info)
{
    if (!pipeline_) {
        info.GetReturnValue().Set(JS_NULL);
        return;
    }
    gint64 dur = 0;
    if (gst_element_query_duration(pipeline_, GST_FORMAT_TIME, &dur)) {
        info.GetReturnValue().Set((double)dur / (double)GST_MSECOND);
    } else {
        info.GetReturnValue().Set(JS_NULL);
    }
}

void BiliStreamPlayer::getPosition(JQFunctionInfo& info)
{
    if (!pipeline_) {
        info.GetReturnValue().Set(JS_NULL);
        return;
    }
    std::lock_guard<std::mutex> lk(mutex_);
    if (seeking_) {
        info.GetReturnValue().Set(seekTargetMs_);
        return;
    }
    gint64 pos = 0;
    if (gst_element_query_position(pipeline_, GST_FORMAT_TIME, &pos)) {
        posMs_ = (double)pos / (double)GST_MSECOND;
        info.GetReturnValue().Set(posMs_);
    } else {
        info.GetReturnValue().Set(JS_NULL);
    }
}

void BiliStreamPlayer::applyCanvasContent(int srcW, int srcH)
{
    if (!vcaps_ || !vbox_ || canvasW_ <= 0 || canvasH_ <= 0) return;
    if (srcW <= 0 || srcH <= 0) return;
    int dispW = srcW, dispH = srcH;
#ifdef KMSSINK_TEST
    dispW = srcH;
    dispH = srcW;
#endif
    double scaleX = static_cast<double>(canvasW_) / dispW;
    double scaleY = static_cast<double>(canvasH_) / dispH;
    double scale = scaleX < scaleY ? scaleX : scaleY;
    int contentW = static_cast<int>(dispW * scale + 0.5);
    int contentH = static_cast<int>(dispH * scale + 0.5);
    if (contentW < 2) contentW = 2;
    if (contentH < 2) contentH = 2;
    int borderL = (canvasW_ - contentW) / 2;
    int borderR = canvasW_ - contentW - borderL;
    int borderT = (canvasH_ - contentH) / 2;
    int borderB = canvasH_ - contentH - borderT;
    if (borderL < 0) borderL = 0;
    if (borderR < 0) borderR = 0;
    if (borderT < 0) borderT = 0;
    if (borderB < 0) borderB = 0;
    GstCaps* caps = gst_caps_new_simple("video/x-raw",
        "width", G_TYPE_INT, contentW,
        "height", G_TYPE_INT, contentH, NULL);
    g_object_set(vcaps_, "caps", caps, NULL);
    gst_caps_unref(caps);
    g_object_set(vbox_,
        "left-border", borderL,
        "right-border", borderR,
        "top-border", borderT,
        "bottom-border", borderB, NULL);
    appliedSrcW_ = srcW;
    appliedSrcH_ = srcH;
    PLAYER_LOG("applyCanvasContent: src=%dx%d content=%dx%d borders=%d,%d,%d,%d canvas=%dx%d",
        srcW, srcH, contentW, contentH, borderL, borderR, borderT, borderB, canvasW_, canvasH_);
}

void BiliStreamPlayer::onQtdemuxPadAdded(GstPad* pad)
{
    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps) {
        PLAYER_LOG("pad-added: no caps");
        return;
    }
    const GstStructure* s = gst_caps_get_structure(caps, 0);
    const gchar* media = s ? gst_structure_get_name(s) : nullptr;
    PLAYER_LOG("pad-added: %s", media ? media : "?");

    GstElement* sink = nullptr;
    if (media && g_str_has_prefix(media, "video/")) {
        gint w = 0, h = 0;
        if (s) {
            gst_structure_get_int(s, "width", &w);
            gst_structure_get_int(s, "height", &h);
        }
        if (w > 0 && h > 0 && (w != appliedSrcW_ || h != appliedSrcH_)) {
            applyCanvasContent(w, h);
        }
        sink = vparse_;
        PLAYER_LOG("video %dx%d -> vparse", w, h);
    } else if (media && g_str_has_prefix(media, "audio/")) {
        sink = acaps_early_;
        PLAYER_LOG("audio -> acaps_early");
    }

    if (sink) {
        GstPad* sinkPad = gst_element_get_static_pad(sink, "sink");
        if (sinkPad) {
            GstPadLinkReturn r = gst_pad_link(pad, sinkPad);
            PLAYER_LOG("qtdemux pad link ret=%d", r);
            if (r != GST_PAD_LINK_OK) {
                if (pendingAudioPad_) gst_object_unref(pendingAudioPad_);
                pendingAudioPad_ = pad;
                gst_object_ref(pendingAudioPad_);
                gst_element_set_state(sink, GST_STATE_READY);
                GstPadLinkReturn r2 = gst_pad_link(pad, sinkPad);
                PLAYER_LOG("qtdemux pad link retry=%d", r2);
                if (r2 == GST_PAD_LINK_OK) {
                    gst_object_unref(pendingAudioPad_);
                    pendingAudioPad_ = nullptr;
                }
            } else {
                if (pendingAudioPad_) {
                    gst_object_unref(pendingAudioPad_);
                    pendingAudioPad_ = nullptr;
                }
            }
            gst_object_unref(sinkPad);
        }
    }
    gst_caps_unref(caps);
}

void BiliStreamPlayer::onDecodebinPadAdded(GstPad* pad)
{
    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps) {
        PLAYER_LOG("decodebin pad-added: no caps");
        return;
    }
    const GstStructure* s = gst_caps_get_structure(caps, 0);
    const gchar* media = s ? gst_structure_get_name(s) : nullptr;
    PLAYER_LOG("decodebin pad-added: %s", media ? media : "?");

    GstElement* sink = nullptr;
    if (media && g_str_has_prefix(media, "audio/")) {
        sink = acaps_early_;
        PLAYER_LOG("audio raw -> acaps_early");
    }

    if (sink) {
        GstPad* sinkPad = gst_element_get_static_pad(sink, "sink");
        if (sinkPad) {
            GstPadLinkReturn r = gst_pad_link(pad, sinkPad);
            PLAYER_LOG("decodebin pad link ret=%d", r);
            if (r != GST_PAD_LINK_OK) {
                if (pendingAudioPad_) gst_object_unref(pendingAudioPad_);
                pendingAudioPad_ = pad;
                gst_object_ref(pendingAudioPad_);
                gst_element_set_state(sink, GST_STATE_READY);
                GstPadLinkReturn r2 = gst_pad_link(pad, sinkPad);
                PLAYER_LOG("decodebin pad link retry=%d", r2);
                if (r2 == GST_PAD_LINK_OK) {
                    gst_object_unref(pendingAudioPad_);
                    pendingAudioPad_ = nullptr;
                }
            } else {
                if (pendingAudioPad_) {
                    gst_object_unref(pendingAudioPad_);
                    pendingAudioPad_ = nullptr;
                }
            }
            gst_object_unref(sinkPad);
        }
    }
    gst_caps_unref(caps);
}

void BiliStreamPlayer::teardown()
{
    stopping_ = true;
    if (busThread_.joinable()) busThread_.join();
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
    }
    pipeline_ = nullptr;
    demux_ = nullptr;
    vparse_ = nullptr;
    vdec_ = nullptr;
    vqueue_ = nullptr;
    vconvert_ = nullptr;
    vscale_ = nullptr;
    vcaps_ = nullptr;
    vbox_ = nullptr;
    vconvert2_ = nullptr;
    decodebin_ = nullptr;
    aconvert_ = nullptr;
    aresample_ = nullptr;
    acaps_early_ = nullptr;
    aqueue_ = nullptr;
    avolume_ = nullptr;
    videoSink_ = nullptr;
    audioSink_ = nullptr;
    if (pendingAudioPad_) {
        gst_object_unref(pendingAudioPad_);
        pendingAudioPad_ = nullptr;
    }
}

void BiliStreamPlayer::busLoop()
{
    if (!pipeline_) return;
    GstBus* bus = gst_element_get_bus(pipeline_);
    while (!stopping_.load()) {
        GstMessage* msg = gst_bus_timed_pop_filtered(bus, 100000,
            GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED));
        if (!msg) continue;
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            PLAYER_LOG("ERROR: %s (%s)", err->message, dbg ? dbg : "");
            emitState(std::string("error:") + (err->message ? err->message : "unknown"));
            g_error_free(err);
            g_free(dbg);
            break;
        }
        case GST_MESSAGE_EOS:
            PLAYER_LOG("EOS");
            emitState("ended");
            break;
        case GST_MESSAGE_STATE_CHANGED: {
            GstState old = GST_STATE_NULL, now = GST_STATE_NULL, pending = GST_STATE_NULL;
            gst_message_parse_state_changed(msg, &old, &now, &pending);
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline_) && now == GST_STATE_PLAYING) {
                PLAYER_LOG("state -> PLAYING");
            }
            break;
        }
        default:
            break;
        }
        gst_message_unref(msg);
    }
    gst_object_unref(bus);
}

void BiliStreamPlayer::emitState(const std::string& state)
{
    try {
        stateChanged.emit(state);
    } catch (...) {
        PLAYER_LOG("emitState cb threw");
    }
}

// 信号静态转发
void BiliStreamPlayer::qtdemuxPadAddedCb(GstElement* element, GstPad* pad, gpointer userdata)
{
    static_cast<BiliStreamPlayer*>(userdata)->onQtdemuxPadAdded(pad);
}

void BiliStreamPlayer::decodebinPadAddedCb(GstElement* element, GstPad* pad, gpointer userdata)
{
    static_cast<BiliStreamPlayer*>(userdata)->onDecodebinPadAdded(pad);
}

} // namespace bilistream

// ---- JS 绑定初始化（由 JSBiliStreamModule.cpp bilistream_init 调用）----
namespace bilistream {

static JQuick::sp<JQObjectTemplate> createBiliStream(JQModuleEnv* env)
{
    JQuick::sp<JQObjectTemplate> tpl = env->CreateClassTemplate("BiliStreamPlayer");
    if (!tpl) return nullptr;

    tpl->SetProtoMethod("open", &BiliStreamPlayer::open);
    tpl->SetProtoMethod("start", &BiliStreamPlayer::start);
    tpl->SetProtoMethod("pause", &BiliStreamPlayer::pause);
    tpl->SetProtoMethod("resume", &BiliStreamPlayer::resume);
    tpl->SetProtoMethod("seek", &BiliStreamPlayer::seek);
    tpl->SetProtoMethod("close", &BiliStreamPlayer::close);
    tpl->SetProtoMethod("getDuration", &BiliStreamPlayer::getDuration);
    tpl->SetProtoMethod("getPosition", &BiliStreamPlayer::getPosition);

    tpl->InstanceTemplate()->Set("stateChanged", &BiliStreamPlayer::stateChanged);

    return tpl->CallConstructor();
}

void bilistream_init(JQModuleEnv* env)
{
    env->setModuleExport("biliStream", createBiliStream(env));
}

} // namespace bilistream

