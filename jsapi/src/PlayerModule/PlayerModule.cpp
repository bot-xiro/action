// 播放器业务实现：内嵌 GStreamer 控制（dlopen + dlsym 手写稳定 ABI 绑定）。
//
// 为什么动态加载而不是静态链接：
//   设备只有 GStreamer 1.22.0 运行时库（libgstreamer-1.0.so.0.2200.0），没有开发头文件，
//   也没有匹配的交叉 sysroot。dlopen 让 native 库编译零外部依赖（在 GitHub Actions 上
//   只需 aarch64 交叉 gcc + iot-sdk），运行时直接绑定设备上版本完全匹配的库。
//
// 手写 ABI（GStreamer 1.x 自 1.0 起长期稳定）：
//   GstState            : VOID_PENDING=0 NULL=1 READY=2 PAUSED=3 PLAYING=4
//   GstStateChangeReturn: FAILURE=0 SUCCESS=1 ASYNC=2 NO_PREROLL=3
//   GstFormat           : UNDEFINED=0 DEFAULT=1 BYTES=2 TIME=3 BUFFERS=4 PERCENT=5
//   GstSeekFlags        : NONE=0 FLUSH=1 ACCURATE=2 KEY_UNIT=4 TRICKMODE=0x10
#include "PlayerModule.hpp"

#include <dlfcn.h>
#include <cstring>
#include <cctype>
#include <mutex>

// ---------------------------------------------------------------------------
// GStreamer 不透明句柄（AArch64 上均为指针）
// ---------------------------------------------------------------------------
typedef void GstElement;
typedef int  GstState;              // enum
typedef int  GstStateChangeReturn;  // enum
typedef int  GstFormat;             // enum
typedef int  GstSeekFlags;          // enum
typedef long long gint64;
typedef int gboolean;
typedef void *gpointer;
typedef struct _GError GError;
typedef struct _GstQuery GstQuery;

namespace gst_enums {
constexpr GstState  STATE_NULL    = 1;
constexpr GstState  STATE_PAUSED  = 3;
constexpr GstState  STATE_PLAYING = 4;
constexpr int       CHANGE_SUCCESS = 1;
constexpr GstFormat FORMAT_TIME   = 3;
constexpr GstSeekFlags SEEK_FLUSH_ACCURATE = 1 | 2;
}

// ---------------------------------------------------------------------------
// GstRuntime：dlopen/dlsym 的函数指针集合（进程内单例）
// ---------------------------------------------------------------------------
class GstRuntime {
public:
    // gst_init(int*, char***)
    void (*gst_init)(int *, char ***) = nullptr;
    // GstElement* gst_parse_launch(const gchar*, GError**)
    GstElement *(*gst_parse_launch)(const char *, GError **) = nullptr;
    // GstStateChangeReturn gst_element_set_state(GstElement*, GstState)
    int (*gst_element_set_state)(GstElement *, GstState) = nullptr;
    // GstStateChangeReturn gst_element_get_state(GstElement*, GstState*, GstState*, GstClockTime)
    int (*gst_element_get_state)(GstElement *, GstState *, GstState *, gint64) = nullptr;
    // gboolean gst_element_seek_simple(GstElement*, GstFormat, GstSeekFlags, gint64)
    gboolean (*gst_element_seek_simple)(GstElement *, GstFormat, GstSeekFlags, gint64) = nullptr;
    // GstQuery* gst_query_new_position(GstFormat)
    GstQuery *(*gst_query_new_position)(GstFormat) = nullptr;
    // GstQuery* gst_query_new_duration(GstFormat)
    GstQuery *(*gst_query_new_duration)(GstFormat) = nullptr;
    // gboolean gst_element_query(GstElement*, GstQuery*)
    gboolean (*gst_element_query)(GstElement *, GstQuery *) = nullptr;
    // gboolean gst_query_parse_position(GstQuery*, GstFormat*, gint64*)
    gboolean (*gst_query_parse_position)(GstQuery *, GstFormat *, gint64 *) = nullptr;
    // gboolean gst_query_parse_duration(GstQuery*, GstFormat*, gint64*)
    gboolean (*gst_query_parse_duration)(GstQuery *, GstFormat *, gint64 *) = nullptr;
    // void gst_object_unref(gpointer)
    void (*gst_object_unref)(gpointer) = nullptr;

    static GstRuntime &inst() {
        static GstRuntime r;
        return r;
    }

    bool ok() const { return gst_init && gst_parse_launch; }

private:
    GstRuntime() { load(); }

