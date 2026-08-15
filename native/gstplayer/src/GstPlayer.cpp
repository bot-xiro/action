#include "GstPlayer.h"
#include "GstProxy.h"
#include "ControlBar.h"

#include <cstdio>
#include <sstream>
#include <syslog.h>
#include <cstring>

#include <gdk-pixbuf/gdk-pixbuf.h>

#ifdef KMSSINK_TEST
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#endif

using namespace JQUTIL_NS;

namespace gstplayer {

// 日志统一走 local7 设施（设备 syslog.conf: local7.* → /data/applog/YD_PEN_APP.log），
// 否则 user.* 无路由 → 原生日志不可见（2026-08-14 实证排查）
#define PLAYER_LOG(fmt, ...) syslog(LOG_LOCAL7 | LOG_ERR, "[gstplayer] " fmt, ##__VA_ARGS__)

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

#ifdef KMSSINK_TEST
// ---- 通过 libdrm 直接设置 DRM plane 的 zpos（KMSSINK 双平面层级控制） ----
//
// 背景：kmssink 元素【没有】zpos 属性（设备日志实证 "kmssink has no zpos
// property"），因此 g_object_set(videoSink_, "zpos", 0) 从不生效；而 Rockchip
// DRM 里 video overlay plane（本设备 plane 76, Esmart1）默认 zpos=2，高于
// UI 主平面（plane 54, Esmart0, zpos=0）→ 视频必然盖住 UI。
// 解法：用 libdrm 打开 /dev/dri/card0，把【UI 主平面 54】的 zpos 提升到 3，
// UI 自然盖过视频；WebView hole 挖洞处透出视频。54 由 weston 独占，
// kmssink 播放接管视频平面时不会重置它，层级策略稳定。
// 调用时机：open() 建好后立即设一次（此时 plane 尚未启用，属性可写），
// start() 播放前再补设一次（幂等）。
//
// 崩溃红线：本 .so 交叉编译自 x86 宿主，若直接链接 libdrm 符号会因
// undefined symbol 在运行时解析失败导致 miniapp 闪退（12:37 三进程崩溃实证）。
// 必须 dlopen("libdrm.so") + dlsym 动态取函数指针，全部失败则安全降级
// （仅记录日志，不崩溃、不影响播放主链路）。
static bool setPlaneZpos(int planeId, uint32_t zpos)
{
    void* drmLib = dlopen("libdrm.so.2", RTLD_NOW | RTLD_GLOBAL);
    if (!drmLib) drmLib = dlopen("libdrm.so", RTLD_NOW | RTLD_GLOBAL);
    if (!drmLib) {
        PLAYER_LOG("setPlaneZpos: dlopen libdrm.so failed: %s", dlerror());
        return false;
    }
    // libdrm 函数签名与 xf86drmMode.h 一致，这里用 typedef 取函数指针
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
        PLAYER_LOG("setPlaneZpos: libdrm symbol missing (get=%p free=%p prop=%p set=%p)",
            (void*)pGetProps, (void*)pFreeProps, (void*)pGetProp, (void*)pSetProp);
        dlclose(drmLib);
        return false;
    }
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        PLAYER_LOG("setPlaneZpos: open /dev/dri/card0 failed: %s", strerror(errno));
        dlclose(drmLib);
        return false;
    }

    // 1) 查找目标的 zpos 属性 id
    uint32_t zpropId = 0;
    drmModeObjectPropertiesPtr props =
        pGetProps(fd, planeId, DRM_MODE_OBJECT_PLANE);
    if (props) {
        for (uint32_t i = 0; i < props->count_props && !zpropId; i++) {
            drmModePropertyPtr prop = pGetProp(fd, props->props[i]);
            if (prop) {
                if (strcmp(prop->name, "zpos") == 0) zpropId = prop->prop_id;
                pFreeProp(prop);
            }
        }
        pFreeProps(props);
    }
    if (!zpropId) {
        PLAYER_LOG("setPlaneZpos: plane %d has no zpos property", planeId);
        close(fd);
        dlclose(drmLib);
        return false;
    }

    // 2) legacy 设置（绝大多数驱动支持）；失败则 atomic 兜底
    int ret = pSetProp(fd, planeId, DRM_MODE_OBJECT_PLANE, zpropId, zpos);
    if (ret != 0) {
        void* req = pAtomicAlloc();
        if (req) {
            pAtomicAdd(req, planeId, zpropId, zpos);
            ret = pAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
            pAtomicFree(req);
        }
    }
    close(fd);
    dlclose(drmLib);
    if (ret != 0) {
        PLAYER_LOG("setPlaneZpos: plane %d set zpos=%u failed: %s", planeId, zpos, strerror(errno));
        return false;
    }
    PLAYER_LOG("setPlaneZpos: plane %d zpos=%u OK", planeId, zpos);
    return true;
}
#endif  // KMSSINK_TEST

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
//      逻辑全屏为 960×480，播放页即全屏视区（hole 挖洞覆盖整个 960×480）。
//      前端调用 gstPlayer.open({ pos_x, pos_y, pos_w, pos_h }) 传入的即此坐标系中
//      的视频矩形（本页全屏: <0, 0, 960, 480>）。
//   2) 物理 CRTC 坐标（kmssink render-rectangle 需要）：未旋转的硬件原生坐标，
//      宽 480 × 高 960（竖屏）。
//      注意：kmssink 直出物理 plane，不经 weston 的 rotate-90 变换，
//      所以把逻辑矩形直接填进 render-rectangle 必然错位（旧 bug：宽度 960
//      超出物理宽 480 被钳制，画面贴到右下角）。
//
// 【终极校准 2026-08-09 第四轮·用户修正】Weston transform=rotate-90 实际为
// 【逆时针 90°】旋转。逆时针坐标系映射（物理面板 宽 480 × 高 960）：
//     逻辑屏幕顶部 (y=0~266) ↔ 物理屏幕左侧 (x=0~266)
//     物理 x' = ly                     ← 逻辑 y 轴正向 → 物理 x 轴正向（不反转！）
//     物理 y' = lx                     ← 逻辑 x 轴正向 → 物理 y 轴正向
//     物理宽  = 逻辑高 lh
//     物理高  = 逻辑宽 lw
//
// 此前误按顺时针计算 physX = 480-(ly+lh) = 214，画面落到物理右缘
// (214~480)，逆时针旋转回逻辑后映射到逻辑底部 → 用户反馈"视频偏下"。
//
// 代入全屏方案（pos_x=0, pos_y=0, pos_w=960, pos_h=480）：
//     physX = ly = 0
//     physY = lx = 0
//     physW = lh = 480
//     physH = lw = 960
// 物理矩形 <0, 0, 480, 960>：正好铺满整块物理面板 —— 播放页全屏无偏移。
//
// 历史踩坑（勿回退）：曾把逻辑矩形换算成 266 宽物理竖条（pos_h=266 时
// physW=266 → x=0 上置偏上、x=214(顺时针误算) 偏下，真机两轮均错位）。
// 根因是前端视区高度写死 266（只占逻辑屏上半），现已统一改为 480 全屏：
// 前端 hole/pos 必须 960×480，原生换算自然得到物理全屏。
struct KmsRect { int x, y, w, h; };

static KmsRect logicToCrtc(int lx, int ly, int lw, int lh)
{
    KmsRect r;
    r.x = ly;
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
    // KMSSINK 双平面模式（视频在上，UI在下）：
    // - 视频平面 76 (overlay, zpos=3) 位于 UI 主平面 54 (primary, zpos=0) 之上
    // - 前端 clip-path: evenodd 在纯黑 #000 底上镂空 960×266 视频区域（物理孔洞）
    // - 控制栏由原生 gdkpixbufoverlay 合成进视频帧，随视频平面一起显示
    // - render-rectangle 物理坐标与前端视频区域保持一致（全视口 960×266）
    // - 触摸事件在 WebView 层处理，与 DRM 平面层级无关
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

    // 【2026-08-11 用户指令最终版】自研播放器 + 本地反向代理（结合 lilo 官方方案：
    // lilo 的 tools_video 用 Video.getProxyUrl() 把 B 站 url 转本地代理
    // 127.0.0.1:<port>/__video_proxy__/...，附加 Referer/w3c UA + 白名单
    // (bilibili.com/bilivideo.com/mountaintoys.cn/hdslb.com)，绕过 B 站 CDN 403。
    // 我们在本 .so 内复刻同款：GstProxy 线程代理 + curl 带 Referer/UA + Range 透传；
    // 白名单域才走代理，其余直连（见 GstProxy.cpp kWhiteList）。
    // 双保险：非白名单/代理不可用时仍直连，且 buildPipeline 的 souphttpsrc
    // extra-headers Referer（877be03）依然在管线里生效。
    uri = proxy::maybeRewrite(uri);

    PLAYER_LOG("open uri=%s audio=%d rect=%s fill=%s", uri.c_str(), audio ? 1 : 0, rect.str().c_str(), fill.c_str());
    // 【2026-08-14 悬浮控制栏】画布尺寸 = sink render-rectangle 尺寸（KMSSINK 物理坐标 / waylandsink 逻辑坐标），
    // 控制栏 cairo 画布与之一致 → 1:1 映射，无二次缩放。
    int canvasW = (posW > 0 && posH > 0) ? posW : 0;
    int canvasH = (posW > 0 && posH > 0) ? posH : 0;
    // 【竖屏支持】早期在 pad-added 中直接调用 applyCanvasContent，
    // 无需 start() 中完整重建，避免“先横后竖”闪烁。
    bool ok = buildPipeline(uri, audio, rect.str(), fill, canvasW, canvasH);
    PLAYER_LOG("open buildPipeline ret=%d", ok ? 1 : 0);
    if (!ok) {
        teardown();
        info.GetReturnValue().ThrowInternalError("open: pipeline build failed");
        return;
    }

#ifdef KMSSINK_TEST
    // 【2026-08-11 嵌进播放器】不再在此强制视频置底（zpos=0）！
    // 视频 zpos 由 JS 统一控制（open 后 setVideoZpos(3) 置顶常驻，中间条布局）。
    // 历史遗留注释保留：曾因 UI 层 XR24 不透明 + 控制栏与视频同区域互斥，
    // 用视频置底让控制栏可操作；现中间条布局已无该冲突。
#endif

    info.GetReturnValue().Set(true);
}