    void load() {
        void *h = ::dlopen("libgstreamer-1.0.so.0", RTLD_NOW | RTLD_GLOBAL);
        if (!h) return;
        gst_init = (decltype(gst_init))::dlsym(h, "gst_init");
        gst_parse_launch = (decltype(gst_parse_launch))::dlsym(h, "gst_parse_launch");
        gst_element_set_state = (decltype(gst_element_set_state))::dlsym(h, "gst_element_set_state");
        gst_element_get_state = (decltype(gst_element_get_state))::dlsym(h, "gst_element_get_state");
        gst_element_seek_simple = (decltype(gst_element_seek_simple))::dlsym(h, "gst_element_seek_simple");
        gst_query_new_position = (decltype(gst_query_new_position))::dlsym(h, "gst_query_new_position");
        gst_query_new_duration = (decltype(gst_query_new_duration))::dlsym(h, "gst_query_new_duration");
        gst_element_query = (decltype(gst_element_query))::dlsym(h, "gst_element_query");
        gst_query_parse_position = (decltype(gst_query_parse_position))::dlsym(h, "gst_query_parse_position");
        gst_query_parse_duration = (decltype(gst_query_parse_duration))::dlsym(h, "gst_query_parse_duration");
        gst_object_unref = (decltype(gst_object_unref))::dlsym(h, "gst_object_unref");
    }
};

namespace {
constexpr const char *kGstLib = "libgstreamer-1.0.so.0";

// 已探测 RK3562 KMS 参数（profiles/device-215.yaml）
constexpr const char *kKmsDriver = "rockchip";
constexpr const char *kKmsConnector = "125";   // DSI-1
constexpr const char *kKmsPlane = "54";        // Primary plane，支持 NV12

bool hasControlChar(const std::string &s) {
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7f) return true;
    }
    return false;
}
} // namespace

struct PlayerPipeline::Impl {
    PlayerStatus status;
    mutable std::mutex mtx;
    GstElement *pipeline = nullptr;
    bool gstAvailable = false;

    void setStatusLocked() {}
};

PlayerPipeline::PlayerPipeline() : impl_(std::make_unique<Impl>())
{
    impl_->gstAvailable = GstRuntime::inst().ok();
}

PlayerPipeline::~PlayerPipeline()
{
    PlayerStatus ignored;
    stop(ignored);
}

bool PlayerPipeline::validateUrl(const std::string &url)
{
    if (url.empty() || url.size() > 2048) return false;
    if (hasControlChar(url)) return false;
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return false;
    return true;
}

bool PlayerPipeline::validateType(const std::string &type)
{
    return type == "hls" || type == "ts" || type == "mp4";
}

bool PlayerPipeline::isLive(const std::string &type)
{
    return type == "ts";
}

// 组装管线描述（parse_launch 字符串）。hls 走 hlsdemux，ts 走 tsdemux，mp4 走 qtdemux。
static std::string buildPipelineDesc(const std::string &url, const std::string &type)
{
    std::string demux;
    if (type == "hls") demux = "hlsdemux";
    else if (type == "ts") demux = "tsdemux";
    else demux = "qtdemux";

    return std::string("souphttpsrc location=") + url + " ! " + demux +
           " ! queue ! h264parse ! mppvideodec rotation=0 ! kmssink "
           "driver-name=" + kKmsDriver +
           " connector-id=" + kKmsConnector +
           " plane-id=" + kKmsPlane +
           " sync=false";
}

static void emitState(const std::function<void(const std::string &json)> &onEvent,
                      const char *state)
{
    if (onEvent) onEvent(std::string("{\"event\":\"state\",\"state\":\"") + state + "\"}");
}

bool PlayerPipeline::load(const std::string &url, const std::string &type,
                          const std::function<void(const std::string &json)> &onEvent,
                          PlayerStatus &outStatus)
{
    stop(outStatus);  // 幂等清旧 pipeline

    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        impl_->status = PlayerStatus{};
        impl_->status.state = PlayerState::Loading;
    }

    if (!validateUrl(url)) {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        impl_->status.state = PlayerState::Error;
        impl_->status.lastError = "invalid url";
        outStatus = impl_->status;
        return false;
    }
    if (!validateType(type)) {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        impl_->status.state = PlayerState::Error;
        impl_->status.lastError = "invalid type";
        outStatus = impl_->status;
        return false;
    }
    if (!impl_->gstAvailable) {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        impl_->status.state = PlayerState::Error;
        impl_->status.lastError = "gstreamer runtime not available";
        outStatus = impl_->status;
        return false;
    }

    auto &g = GstRuntime::inst();
    g.gst_init(nullptr, nullptr);

    GError *err = nullptr;
    const std::string desc = buildPipelineDesc(url, type);
    GstElement *pipeline = g.gst_parse_launch(desc.c_str(), &err);
    if (!pipeline) {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        impl_->status.state = PlayerState::Error;
        impl_->status.lastError = err && err->message ? std::string(err->message)
                                                       : "parse_launch failed";
        outStatus = impl_->status;
        return false;
    }

    // 预滚到 PAUSED
    int rc = g.gst_element_set_state(pipeline, gst_enums::STATE_PAUSED);
    (void)rc;

    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        impl_->pipeline = pipeline;
        impl_->status.state = PlayerState::Paused;
        impl_->status.title = url;
        outStatus = impl_->status;
    }
    emitState(onEvent, "loaded");
    return true;
}