void GstPlayer::preheat(JQFunctionInfo& info)
{
    // 【2026-08-15 修正】视频在上，UI在下：overlay plane 76 提升至 zpos=3，primary plane 54 保持 zpos=0。
    // 原因：primary 平面不支持 alpha/透明（黑底遮挡视频），overlay 支持 alpha 且控制栏已合成进视频帧。
    // 前端使用 clip-path: evenodd 在黑底上镂空视频区域，视频平面在 UI 平面之上透出。
    // 触摸事件在 WebView 层处理（与 DRM 平面层级无关，前端已验证）。
    // 本方法幂等（app.js onLaunch 调用一次）。
    static std::atomic<bool> done{false};
    if (!done.exchange(true)) {
        // 视频平面 76 (overlay) 提升至 zpos=3，覆盖在 UI 主平面 54(zpos=0) 之上
        if (!setPlaneZpos(76, 3)) {
            PLAYER_LOG("preheat WARN: video plane 76 zpos=3 set failed");
        }
        // UI 平面 54 (primary) 保持默认 zpos=0，无需设置
    }
    info.GetReturnValue().Set(true);
}

// 【2026-08-15 启用】动态层级切换：可通过 JS 调整视频平面 76 zpos。
// 控制栏由 gdkpixbufoverlay 合成进视频帧，无需通过 DRM zpos 切换实现显隐。
// 保留接口仅为兼容旧调用（前端已移除 setVideoTopmost），忽略参数直接返回。
void GstPlayer::setVideoZpos(JQFunctionInfo& info)
{
#ifdef KMSSINK_TEST
    int zpos = 0;
    if (info.Length() > 0) {
        if (JS_ToInt32(info.GetContext(), &zpos, info[0]) != 0) {
            // 对象形态 {zpos: N} 兜底
            zpos = jsGetInt(info.GetContext(), info[0], "zpos", 0);
        }
    }
    if (zpos < 0) zpos = 0;
    if (zpos > 10) zpos = 10;
    if (!setPlaneZpos(76, (uint32_t)zpos)) {
        PLAYER_LOG("setVideoZpos: plane 76 zpos=%d set failed", zpos);
    } else {
        PLAYER_LOG("setVideoZpos: plane 76 zpos=%d set ok", zpos);
    }
#else
    PLAYER_LOG("setVideoZpos: ignored (non-KMSSINK build)");
#endif
    info.GetReturnValue().Set(true);
}

void GstPlayer::start(JQFunctionInfo& info)
{
    PLAYER_LOG("start enter");
    if (!pipeline_) {
        info.GetReturnValue().ThrowInternalError("start: not opened");
        return;
    }
    // 【死锁红线 2026-08-09】open() 返回时 PAUSED 预滚仍是 ASYNC（ret=2），
    // 若直接切 PLAYING，状态迁移会与流线程的 pad-added 动态链接（KMSSINK 下
    // 音频 decodebin pad、h264 vdec 链）并发互斥，真机实证全线 futex 死锁：
    // 画面冻结第一帧（15:37 /proc 线程快照：所有 GStreamer 线程 utime 零增长、
    // mpp_dec_hal 0 CPU，wchan=futex_wait_queue_me）。
    // 解法：start() 先同步等待预滚真正完成（gst_element_get_state 阻塞式），
    // 所有动态 pad 就绪后再切 PLAYING；10s 超时以免永久挂起。
    GstStateChangeReturn preroll = gst_element_get_state(
        pipeline_, nullptr, nullptr, 10LL * GST_SECOND);
    PLAYER_LOG("start wait preroll ret=%d", (int)preroll);
    if (preroll == GST_STATE_CHANGE_FAILURE) {
        info.GetReturnValue().ThrowInternalError("start: preroll failed");
        return;
    }
    // 【竖屏支持】早期已在 pad-added 调用 applyCanvasContent，此处无需重建。
    // 如检测到仍需重建（极端情况），可在此处添加兜底。
    // 【诊断 2026-08-14】预滚后打印视频链实际协商 caps（帧尺寸/格式），
    // 用于核对画布/叠加几何是否与预期一致（266×960 或 960×266）
    if (voverlay_) {
        GstPad* opad = gst_element_get_static_pad(voverlay_, "src");
        if (opad) {
            GstCaps* c = gst_pad_get_current_caps(opad);
            if (c) {
                gchar* cs = gst_caps_to_string(c);
                PLAYER_LOG("overlay src caps: %s", cs ? cs : "(null)");
                if (cs) g_free(cs);
                gst_caps_unref(c);
            } else {
                PLAYER_LOG("overlay src caps: (no caps yet)");
            }
            gst_object_unref(opad);
        }
    }
    // 预滚后重试挂起的音频 pad：pad-added 在 ASYNC 阶段可能因又被占用/被阻止而失败，
    // 预滚完成后再试一次，提高绑定成功率。
    if (pendingAudioPad_ && acaps_early_) {
        GstPad* acapsSink = gst_element_get_static_pad(acaps_early_, "sink");
        if (acapsSink) {
            GstPadLinkReturn ar = gst_pad_link(pendingAudioPad_, acapsSink);
            PLAYER_LOG("start: audio-pending pad link retry ret=%d", (int)ar);
            if (ar == GST_PAD_LINK_OK && pendingAudioPad_) {
                gst_object_unref(pendingAudioPad_);
                pendingAudioPad_ = nullptr;
            }
            gst_object_unref(acapsSink);
        }
    }
    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    // 【2026-08-15 用户指令】视频与 UI 同层：视频平面 76 固定 zpos=0（preheat 已设置），
    // UI 主平面 54 zpos=3。控制栏由 gdkpixbufoverlay 合成进视频帧，无需动态切换层级。
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

// ---- 进度条支持：时长/位置查询（ms），seek 跳转（ms）----

void GstPlayer::getDuration(JQFunctionInfo& info)
{
    if (!pipeline_) {
        info.GetReturnValue().Set(0);
        return;
    }
    gint64 dur = 0;
    // 多源查询（2026-08-14 实测 pipeline 查询在叠加链下返回 0）：
    // 1) pipeline → 2) qtdemux（moov 已知时长）→ 3) 视频 sink
    if (!(gst_element_query_duration(pipeline_, GST_FORMAT_TIME, &dur) && dur > 0)) {
        dur = 0;
        if (demux_ && gst_element_query_duration(demux_, GST_FORMAT_TIME, &dur) && dur > 0) {
            PLAYER_LOG("duration from qtdemux: %lld ms", (long long)(dur / 1000000));
        } else {
            dur = 0;
            if (videoSink_ && gst_element_query_duration(videoSink_, GST_FORMAT_TIME, &dur) && dur > 0) {
                PLAYER_LOG("duration from video sink: %lld ms", (long long)(dur / 1000000));
            } else {
                dur = 0;
            }
        }
    }
    if (dur > 0) {
        info.GetReturnValue().Set(static_cast<double>(dur) / 1000000.0);   // ns -> ms
    } else {
        info.GetReturnValue().Set(0);
    }
}

void GstPlayer::getPosition(JQFunctionInfo& info)
{
    if (!pipeline_) {
        info.GetReturnValue().Set(0);
        return;
    }
    // 【防��回 2026-08-14】seeking_ 期间返回目标位置，��免查�� pipeline 返回旧位置
    if (seeking_) {
        info.GetReturnValue().Set(seekTargetMs_);
        return;
    }
    gint64 pos = 0;
    if (gst_element_query_position(pipeline_, GST_FORMAT_TIME, &pos) && pos > 0) {
        info.GetReturnValue().Set(static_cast<double>(pos) / 1000000.0);   // ns -> ms
    } else {
        info.GetReturnValue().Set(0);
    }
}

void GstPlayer::seek(JQFunctionInfo& info)
{
    if (!pipeline_) {
        info.GetReturnValue().Set(false);
        return;
    }
    if (info.Length() < 1 || !JS_IsNumber(info[0])) {
        info.GetReturnValue().Set(false);
        return;
    }
    double ms = 0;
    if (JS_ToFloat64(info.GetContext(), &ms, info[0]) != 0) {
        info.GetReturnValue().Set(false);
        return;
    }
    gint64 ns = static_cast<gint64>(ms * 1000000.0);   // ms -> ns
    if (ns < 0) ns = 0;
    // 【seek 修复 2026-08-14】源可 seek 性由代理响应头决定（Accept-Ranges，见
    // GstProxy.cpp）：恢复 FLUSH seek（无 FLUSH 时被源拒绝 ret=0）。
    bool ok = gst_element_seek_simple(pipeline_, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), ns);
    PLAYER_LOG("seek to %.0f ms ret=%d", ms, ok ? 1 : 0);
    info.GetReturnValue().Set(ok);
}

// ---- 双指缩放：播放中动态修改渲染区域（render-rectangle） ----
//
// KMS 双平面架构下 UI 层 CSS 无法缩放视频（视频由 DRM 硬件直接合成），
// 前端只能捕捉双指手势 → 计算缩放后的逻辑矩形 → 调本接口动态更新 sink
// 的 render-rectangle 属性，Rockchip DRM 在硬件层面瞬间完成重缩放与位移。
// 入参: setRect(x, y, w, h) —— UI 逻辑坐标（与 open 的 pos_* 同坐标系），
// 内部按 KMSSINK 逆时针 rotate-90 换算为物理 CRTC 坐标。
void GstPlayer::setRect(JQFunctionInfo& info)
{
    if (info.Length() < 4) {
        info.GetReturnValue().ThrowTypeError("setRect: x,y,w,h required");
        return;
    }
    int lx = 0, ly = 0, lw = 0, lh = 0;
    if (JS_ToInt32(info.GetContext(), &lx, info[0]) != 0 ||
        JS_ToInt32(info.GetContext(), &ly, info[1]) != 0 ||
        JS_ToInt32(info.GetContext(), &lw, info[2]) != 0 ||
        JS_ToInt32(info.GetContext(), &lh, info[3]) != 0) {
        info.GetReturnValue().ThrowTypeError("setRect: invalid numbers");
        return;
    }
    if (!videoSink_) {
        PLAYER_LOG("setRect failed: video sink is null");
        info.GetReturnValue().Set(false);
        return;
    }
    if (lw <= 0 || lh <= 0 || lx < 0 || ly < 0) {
        PLAYER_LOG("setRect rejected: invalid rect (%d,%d,%d,%d)", lx, ly, lw, lh);
        info.GetReturnValue().Set(false);
        return;
    }

#ifdef KMSSINK_TEST
    // KMSSINK 双平面：逻辑坐标 → 物理 CRTC 坐标（逆时针 rotate-90，复用 open 同款换算）
    KmsRect p = logicToCrtc(lx, ly, lw, lh);
    PLAYER_LOG("setRect LOGIC(%d,%d,%d,%d) -> PHYS(%d,%d,%d,%d)", lx, ly, lw, lh, p.x, p.y, p.w, p.h);
    lx = p.x; ly = p.y; lw = p.w; lh = p.h;
#else
    PLAYER_LOG("setRect LOGIC(%d,%d,%d,%d) -> waylandsink same coords", lx, ly, lw, lh);
#endif

    // render-rectangle 是 GstValueArray of gint（Write only），g_object_set 传 C 字符串
    // 会触发 GLib 类型不匹配崩溃；必须 gst_util_set_object_arg 解析 "<x, y, w, h>"。
    std::ostringstream rect;
    rect << "<" << lx << ", " << ly << ", " << lw << ", " << lh << ">";
    GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(videoSink_), "render-rectangle");
    if (!pspec) {
        PLAYER_LOG("setRect failed: sink has no render-rectangle property");
        info.GetReturnValue().Set(false);
        return;
    }
    gst_util_set_object_arg(G_OBJECT(videoSink_), "render-rectangle", rect.str().c_str());
    PLAYER_LOG("setRect applied: %s", rect.str().c_str());
    info.GetReturnValue().Set(true);
}

// ---- 通用 HTTP GET（带浏览器 UA+Referer；系统 http JSAPI 不发自定义 header）----
//
// 背景：设备 http JSAPI（$jsapi/http）实测不发送自定义 Referer/UA —— B 站 wbi
// 搜索接口对"无浏览器 UA/Referer"的请求直接返回 v_voucher 风控（code=0 但
// data 仅含 v_voucher，无 result）→ 前端解析为空列表 → "没有找到相关视频"。
// 设备自带 curl（/bin/curl），带浏览器 UA+Referer 实测搜索正常返回结果，
// 因此本方法直接 popen 调 curl 完成请求，绕过 http JSAPI 的 header 限制。
// 入参: httpGet(url, timeoutSec) → 同步返回响应体字符串（失败返回空串）。
void GstPlayer::httpGet(JQFunctionInfo& info)
{
    JSContext* ctx = info.GetContext();
    if (info.Length() < 1 || !JS_IsString(info[0])) {
        info.GetReturnValue().ThrowTypeError("httpGet: url required");
        return;
    }
    const char* urlC = JS_ToCString(ctx, info[0]);
    if (!urlC) {
        info.GetReturnValue().ThrowTypeError("httpGet: invalid url");
        return;
    }
    std::string url(urlC);
    JS_FreeCString(ctx, urlC);

    int timeout = 10;
    if (info.Length() >= 2 && JS_IsNumber(info[1])) {
        double t = 0;
        if (JS_ToFloat64(ctx, &t, info[1]) == 0 && t > 0 && t < 120) timeout = static_cast<int>(t);
    }
    if (timeout <= 0) timeout = 10;

    // 浏览器 UA + Referer（B站搜索/防盗链必需）
    std::string ua = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
    std::string referer = "https://www.bilibili.com";

    // shell 单引号转义（URL 可能含 & 等特殊字符必须引住；单引号本身罕见，稳妥处理）
    auto shellQuote = [](const std::string& s) {
        std::string out = "'";
        for (char c : s) {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        out += "'";
        return out;
    };

    std::string cmd = "curl -s --compressed --max-time " + std::to_string(timeout)
        + " -A " + shellQuote(ua)
        + " -e " + shellQuote(referer)
        + " " + shellQuote(url);
    PLAYER_LOG("httpGet: %s", cmd.c_str());

    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) {
        PLAYER_LOG("httpGet: popen failed");
        info.GetReturnValue().Set(std::string());
        return;
    }
    std::string body;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        body.append(buf, n);
        if (body.size() > 4 * 1024 * 1024) break;  // 4MB 上限保护
    }
    int rc = pclose(fp);
    if (rc != 0) {
        PLAYER_LOG("httpGet: curl rc=%d", rc);
    }
    PLAYER_LOG("httpGet: len=%zu", body.size());
    info.GetReturnValue().Set(body);
}

// ---- 悬浮控制栏（2026-08-14）----
// 控制栏以 cairo 渲染成 ARGB 位图 → GdkPixbuf → gdkpixbufoverlay 合入视频帧，
// 因此视频 plane 置顶时控制栏仍悬浮在视频上方（JS 触摸命中由 player.vue 负责）。

// 【自动隐藏修复 2026-08-14（第二轮）】隐藏 = 设置【全透明条带】，而非清除 pixbuf。
// 真机实证：g_object_set_property 把 pixbuf 置 NULL（无论 G_TYPE_OBJECT 还是
// G_PARAM_SPEC_VALUE_TYPE 初始化 GValue）在该 gdkpixbufoverlay 实现上都不生效——
// 日志打 "hidden (overlay removed)" 但控制栏定格在画面（用户复测"还是一样"）。
// 而"设置真实 pixbuf"（g_object_set）路径一直可靠（控制栏能正常显示/刷新）。
// 故隐藏时渲染 visible=false 的全透明条带并设置：视觉上无任何内容，且复用
// 已验证可靠的设置路径，必然生效。首次设置后 barHiddenSet_ 防重复分配。
static void setTransparentOverlay(GstElement* overlay, ControlBar* bar, GdkPixbuf** slot)
{
    GdkPixbuf* tp = bar ? bar->render(false, false, false, false, 0, 0, nullptr) : nullptr;
    if (!tp) return;
    if (*slot) g_object_unref(*slot);
    *slot = tp;
    g_object_set(overlay, "pixbuf", tp, nullptr);
}

void GstPlayer::refreshBar()
{
    if (!bar_ || !voverlay_) return;
    std::lock_guard<std::mutex> lk(barMutex_);
    bool showBar = barVisible_ || barError_ || barEnded_;
    if (!showBar) {
        // 隐藏：设置全透明条带（bar + title 同显隐）。轮询期间只做一次。
        if (!barHiddenSet_) {
            setTransparentOverlay(voverlay_, bar_, &barPixbuf_);
            if (titleBar_ && vtitleoverlay_)
                setTransparentOverlay(vtitleoverlay_, titleBar_, &titlePixbuf_);
            barHiddenSet_ = true;
            PLAYER_LOG("bar hidden (transparent pixbuf set)");
        }
        return;
    }
    barHiddenSet_ = false;
    GdkPixbuf* pb = bar_->render(barVisible_, barPlaying_, barEnded_, barError_,
                                 barPosMs_, barDurMs_, nullptr);
    if (!pb) return;
    if (barPixbuf_) g_object_unref(barPixbuf_);
    barPixbuf_ = pb;
    g_object_set(voverlay_, "pixbuf", pb, nullptr);
    // 【顶部标题条 2026-08-14】与底部控制栏同频刷新；标题为空时挂透明条带
    // （drawTitle 会画半透明黑底，空标题也显示黑条 → 无标题则透明，避免黑条）
    if (titleBar_ && vtitleoverlay_ && !barTitle_.empty()) {
        GdkPixbuf* tpb = titleBar_->render(barVisible_, barPlaying_, barEnded_, barError_,
                                           barPosMs_, barDurMs_, barTitle_.c_str());
        if (tpb) {
            if (titlePixbuf_) g_object_unref(titlePixbuf_);
            titlePixbuf_ = tpb;
            g_object_set(vtitleoverlay_, "pixbuf", tpb, nullptr);
        }
    } else {
        setTransparentOverlay(vtitleoverlay_, titleBar_, &titlePixbuf_);
    }
    PLAYER_LOG("bar refreshed (visible=%d playing=%d ended=%d pos=%.0f dur=%.0f title='%.24s')",
        barVisible_ ? 1 : 0, barPlaying_ ? 1 : 0, barEnded_ ? 1 : 0, barPosMs_, barDurMs_,
        barTitle_.c_str());
}