bool PlayerPipeline::play(PlayerStatus &outStatus)
{
    auto &g = GstRuntime::inst();
    GstElement *p;
    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        p = impl_->pipeline;
    }
    if (!p) { outStatus = getStatus(); outStatus.lastError = "no pipeline"; return false; }

    int rc = g.gst_element_set_state(p, gst_enums::STATE_PLAYING);
    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        impl_->status.state = (rc == gst_enums::CHANGE_SUCCESS)
                              ? PlayerState::Playing : PlayerState::Playing;
        // ASYNC 也先标 playing，后续 refresh 校正
        outStatus = impl_->status;
    }
    return true;
}

bool PlayerPipeline::pause(PlayerStatus &outStatus)
{
    auto &g = GstRuntime::inst();
    GstElement *p;
    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        p = impl_->pipeline;
    }
    if (!p) { outStatus = getStatus(); outStatus.lastError = "no pipeline"; return false; }

    g.gst_element_set_state(p, gst_enums::STATE_PAUSED);
    refresh(impl_->status);
    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        impl_->status.state = PlayerState::Paused;
        outStatus = impl_->status;
    }
    return true;
}

bool PlayerPipeline::resume(PlayerStatus &outStatus)
{
    return play(outStatus);
}

bool PlayerPipeline::stop(PlayerStatus &outStatus)
{
    auto &g = GstRuntime::inst();
    GstElement *p;
    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        p = impl_->pipeline;
        impl_->pipeline = nullptr;
    }
    if (p) {
        g.gst_element_set_state(p, gst_enums::STATE_NULL);
        g.gst_object_unref(p);
    }
    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        impl_->status = PlayerStatus{};
        impl_->status.state = PlayerState::Idle;
        outStatus = impl_->status;
    }
    return true;
}

bool PlayerPipeline::seek(double seconds, PlayerStatus &outStatus)
{
    auto &g = GstRuntime::inst();
    GstElement *p;
    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        p = impl_->pipeline;
    }
    if (!p) { outStatus = getStatus(); outStatus.lastError = "no pipeline"; return false; }

    gint64 pos = (gint64)(seconds * 1000000000.0);  // 秒 -> 纳秒
    g.gst_element_seek_simple(p, gst_enums::FORMAT_TIME,
                              gst_enums::SEEK_FLUSH_ACCURATE, pos);
    refresh(impl_->status);
    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        outStatus = impl_->status;
    }
    return true;
}

bool PlayerPipeline::refresh(PlayerStatus &outStatus)
{
    auto &g = GstRuntime::inst();
    GstElement *p;
    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        p = impl_->pipeline;
    }
    if (!p) { outStatus = getStatus(); return false; }

    long long posMs = 0;
    long long durMs = -1;

    GstQuery *qpos = g.gst_query_new_position(gst_enums::FORMAT_TIME);
    if (qpos) {
        if (g.gst_element_query(p, qpos)) {
            GstFormat fmt = 0;
            gint64 val = 0;
            if (g.gst_query_parse_position(qpos, &fmt, &val) && fmt == gst_enums::FORMAT_TIME)
                posMs = val / 1000000;
        }
        g.gst_object_unref(qpos);
    }

    GstQuery *qdur = g.gst_query_new_duration(gst_enums::FORMAT_TIME);
    if (qdur) {
        if (g.gst_element_query(p, qdur)) {
            GstFormat fmt = 0;
            gint64 val = 0;
            if (g.gst_query_parse_duration(qdur, &fmt, &val) && fmt == gst_enums::FORMAT_TIME)
                durMs = val / 1000000;
        }
        g.gst_object_unref(qdur);
    }

    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        impl_->status.positionMs = posMs;
        impl_->status.durationMs = durMs;
        outStatus = impl_->status;
    }
    return true;
}

PlayerStatus PlayerPipeline::getStatus() const
{
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->status;
}