void GstPlayer::setBarState(JQFunctionInfo& info)
{
    JSContext* ctx = info.GetContext();
    if (info.Length() < 1 || !JS_IsObject(info[0])) {
        info.GetReturnValue().Set(true);
        return;
    }
    JSValueConst opt = info[0];
    bool visible = jsGetBool(ctx, opt, "visible", barVisible_);
    bool playing = jsGetBool(ctx, opt, "playing", barPlaying_);
    bool ended = jsGetBool(ctx, opt, "ended", barEnded_);
    bool error = jsGetBool(ctx, opt, "error", barError_);
    double pos = 0, dur = 0;
    JSValue v = JS_GetPropertyStr(ctx, opt, "position");
    if (JS_IsNumber(v)) JS_ToFloat64(ctx, &pos, v);
    JS_FreeValue(ctx, v);
    v = JS_GetPropertyStr(ctx, opt, "duration");
    if (JS_IsNumber(v)) JS_ToFloat64(ctx, &dur, v);
    JS_FreeValue(ctx, v);
    // 【顶部标题 2026-08-14】title 字符串（视频标题，显示在顶部标题条）
    v = JS_GetPropertyStr(ctx, opt, "title");
    if (JS_IsString(v)) {
        size_t tlen = 0;
        const char* tstr = JS_ToCStringLen(ctx, &tlen, v);
        if (tstr) {
            barTitle_ = std::string(tstr, tlen);
            JS_FreeCString(ctx, tstr);
        }
    }
    JS_FreeValue(ctx, v);

    if (pos >= 0) barPosMs_ = pos;
    if (dur >= 0) barDurMs_ = dur;
    barVisible_ = visible;
    barPlaying_ = playing;
    barEnded_ = ended;
    barError_ = error;
    refreshBar();
    info.GetReturnValue().Set(true);
}

// ---- 管线构建（手动管线：souphttpsrc → queue → decodebin → 音视频分流）----

bool GstPlayer::buildPipeline(const std::string& uri, bool audio, const std::string& rect,
                              const std::string& fill, int canvasW, int canvasH)
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
    demux_ = gst_element_factory_make("qtdemux", "demux");       // mp4/m4s 解复用（B 站 CDN 均为 mp4 容器）
    // 【首帧加速】qtdemux 优化：快速索引、不等完整 moov 即产生 pad
    g_object_set(demux_, "faststart", true, nullptr);       // moov 在尾部时从头开始解析
    g_object_set(demux_, "push-first", true, nullptr);      // 有数据即推送，不等索引完成
    // 避免过度预读：只解析必要的索引
    g_object_set(demux_, "max-offset", (guint64)1024 * 1024, nullptr); // 1MB 偏移限制
    vparse_ = gst_element_factory_make("h264parse", "vparse");   // avcC → byte-stream，补齐帧边界
    vdec_ = gst_element_factory_make("mppvideodec", "vdec");     // RK MPP 硬解，DMA-BUF 输出，硬件旋转
    vqueue_ = gst_element_factory_make("queue", "vqueue");        // 视频后端入口缓冲：小缓冲减少首帧延迟
    g_object_set(vqueue_, "max-size-buffers", 4, nullptr);
    g_object_set(vqueue_, "max-size-bytes", 2 * 1024 * 1024, nullptr);
    g_object_set(vqueue_, "max-size-time", 0, nullptr);
    vconvert_ = gst_element_factory_make("videoconvert", "vconv"); // 格式协商缓冲（格式不匹配时兜底转换）
    // 【2026-08-14 悬浮控制栏 + 比例修复】视频链：videoscale(→内容尺寸) →
    // capsfilter(内容尺寸) → videobox(黑边补到画布) → gdkpixbufoverlay(控制栏)
    // → gdkpixbufoverlay(顶部标题, 2026-08-14) → videoconvert
    vscale_ = gst_element_factory_make("videoscale", "vscale");
    vcaps_ = gst_element_factory_make("capsfilter", "vcaps");
    vbox_ = gst_element_factory_make("videobox", "vbox");
    voverlay_ = gst_element_factory_make("gdkpixbufoverlay", "voverlay");
    vtitleoverlay_ = gst_element_factory_make("gdkpixbufoverlay", "vtitleoverlay");
    vconvert2_ = gst_element_factory_make("videoconvert", "vconv2");
    // 【2026-08-15 音频稳定版】回退 decodebin + 早期 capsfilter，用已知可用元素
    // 管线：qtdemux(audio/mpeg) → decodebin → acaps_early(S16LE/2ch, rate 由 decodebin 保留)
    //       → audioconvert(兜底) → audioresample(兜底) → volume → alsasink
    decodebin_ = gst_element_factory_make("decodebin", "adec");  // 系统自动选最优解码器
    aconvert_ = gst_element_factory_make("audioconvert", "aconv"); // 音频格式协商（兜底）
    aresample_ = gst_element_factory_make("audioresample", "ares"); // 采样率协商（兜底）
    acaps_early_ = gst_element_factory_make("capsfilter", "acaps_early"); // 强制 caps（紧跟解码器后）
    aqueue_ = gst_element_factory_make("queue", "aqueue");              // 音频缓冲 queue
    avolume_ = gst_element_factory_make("volume", "avol");       // 音量控制
    // 早期 caps：decodebin 输出强制格式+声道+交错（不锁 rate，避免 preroll 协商失败）；
    // 实测 40791444453 这条流 decodebin 实际输出 S16LE/48000/2ch，
    // 若 capsfilter 强锁 44100 会导致 preroll Internal data stream error。
    // 强制 interleaved 可避免 audioresample/alsasink 对 planar layout 不匹配导致 preroll 后无声；
    // 不锁 format/S16LE：decodebin 实际格式可能因 AAC profile 切换（如 SBR 启用）而变，
    // 强锁 S16LE 会导致 1 秒后 caps renegotiation 失败、静音。
    if (acaps_early_) {
        GstCaps* forcedCaps = gst_caps_from_string("audio/x-raw,channels=2,layout=interleaved");
        g_object_set(acaps_early_, "caps", forcedCaps, nullptr);
        gst_caps_unref(forcedCaps);
    }
    if (avolume_) {
        g_object_set(avolume_, "volume", 1.0, "mute", false, nullptr);
    }
    if (aqueue_) {
        g_object_set(aqueue_, "max-size-buffers", 50, nullptr);   // 减少音频缓冲
        g_object_set(aqueue_, "max-size-bytes", 2 * 1024 * 1024, nullptr); // 2MB
        g_object_set(aqueue_, "max-size-time", (guint64)0, nullptr);
    }
    if (!src || !queue || !demux_ || !vparse_ || !vdec_ || !vqueue_ || !vconvert_ ||
        !vscale_ || !vcaps_ || !vbox_ || !voverlay_ || !vtitleoverlay_ || !vconvert2_ ||
        !decodebin_ || !aconvert_ || !aresample_ || !acaps_early_ || !aqueue_ || !avolume_) {
        PLAYER_LOG("factory failed src=%d queue=%d demux=%d vparsed=%d vdec=%d vqueue=%d vconv=%d vscale=%d vcaps=%d vbox=%d voverlay=%d vtitleoverlay=%d vconv2=%d adec=%d aconv=%d ares=%d acaps=%d aqueue=%d avol=%d",
            src ? 1 : 0, queue ? 1 : 0, demux_ ? 1 : 0,
            vparse_ ? 1 : 0, vdec_ ? 1 : 0, vqueue_ ? 1 : 0, vconvert_ ? 1 : 0,
            vscale_ ? 1 : 0, vcaps_ ? 1 : 0, vbox_ ? 1 : 0, voverlay_ ? 1 : 0, vtitleoverlay_ ? 1 : 0, vconvert2_ ? 1 : 0,
            decodebin_ ? 1 : 0, aconvert_ ? 1 : 0, aresample_ ? 1 : 0, acaps_early_ ? 1 : 0, aqueue_ ? 1 : 0, avolume_ ? 1 : 0);
        if (src) gst_object_unref(src);
        if (queue) gst_object_unref(queue);
        if (demux_) gst_object_unref(demux_);
        if (vparse_) gst_object_unref(vparse_);
        if (vdec_) gst_object_unref(vdec_);
        if (vqueue_) gst_object_unref(vqueue_);
        if (vconvert_) gst_object_unref(vconvert_);
        if (vscale_) gst_object_unref(vscale_);
        if (vcaps_) gst_object_unref(vcaps_);
        if (vbox_) gst_object_unref(vbox_);
        if (voverlay_) gst_object_unref(voverlay_);
        if (vtitleoverlay_) gst_object_unref(vtitleoverlay_);
        if (vconvert2_) gst_object_unref(vconvert2_);
        if (decodebin_) gst_object_unref(decodebin_);
        if (aconvert_) gst_object_unref(aconvert_);
        if (aresample_) gst_object_unref(aresample_);
        if (acaps_early_) gst_object_unref(acaps_early_);
        if (aqueue_) gst_object_unref(aqueue_);
        if (avolume_) gst_object_unref(avolume_);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        demux_ = nullptr;
        vparse_ = nullptr;
        vdec_ = nullptr;
        vqueue_ = nullptr;
        vconvert_ = nullptr;
        vscale_ = nullptr;
        vcaps_ = nullptr;
        vbox_ = nullptr;
        voverlay_ = nullptr;
        vtitleoverlay_ = nullptr;
        vconvert2_ = nullptr;
        decodebin_ = nullptr;
        aconvert_ = nullptr;
        aresample_ = nullptr;
        acaps_early_ = nullptr;
        avolume_ = nullptr;
        return false;
    }

    // 网络队列：首帧优先，小缓冲快速启动；后续可动态增大。
    // 关键：max-size-time=0 保持，防止加载慢时首帧卡顿。
    g_object_set(queue, "max-size-buffers", 50, nullptr);     // 减少缓冲区数
    g_object_set(queue, "max-size-bytes", 2 * 1024 * 1024, nullptr); // 2MB 起步，首帧更快
    g_object_set(queue, "max-size-time", 0, nullptr);

    g_object_set(src, "location", uri.c_str(), nullptr);
    // 网络读块 64KB：默认 4096 会在 RK3562 上产生频繁系统调用；
    // 64KB 分块减少 ~16 倍 syscall 开销，长时间播放 CPU 占用显著下降
    g_object_set(src, "blocksize", 65536, nullptr);
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
    // 真机 modetest 平面普查（2026-08-09）：54(Esmart0, primary, z=0) /
    // 76(Esmart1, overlay, z=2) / 90(Esmart2, overlay, z=3) / 104(Esmart3, overlay, z=4)
    // 选择 plane 76 作为视频平面，基于历史实验和 Lilo 参考（注：Lilo 可能使用不同平台）。
    // 平面 76 存在且可用，与历史实验中测试的 overlay 平面一致。
g_object_set(videoSink_, "plane-id", 76, nullptr);
    // 层级控制说明：kmssink【没有 zpos 属性】（设备实证 "kmssink has no zpos
    // property"），g_object_set 永不生效。UI 主平面 54(Esmart0) 的 zpos 提升
    // 已【移出播放路径】——由 app.js onLaunch 调用 gstPlayer.preheat() 启动时
    // 全局执行一次（闪烁修复 2026-08-09：播放中动 zpos 会与合成器竞争闪屏）。
    // 视频 plane 76(Esmart1, z=2) < UI 主平面 54(z=3)：UI 永远盖在视频之上。
    // 恢复 VSYNC 约束（闪烁修复 2026-08-09 用户诊断）：skip-vsync=true 会让
    // 视频帧绕过垂直同步直接提交，与 weston 主平面叠加时高频撕裂闪烁。
    // 管线死锁已由静态后端+preroll 等待修复（1cb2b3b），此处改回 false。
    // 若关闭后重新出现状态切换失败，再回退 true 并排查 sync 时钟（备选方案）。
    gboolean skip = false;
    GParamSpec* skipPspec = g_object_class_find_property(G_OBJECT_GET_CLASS(videoSink_), "skip-vsync");
    if (skipPspec) g_object_set(videoSink_, "skip-vsync", skip, nullptr);

    // 【抗闪烁 2026-08-14】kmssink 配置加强：
    // 1) 开启 QoS（bright-screen mute fix：降低渲染压力，防止抢占 ALSA buffer）
    // 2) 设置 max-lateness=-1（无限制晚到，由 VSYNC 决定显现时机）
    // 3) sync=true（默认，显式确保基于时钟同步显现）
    gboolean qos = true;
    GParamSpec* qosPspec = g_object_class_find_property(G_OBJECT_GET_CLASS(videoSink_), "qos");
    if (qosPspec) g_object_set(videoSink_, "qos", qos, nullptr);
    gint64 maxLateness = -1;
    GParamSpec* latePspec = g_object_class_find_property(G_OBJECT_GET_CLASS(videoSink_), "max-lateness");
    if (latePspec) g_object_set(videoSink_, "max-lateness", maxLateness, nullptr);
    gboolean sync = true;
    GParamSpec* syncPspec = g_object_class_find_property(G_OBJECT_GET_CLASS(videoSink_), "sync");
    if (syncPspec) g_object_set(videoSink_, "sync", sync, nullptr);

    // 保持视频原始比例（force-aspect-ratio 默认 true）：KMS 硬件在
    // render-rectangle 划定的物理矩形内等比缩放并居中，不拉伸变形。
    // 注意：16:9 视频放进 266×960 竖条矩形时会等比缩至 472 高、上下留黑边，
    // 这是预期行为（用户方案：只修正坐标对位，不改变画面比例）。
    // 此属性保持默认 true，无需显式设置——绝不设 false（避免强制拉伸变形）。
    PLAYER_LOG("kmssink created (plane-id=76 driver=rockchip, keep-aspect-ratio, qos=on, max-lateness=-1, sync=on)");
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

    // 音频输出：alsasink（device 由 ALSA 默认决定，不显式指定）；audio=false 时用 fakesink 静音
    audioSink_ = audio
        ? gst_element_factory_make("alsasink", "asink")
        : gst_element_factory_make("fakesink", "asink");
    if (audioSink_ && audio) {
        // 2026-08-15 亮屏无声音修复：kmssink 持续渲染抢夺 ALSA buffer，导致 xrun
        // 增加 alsasink buffer 缓冲：200ms（可容纳 10ms buffer @ 60fps）
        // latency-time=100ms 给 ALSA 实时调度足够余量
        g_object_set(audioSink_, "buffer-time", (gint64)200000, "latency-time", (gint64)100000, nullptr);
        PLAYER_LOG("alsasink buffer-time=200000 latency-time=100000 (fix bright-screen mute)");
    }
    PLAYER_LOG("audio-sink created: %s", audio ? "alsasink" : "fakesink");

    // 组装：src → queue → qtdemux（动态 pad 分流：
    //   video/x-h264 → h264parse → mppvideodec(硬件旋转) → vqueue → videoconvert → videoSink_
    //   audio/*      → aacparse → avdec_aac/faad → acaps_early(S16LE/2ch/i) → aqueue(缓冲)
    //              → audioconvert(兜底) → audioresample(兜底) → volume → alsasink
    // 【2026-08-15 音频稳定版】decodebin 系统自动选 AAC/MP3 解码器，acaps_early
    // 紧跟解码器早期锁定 S16LE/2ch/i，消除沙沙声根因。
    gst_bin_add_many(GST_BIN(pipeline_), src, queue, demux_, vparse_, vdec_,
        vqueue_, vconvert_, vscale_, vcaps_, vbox_, voverlay_, vtitleoverlay_, vconvert2_,
        decodebin_, aconvert_, aresample_, acaps_early_, aqueue_, avolume_, videoSink_, audioSink_, nullptr);
    if (!gst_element_link_many(src, queue, demux_, nullptr)) {
        PLAYER_LOG("link src->qtdemux failed");
        teardown();
        return false;
    }
    // 视频后端链静态预链接（sink pad 空闲等动态 pad 接入）：
    // vparse → vdec → vqueue → videoconvert → videoscale(等比→内容尺寸) → capsfilter(内容尺寸)
    // → videobox(黑边补到画布) → gdkpixbufoverlay(悬浮控制栏) → videoconvert → sink；
    // qtdemux h264 pad 出现时只连 vparse sink。
    if (!gst_element_link_many(vparse_, vdec_, vqueue_, vconvert_, vscale_, vcaps_, vbox_,
                               voverlay_, vtitleoverlay_, vconvert2_, videoSink_, nullptr)) {
        PLAYER_LOG("link vparse->vdec->vqueue->vconv->vscale->vcaps->vbox->overlay->titleoverlay->vconv2->sink failed");
        teardown();
        return false;
    }
    // 【2026-08-14 悬浮控制栏 + 比例修复】rk 版 videoscale 无 force-aspect-ratio
    // 属性（真机 gst-inspect 实证），且 add-borders 在显式 caps 尺寸下不补边 →
    // 改为：videoscale 拉伸到【等比内容尺寸】（applyCanvasContent 计算，AR 正确），
    // videobox 补黑边到画布尺寸（= sink render-rectangle，1:1 映射）。
    g_object_set(vscale_, "add-borders", FALSE, nullptr);
    if (canvasW > 0 && canvasH > 0) {
        // 控制栏渲染器（画布竖条 = KMS 物理方向时启用旋转映射）
        canvasW_ = canvasW;
        canvasH_ = canvasH;
        PLAYER_LOG("overlay chain: vscale+vcaps(content)+vbox(%dx%d)+gdkpixbufoverlay inserted", canvasW, canvasH);
        // 默认按 16:9 源预置内容尺寸；真实尺寸在 qtdemux pad-added 时校正
        applyCanvasContent(1280, 720);
        if (!bar_) bar_ = new ControlBar();
        if (!bar_->init(canvasW_, canvasH_, canvasH_ > canvasW_, "bar")) {
            PLAYER_LOG("ControlBar init failed (%dx%d)", canvasW_, canvasH_);
        } else {
            PLAYER_LOG("ControlBar init ok (%dx%d, portrait=%d) strip=%dx%d @(%d,%d)",
                canvasW_, canvasH_, canvasH_ > canvasW_ ? 1 : 0,
                bar_->stripWidth(), bar_->stripHeight(),
                bar_->stripOffsetX(), bar_->stripOffsetY());
            // 条带定位（只合成控制栏区域，减小逐帧开销；overlay 默认按帧尺寸缩放，
            // 显式指定 width/height 使条带 1:1 映射到画布对应区域）
            g_object_set(voverlay_, "offset-x", bar_->stripOffsetX(),
                         "offset-y", bar_->stripOffsetY(),
                         "overlay-width", bar_->stripWidth(),
                         "overlay-height", bar_->stripHeight(), nullptr);
        }
        // 【顶部标题条 2026-08-14】独立 ControlBar(kind="title") + 独立
        // gdkpixbufoverlay(vtitleoverlay_)：标题条位于画布顶部（用户空间 y 0~40），
        // 与底部控制栏互不干扰；空标题时不设 pixbuf（零合成开销）。
        if (!titleBar_) titleBar_ = new ControlBar();
        if (!titleBar_->init(canvasW_, canvasH_, canvasH_ > canvasW_, "title")) {
            PLAYER_LOG("TitleBar init failed (%dx%d)", canvasW_, canvasH_);
        } else {
            PLAYER_LOG("TitleBar init ok (%dx%d, portrait=%d) strip=%dx%d @(%d,%d)",
                canvasW_, canvasH_, canvasH_ > canvasW_ ? 1 : 0,
                titleBar_->stripWidth(), titleBar_->stripHeight(),
                titleBar_->stripOffsetX(), titleBar_->stripOffsetY());
            g_object_set(vtitleoverlay_, "offset-x", titleBar_->stripOffsetX(),
                         "offset-y", titleBar_->stripOffsetY(),
                         "overlay-width", titleBar_->stripWidth(),
                         "overlay-height", titleBar_->stripHeight(), nullptr);
        }
        // 初始叠加帧：透明（等待 JS setBarState 首绘）
        barVisible_ = true;
        barPlaying_ = false;
        barEnded_ = false;
        barError_ = false;
        barPosMs_ = 0.0;
        barDurMs_ = 0.0;
        refreshBar();
        PLAYER_LOG("overlay initial pixbuf set");
    } else {
        canvasW_ = 0;
        canvasH_ = 0;
        PLAYER_LOG("no canvas rect, overlay disabled");
    }
    // 音频后端链静态预链接：decodebin 音频 pad 出现时连 acaps_early sink。
    // 【2026-08-15 音频稳定版】decodebin → acaps_early(S16LE/2ch/i) → aqueue → aconvert → aresample → avolume → alsasink
    if (!gst_element_link_many(acaps_early_, aqueue_, aconvert_, aresample_, avolume_, audioSink_, nullptr)) {
        PLAYER_LOG("link acaps_early->aqueue->aconv->ares->avol->asink failed");
        teardown();
        return false;
    }
    // 【2026-08-15 详细 caps 协商日志】排查沙沙声根因
    {
        GstPad* acapsEarlySrcPad = gst_element_get_static_pad(acaps_early_, "src");
        GstPad* aconvSinkPad = gst_element_get_static_pad(aconvert_, "sink");
        if (acapsEarlySrcPad) {
            GstCaps* caps = gst_pad_get_current_caps(acapsEarlySrcPad);
            if (caps) {
                PLAYER_LOG("audio caps after acaps_early: %s", gst_caps_to_string(caps));
                gst_caps_unref(caps);
            }
            gst_object_unref(acapsEarlySrcPad);
        }
        if (aconvSinkPad) {
            GstCaps* caps = gst_pad_get_current_caps(aconvSinkPad);
            if (caps) {
                PLAYER_LOG("audio caps at aconvert sink: %s", gst_caps_to_string(caps));
                gst_caps_unref(caps);
            }
            gst_object_unref(aconvSinkPad);
        }
    }
#ifdef KMSSINK_TEST
    // mppvideodec 硬件旋转（消灭 videoflip CPU 逐帧旋转 + 一次 DMA 拷贝）：
    // kmssink 直出物理 plane，不经 weston rotate-90 变换，所有视频必须顺时针转 90°
    // 与 UI 同向。
    // 【方向校准 2026-08-09 真机】rotation=90 实测画面颠倒 180°：
    // mpp 的 rotation=90 语义是【逆时针 90°】（=videoflip method=1），而旧 videoflip
    // method=3 是顺时针 90°，两者相差 180°（用户反馈"视频反了 180°"实证）。
    // 因此顺时针 90° 必须写 rotation=270。编码器顺时针=CW90；mpp 语义 CCW90=270。
    {
        GParamSpec* rotPspec = g_object_class_find_property(G_OBJECT_GET_CLASS(vdec_), "rotation");
        if (rotPspec) {
            g_object_set(vdec_, "rotation", 270, nullptr);
            PLAYER_LOG("mppvideodec rotation=270 set (cw90, calibrated on device)");
        } else {
            PLAYER_LOG("mppvideodec has no rotation property (keep videoflip path?)");
        }
    }
#else
    // 非 KMSSINK：旋转默认 0，由 onQtdemuxPadAdded 按 h>w 竖屏动态设 270
#endif
    g_signal_connect(demux_, "pad-added", G_CALLBACK(GstPlayer::qtdemuxPadAddedCb), this);
    // 音频解码链：decodebin（AAC/MP3 → raw）→ acaps_early(S16LE/2ch/i) → audioconvert → audioresample → volume → alsasink
    g_signal_connect(decodebin_, "pad-added", G_CALLBACK(GstPlayer::decodebinPadAddedCb), this);
    PLAYER_LOG("pipeline linked (qtdemux>vparse>mppvideodec>vqueue>videoconvert:kmssink | audio>decodebin>acaps_early(S16LE/2ch/i)>audioconvert>audioresample>volume>alsasink)");

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

// ---- 画布内容几何（2026-08-14 比例修复）----
// 视频链：videoscale 拉伸源 → 【等比内容尺寸】（AR 正确，因尺寸按源比例计算）
// → videobox 补黑边 → 画布尺寸（= sink render-rectangle，1:1）。
// 设备 rk 版 videoscale 无 force-aspect-ratio 属性且 add-borders 不可靠（真机
// gst-inspect 实证），故比例由本函数用真实源尺寸保证，不依赖 videoscale。
void GstPlayer::applyCanvasContent(int srcW, int srcH)
{
    if (!vcaps_ || !vbox_ || canvasW_ <= 0 || canvasH_ <= 0) return;
    if (srcW <= 0 || srcH <= 0) return;
    // 旋转处理：KMSSINK 构建 mppvideodec rotation=270 → 显示尺寸交换（竖屏）
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
        "height", G_TYPE_INT, contentH, nullptr);
    g_object_set(vcaps_, "caps", caps, nullptr);
    gst_caps_unref(caps);
    // 注意：videobox 的 left/right/top/bottom 为【负数】时加边框（正数裁剪）
    g_object_set(vbox_, "left", -borderL, "right", -borderR, "top", -borderT, "bottom", -borderB, nullptr);
    appliedSrcW_ = srcW;
    appliedSrcH_ = srcH;
    PLAYER_LOG("canvas content: src=%dx%d disp=%dx%d -> content=%dx%d borders L%d R%d T%d B%d",
        srcW, srcH, dispW, dispH, contentW, contentH, borderL, borderR, borderT, borderB);
}

// qtdemux pad-added 静态回调 → 转成员函数（userdata=this）
void GstPlayer::qtdemuxPadAddedCb(GstElement* element, GstPad* pad, gpointer userdata)
{
    GstPlayer* self = static_cast<GstPlayer*>(userdata);
    self->onQtdemuxPadAdded(pad);
}

// qtdemux 动态 pad 分流：
//   video/x-h264 → vparse_(h264parse) 的静态预链（vparse→vdec→videoSink 已在 buildPipeline 接好）
//   video/* 其它（av1/hevc/vp9 等）→ 交给 decodebin_ 兜底解码（其 pad-added 再分流到 sink）
//   audio/*       → decodebin_（AAC → raw → alsasink）
void GstPlayer::onQtdemuxPadAdded(GstPad* pad)
{
    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps) {
        PLAYER_LOG("qtdemux pad-added: no caps");
        return;
    }
    const GstStructure* s = gst_caps_get_structure(caps, 0);
    const gchar* media = s ? gst_structure_get_name(s) : nullptr;  // "video/x-h264" / "audio/mpeg"
    PLAYER_LOG("qtdemux pad-added: %s", media ? media : "?");
    GstElement* sink = nullptr;
    if (media && g_str_has_prefix(media, "video/")) {
        // 【竖屏支持 2026-08-14】所有视频路径统一做源尺寸核对（h264 主路径此前遗漏，
        // 导致 9:16 视频不触发重建、被按 16:9 拉伸）。
        // 【关键：提前应用】在 pad-added 即刻用真实源尺寸调用 applyCanvasContent，
        // 避免 start() 中 rebuildForSource() 导致的“先横后竖”闪烁。
        gint vw = 0, vh = 0;
        gst_structure_get_int(s, "width", &vw);
        gst_structure_get_int(s, "height", &vh);
        if (vw > 0 && vh > 0 && (vw != appliedSrcW_ || vh != appliedSrcH_)) {
            // 直接应用真实尺寸，无需完整重建管线
            applyCanvasContent(vw, vh);
            PLAYER_LOG("early applyCanvasContent %dx%d (was %dx%d)",
                vw, vh, appliedSrcW_, appliedSrcH_);
        }
        // H.264 → 显式硬解链（mppvideodec rotation 硬件旋转，KMSSINK 下已在 build 时设 90）
        if (g_str_equal(media, "video/x-h264")) {
#ifndef KMSSINK_TEST
            // waylandsink 路径：竖屏视频（高>宽）才旋转 90°，与旧 videoflip 行为一致
            // （KMSSINK_TEST 下已在 buildPipeline 统一设 rotation=270）
            gint w = 0, h = 0;
            gst_structure_get_int(s, "width", &w);
            gst_structure_get_int(s, "height", &h);
            if (h > w) {
                GParamSpec* rotPspec = g_object_class_find_property(G_OBJECT_GET_CLASS(vdec_), "rotation");
                if (rotPspec) {
                    g_object_set(vdec_, "rotation", 270, nullptr);
                    PLAYER_LOG("mppvideodec rotation=270 (portrait h>w %dx%d)", w, h);
                }
            }
#endif
            sink = vparse_;
            // 【静态后端+动态前端（用户方案 2026-08-09）】vparse→vdec→vqueue→
            // videoconvert→videoSink 已在 buildPipeline 静态链接完毕，此处只做
            // pad 级连接。绝不复用 3fc0948 的 lazy-link（vdec→videoSink 在
            // PAUSED/PLAYING 状态切换途中元素级链接 → 真机全线 futex 死锁、
            // 画面冻结第一帧）。非 h264 fallback 路径（decodebin）亦静态指向
            // vqueue，两条视频路径共用同一条静态后端，互不占用彼此 sink pad。
            PLAYER_LOG("video/x-h264 -> h264parse (mpp hw chain, static backend)");
        } else {
            // 非 h264（av1/hevc 等）：decodebin 兜底；KMSSINK 下视频输出需与 UI 同向，
            // onDecodebinPadAdded 的 video 分支会按需插 videoflip（与硬解链互斥，只此路径触发）
            sink = decodebin_;
            PLAYER_LOG("video %s -> decodebin fallback", media);
        }
    } else if (media && g_str_has_prefix(media, "audio/")) {
        // 【2026-08-15 音频稳定版】音频接入 decodebin（系统自动选最优解码器）
        sink = decodebin_;
        PLAYER_LOG("audio %s -> decodebin (stable fallback)", media);
    }
    if (sink) {
        GstPad* sinkPad = gst_element_get_static_pad(sink, "sink");
        if (sinkPad) {
            GstPadLinkReturn r = gst_pad_link(pad, sinkPad);
            PLAYER_LOG("qtdemux pad link ret=%d", r);
            gst_object_unref(sinkPad);
        } else {
            PLAYER_LOG("sink pad not found for %s", media ? media : "?");
        }
    } else {
        PLAYER_LOG("no sink for %s", media ? media : "?");
    }
    gst_caps_unref(caps);
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
    // 【2026-08-15 详细 caps 日志】排查沙沙声根因
    if (media && g_str_has_prefix(media, "audio/")) {
        PLAYER_LOG("decodebin audio caps: %s", gst_caps_to_string(caps));
    }
    GstElement* sink = nullptr;
    if (media && g_str_has_prefix(media, "video/")) {
        // 非 h264 fallback 视频：接入静态视频后端入口 vqueue_（vqueue→videoconvert→
        // kmssink 已在 buildPipeline 静态链接）。旋转由 mppvideodec 硬链负责；
        // fallback（av1/hevc 软解）极少触发（API 已锁 codecid=7），保持直通即可。
        // （尺寸核对/重建请求已在 onQtdemuxPadAdded video/ 分支统一处理）
        gint w = 0, h = 0;
        gst_structure_get_int(s, "width", &w);
        gst_structure_get_int(s, "height", &h);
        sink = vqueue_;
        PLAYER_LOG("video %dx%d fallback -> vqueue (static backend)", w, h);
    } else if (media && g_str_has_prefix(media, "audio/")) {
        // 【2026-08-15 音频稳定版】音频解码输出 → 静态音频后端入口 acaps_early(S16LE/44100/2ch)。
        // 后端已在 buildPipeline 以 gst_element_link_many(acaps_early, aconvert, ...) 静态预链接，
        // 因此 acaps_early_.src 已被 aconvert_.sink 占用，不得再次 seeking 到 aconvert_。
        sink = acaps_early_;
        PLAYER_LOG("audio raw -> acaps_early (static backend)");
    }
    if (sink) {
        GstPad* sinkPad = gst_element_get_static_pad(sink, "sink");
        if (sinkPad) {
            GstPadLinkReturn r = gst_pad_link(pad, sinkPad);
            PLAYER_LOG("pad link ret=%d", r);
            if (r != GST_PAD_LINK_OK) {
                // 链接失败：保存该 pad 供稍后重试（先释放旧 pending，避免泄漏）
                if (pendingAudioPad_) {
                    gst_object_unref(pendingAudioPad_);
                }
                pendingAudioPad_ = pad;
                gst_object_ref(pendingAudioPad_);
                // 【2026-08-15 断流排查】链接失败多因下游已处于 PLAYING 而元素还
                // 在 NULL/READY（decodebin 动态造 pad 的竞态）。把目标 sink 先置
                // READY 再重试一次，多数可解。
                gst_element_set_state(sink, GST_STATE_READY);
                GstPadLinkReturn r2 = gst_pad_link(pad, sinkPad);
                PLAYER_LOG("pad link retry=%d", r2);
                if (r2 == GST_PAD_LINK_OK) {
                    gst_object_unref(pendingAudioPad_);
                    pendingAudioPad_ = nullptr;
                }
            } else {
                // 链接成功：清除历史 pending
                if (pendingAudioPad_) {
                    gst_object_unref(pendingAudioPad_);
                    pendingAudioPad_ = nullptr;
                }
            }
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
    demux_ = nullptr;
    vparse_ = nullptr;
    vdec_ = nullptr;
    vqueue_ = nullptr;
    vconvert_ = nullptr;
    vscale_ = nullptr;
    vcaps_ = nullptr;
    vbox_ = nullptr;
    voverlay_ = nullptr;
    vtitleoverlay_ = nullptr;
    vconvert2_ = nullptr;
    decodebin_ = nullptr;
    aconvert_ = nullptr;
    aresample_ = nullptr;
    acaps_early_ = nullptr;
    aqueue_ = nullptr;
    avolume_ = nullptr;
    videoSink_ = nullptr;
    audioSink_ = nullptr;
    videoFlip_ = nullptr;
    if (pendingAudioPad_) {
        gst_object_unref(pendingAudioPad_);
        pendingAudioPad_ = nullptr;
    }
    if (barPixbuf_) {
        g_object_unref(barPixbuf_);
        barPixbuf_ = nullptr;
    }
    if (titlePixbuf_) {
        g_object_unref(titlePixbuf_);
        titlePixbuf_ = nullptr;
    }
    if (bar_) {
        delete bar_;
        bar_ = nullptr;
    }
    if (titleBar_) {
        delete titleBar_;
        titleBar_ = nullptr;
    }
    barTitle_.clear();
    canvasW_ = 0;
    canvasH_ = 0;
    seeking_ = false;
    seekTargetMs_ = 0.0;
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
            static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR | GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_STREAM_STATUS | GST_MESSAGE_TAG));
        if (!msg) continue;

        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            PLAYER_LOG("bus EOS");
            barEnded_ = true;
            barPlaying_ = false;
            refreshBar();
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
            barError_ = true;
            barPlaying_ = false;
            refreshBar();
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
                    barPlaying_ = true;
                    refreshBar();
                    emitState("playing");
                } else if (newState == GST_STATE_PAUSED) {
                    barPlaying_ = false;
                    refreshBar();
                    emitState("paused");
                }
            }
            break;
        }
        case GST_MESSAGE_ASYNC_DONE: {
            // 【防��回 2026-08-14】FLUSH seek 完成（管线已����并重启到新位置）。
            // ��除 seeking_，后续 getPosition() ���复查�� pipeline 真实位置。
            if (seeking_) {
                PLAYER_LOG("bus ASYNC_DONE: seek done, clearing seeking_");
                seeking_ = false;
                seekTargetMs_ = 0.0;
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

// 【seek 自测 2026-08-14】模块加载时若存在 /tmp/autoseek.flag 则自动运行：
// 用真实链（含 videobox/内容 caps/kmssink）播放 /tmp/video.mp4 并执行 FLUSH seek，
// 日志输出 seek 前后位置 —— 在 miniapp 进程内复现 app 的 seek 行为（无需用户/独立进程）。
static void runSeekSelfTest()
{
    FILE* f = fopen("/tmp/autoseek.flag", "r");
    if (!f) return;
    char variant[64] = "vbox-contentcaps";
    if (fgets(variant, sizeof(variant), f)) {
        size_t n = strlen(variant);
        while (n > 0 && (variant[n - 1] == '\n' || variant[n - 1] == '\r')) variant[--n] = 0;
    }
    fclose(f);
    remove("/tmp/autoseek.flag");
    PLAYER_LOG("=== seek self-test variant=%s ===", variant);

    const char* path = "/tmp/video.mp4";
    bool useSoup = (strstr(variant, "soup") != NULL);
    GstElement* pipe = gst_pipeline_new("selftest");
    GstElement* src = useSoup
        ? gst_element_factory_make("souphttpsrc", "src")
        : gst_element_factory_make("filesrc", "src");
    GstElement* demux = gst_element_factory_make("qtdemux", "demux");
    GstElement* vparse = gst_element_factory_make("h264parse", "vp");
    GstElement* vdec = gst_element_factory_make("mppvideodec", "vd");
    GstElement* vqueue = gst_element_factory_make("queue", "vq");
    GstElement* vconv = gst_element_factory_make("videoconvert", "vc");
    GstElement* vscale = gst_element_factory_make("videoscale", "vs");
    GstElement* vcaps = gst_element_factory_make("capsfilter", "vcaps");
    GstElement* vbox = NULL;
    GstElement* voverlay = NULL;
    GstElement* vconv2 = gst_element_factory_make("videoconvert", "vc2");
    GstElement* vsink = NULL;
    GstElement* adec = gst_element_factory_make("decodebin", "adec");
    GstElement* aconv = gst_element_factory_make("audioconvert", "ac");
    GstElement* ares = gst_element_factory_make("audioresample", "ar");
    GstElement* asink = gst_element_factory_make("fakesink", "asink");
    bool useVbox = (strstr(variant, "vbox") != NULL);
    bool useContent = (strstr(variant, "content") != NULL);
    bool useKms = (strstr(variant, "kmssink") != NULL);
    bool useOverlay = (strstr(variant, "overlay") != NULL);
    if (useVbox) vbox = gst_element_factory_make("videobox", "vbox");
    if (useOverlay) voverlay = gst_element_factory_make("gdkpixbufoverlay", "voverlay");
    vsink = useKms
        ? gst_element_factory_make("kmssink", "vsink")
        : gst_element_factory_make("fakesink", "vsink");
    if (!pipe || !src || !demux || !vparse || !vdec || !vqueue || !vconv || !vscale ||
        !vcaps || !vconv2 || !vsink || !adec || !aconv || !ares || !asink ||
        (useVbox && !vbox) || (useOverlay && !voverlay)) {
        PLAYER_LOG("selftest factory failed");
        return;
    }
    if (useSoup) {
        // 走本地代理（与 app 完全一致：GstProxy 白名单 + Range + Referer），
        // 验证 seek 在代理路径下的行为
        FILE* uf = fopen("/tmp/playurl2.txt", "r");
        char urlbuf[2048] = "";
        if (uf) {
            if (fgets(urlbuf, sizeof(urlbuf), uf)) {
                size_t n = strlen(urlbuf);
                while (n > 0 && (urlbuf[n - 1] == '\n' || urlbuf[n - 1] == '\r')) urlbuf[--n] = 0;
            }
            fclose(uf);
        }
        if (!*urlbuf) {
            PLAYER_LOG("selftest soup: no /tmp/playurl2.txt");
            return;
        }
        std::string rawUrl(urlbuf);
        std::string proxied = proxy::maybeRewrite(rawUrl);
        g_object_set(src, "location", proxied.c_str(), NULL);
        g_object_set(src, "user-agent",
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
            NULL);
        g_object_set(src, "timeout", 15, NULL);
        PLAYER_LOG("selftest soup(proxy) url: %.120s", proxied.c_str());
    } else {
        g_object_set(src, "location", path, NULL);
    }
    g_object_set(vqueue, "max-size-buffers", 8, "max-size-bytes", 4 * 1024 * 1024, "max-size-time", 0, NULL);
    g_object_set(vscale, "add-borders", FALSE, NULL);
    if (useContent) {
        GstCaps* caps = gst_caps_new_simple("video/x-raw", "width", G_TYPE_INT, 266, "height", G_TYPE_INT, 473, NULL);
        g_object_set(vcaps, "caps", caps, NULL);
        gst_caps_unref(caps);
    } else {
        GstCaps* caps = gst_caps_new_simple("video/x-raw", "width", G_TYPE_INT, 266, "height", G_TYPE_INT, 960, NULL);
        g_object_set(vcaps, "caps", caps, NULL);
        gst_caps_unref(caps);
    }
    if (useVbox) g_object_set(vbox, "left", 0, "right", 0, "top", -243, "bottom", -244, NULL);
    if (useKms) {
        g_object_set(vsink, "plane-id", 75, "driver-name", "rockchip", NULL);
        gst_util_set_object_arg(G_OBJECT(vsink), "render-rectangle", "<107, 0, 266, 960>");
    }
    if (useOverlay) {
        g_object_set(voverlay, "location", "/tmp/bar.png",
                     "offset-x", 190, "offset-y", 0,
                     "overlay-width", 76, "overlay-height", 960, NULL);
    }

    gst_bin_add_many(GST_BIN(pipe), src, demux, vparse, vdec, vqueue, vconv, vscale, vcaps,
        vconv2, vsink, adec, aconv, ares, asink, NULL);
    if (vbox) gst_bin_add(GST_BIN(pipe), vbox);
    if (voverlay) gst_bin_add(GST_BIN(pipe), voverlay);
    gst_element_link_many(src, demux, NULL);
    gst_element_link_many(vparse, vdec, vqueue, vconv, vscale, vcaps, NULL);
    // 链尾：caps → (vbox) → (overlay) → vconv2 → sink
    if (vbox) {
        if (voverlay) gst_element_link_many(vcaps, vbox, voverlay, vconv2, vsink, NULL);
        else gst_element_link_many(vcaps, vbox, vconv2, vsink, NULL);
    } else {
        if (voverlay) gst_element_link_many(vcaps, voverlay, vconv2, vsink, NULL);
        else gst_element_link_many(vcaps, vconv2, vsink, NULL);
    }
    gst_element_link_many(aconv, ares, asink, NULL);

    GstPad* vp = gst_element_get_static_pad(vparse, "sink");
    GstPad* ap = gst_element_get_static_pad(adec, "sink");
    GstPad* ac = gst_element_get_static_pad(aconv, "sink");
    g_signal_connect(demux, "pad-added", G_CALLBACK(+[](GstElement*, GstPad* pad, gpointer ud) {
        GstCaps* caps = gst_pad_get_current_caps(pad);
        const GstStructure* s = caps ? gst_caps_get_structure(caps, 0) : NULL;
        const gchar* media = s ? gst_structure_get_name(s) : NULL;
        GstPad* t = NULL;
        if (media && g_str_has_prefix(media, "video/")) t = (GstPad*)g_object_get_data(G_OBJECT(ud), "vp");
        else if (media && g_str_has_prefix(media, "audio/")) t = (GstPad*)g_object_get_data(G_OBJECT(ud), "ap");
        if (t) gst_pad_link(pad, t);
        if (caps) gst_caps_unref(caps);
    }), pipe);
    g_signal_connect(adec, "pad-added", G_CALLBACK(+[](GstElement*, GstPad* pad, gpointer ud) {
        GstPad* t = (GstPad*)g_object_get_data(G_OBJECT(ud), "ac");
        gst_pad_link(pad, t);
    }), pipe);
    g_object_set_data(G_OBJECT(pipe), "vp", vp);
    g_object_set_data(G_OBJECT(pipe), "ap", ap);
    g_object_set_data(G_OBJECT(pipe), "ac", ac);
    gst_object_unref(vp);
    gst_object_unref(ap);
    gst_object_unref(ac);

    gst_element_set_state(pipe, GST_STATE_PLAYING);
    GstState st;
    gst_element_get_state(pipe, &st, NULL, 12 * GST_SECOND);
    PLAYER_LOG("selftest state=%s", gst_element_state_get_name(st));
    g_usleep(4 * 1000000);
    gint64 pos = 0;
    gst_element_query_position(pipe, GST_FORMAT_TIME, &pos);
    PLAYER_LOG("selftest pos before seek: %lld ms", (long long)(pos / 1000000));
    bool ok = gst_element_seek_simple(pipe, GST_FORMAT_TIME,
        (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), 60LL * GST_SECOND);
    PLAYER_LOG("selftest seek(60s) ret=%d", ok ? 1 : 0);
    g_usleep(3 * 1000000);
    gst_element_query_position(pipe, GST_FORMAT_TIME, &pos);
    PLAYER_LOG("selftest pos after seek: %lld ms (期望~63000)", (long long)(pos / 1000000));
    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
    PLAYER_LOG("=== seek self-test done ===");
}

static JSValue createGstPlayer(JQModuleEnv* env)
{
    // 预热 GStreamer：模块加载（app 启动 import gstplayer）即完成 gst_init，
    // 把插件扫描开销从首次 open 播放路径上移走，缩短首帧延迟
    ensureGstInit();
    // 预热悬浮控制栏依赖库（cairo/gdk-pixbuf/fontconfig，RTLD_GLOBAL），
    // 避免首次使用时的懒解析崩溃（2026-08-14 闪退修复，见 ControlBar.cpp）
    ensureOverlayLibsGlobal();
    // 【调试】/tmp/autoseek.flag 存在时运行 seek 自测（adb 可控，日志走 local7）
    runSeekSelfTest();

    JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "gstPlayer");
    tpl->InstanceTemplate()->setObjectCreator([]() {
        static GstPlayer* player = []() {
            GstPlayer* instance = new GstPlayer();
            instance->REF();
            return instance;
        }();
        return player;
    });

    tpl->SetProtoMethod("preheat", &GstPlayer::preheat);
    tpl->SetProtoMethod("open", &GstPlayer::open);
    tpl->SetProtoMethod("start", &GstPlayer::start);
    tpl->SetProtoMethod("setVideoZpos", &GstPlayer::setVideoZpos);
    tpl->SetProtoMethod("pause", &GstPlayer::pause);
    tpl->SetProtoMethod("resume", &GstPlayer::resume);
    tpl->SetProtoMethod("close", &GstPlayer::close);
    tpl->SetProtoMethod("getDuration", &GstPlayer::getDuration);
    tpl->SetProtoMethod("getPosition", &GstPlayer::getPosition);
    tpl->SetProtoMethod("seek", &GstPlayer::seek);
    tpl->SetProtoMethod("setRect", &GstPlayer::setRect);
    tpl->SetProtoMethod("setBarState", &GstPlayer::setBarState);
    tpl->SetProtoMethod("httpGet", &GstPlayer::httpGet);

    // JS 侧: gstPlayer.stateChanged.on(cb) / .off(cb)
    tpl->InstanceTemplate()->Set("stateChanged", &GstPlayer::stateChanged);

    return tpl->CallConstructor();
}

void gstplayer_init(JQModuleEnv* env)
{
    env->setModuleExport("gstPlayer", createGstPlayer(env));
}

}  // namespace gstplayer



