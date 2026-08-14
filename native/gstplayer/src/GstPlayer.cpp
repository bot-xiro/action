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
    // KMSSINK 双平面模式：前端传逻辑全屏矩形（960×480），换算为物理坐标。
    // 前端 hole（960×480 全屏挖洞）与 render-rectangle 必须一致，UI 可视区域
    // 由 WebView 层 hole 决定：hole 区域透明，KMS 视频平面透出，其余被 UI 覆盖。
    // 历史教训（勿回退）：曾用 logicToCrtc 把 266 高竖条换算成物理竖条
    // （x=0 上半屏、x=214 下半屏，两轮真机均反馈位置错误）——根因是前端
    // 视区高度写死 266（只占逻辑屏上半）。现前端统一 960×480 全屏传参。
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
    // 【2026-08-11 用户指令】视频置底策略：UI 主平面抬 zpos=1（0→1）。
    // 视频 plane 76 zpos=0（见 open()）与 UI 同层竞争不确定，故 UI 抬到 1
    // 保证控制栏（UI 平面内）确定盖住视频；后续 WebView 挖洞成功后，
    // 洞区域透明 → 视频从洞中透出，控制栏仍悬浮在视频上方。
    // 历史教训：曾抬 zpos=3（2026-08-09）依赖 hole 透出但 JQuick 不支持
    // hole → 视频永远被盖 → 黑屏事故；本次是用户明确要求的"视频置底 +
    // 挖洞"方案的基线，语义不同（先验证控制栏可见，再做挖洞）。
    // 本方法幂等（app.js onLaunch 调用一次）。
    static std::atomic<bool> done{false};
    if (!done.exchange(true)) {
        if (!setPlaneZpos(54, 1)) {
            PLAYER_LOG("preheat WARN: UI plane 54 zpos set failed (UI default zpos=0, video may cover UI)");
        }
    }
    info.GetReturnValue().Set(true);
}

// 【2026-08-11 动态层级】运行时切换视频 plane 76 的 zpos：
// 前端控制栏显隐联动（见 player.vue showControls/hideControls/playing 分支）：
//   - 播放中：setVideoZpos(3) 视频置顶 → 全屏可见（盖过 UI，zpos 3 > UI 1）
//   - 控制栏唤出：setVideoZpos(0) 视频置底 → UI 控制栏可操作（zpos 1 > 0）
// 背景：UI 层恒 XR24 不透明且 JQuick 不支持 hole，双平面下"视频可见"与
// "控制栏可操作"互斥，只能动态切换层级（调用即时生效，DRM 属性内核态持久）。
// 入参: setVideoZpos(3) 数字；或 setVideoZpos({zpos:3}) 对象。
// 非 KMSSINK 构建（waylandsink 单平面）下无需层级切换，直接忽略。
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
        PLAYER_LOG("setVideoZpos: plane 76 zpos=%d OK", zpos);
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
    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    // 【2026-08-11 嵌进播放器】不再在此强制置底！历史遗留（2bdb63e "视频置底 +
    // 挖洞"方案）曾 setPlaneZpos(76, 0)，会把刚由 JS setVideoZpos(3) 置顶的视频
    // 又拉回底层 → 被 UI 不透明平面盖住 → 画面消失（真机实证 21:26：stateChanged
    // playing 但用户看不到视频）。
    // 现在视频 zpos 完全由 JS 控制（open 后 setVideoTopmost(true) = zpos=3 置顶，
    // 视频只覆盖中间条，控制栏上下条在视频 plane 外不受影响）。
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

void GstPlayer::refreshBar()
{
    if (!bar_ || !voverlay_) return;
    std::lock_guard<std::mutex> lk(barMutex_);
    // 隐藏且无错误/结束标记：无需重绘（进度轮询期间省 CPU/内存分配）
    if (!barVisible_ && !barError_ && !barEnded_) {
        // 隐藏：移除叠加（pixbuf=NULL，gdkpixbufoverlay 跳过合成，零开销）。
        // 注意 g_object_set 传 NULL 会被当作参数终止符（对象属性不能用 NULL 值），
        // 必须用 GValue + g_object_set_property。
        if (barPixbuf_) {
            GValue gv = G_VALUE_INIT;
            g_value_init(&gv, G_TYPE_OBJECT);
            g_value_set_object(&gv, nullptr);
            g_object_set_property(G_OBJECT(voverlay_), "pixbuf", &gv);
            g_value_unset(&gv);
            g_object_unref(barPixbuf_);
            barPixbuf_ = nullptr;
            PLAYER_LOG("bar hidden (overlay removed)");
        }
        return;
    }
    GdkPixbuf* pb = bar_->render(barVisible_, barPlaying_, barEnded_, barError_,
                                 barPosMs_, barDurMs_);
    if (!pb) return;
    if (barPixbuf_) g_object_unref(barPixbuf_);
    barPixbuf_ = pb;
    g_object_set(voverlay_, "pixbuf", pb, nullptr);
    PLAYER_LOG("bar refreshed (visible=%d playing=%d ended=%d pos=%.0f dur=%.0f)",
        barVisible_ ? 1 : 0, barPlaying_ ? 1 : 0, barEnded_ ? 1 : 0, barPosMs_, barDurMs_);
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
    vparse_ = gst_element_factory_make("h264parse", "vparse");   // avcC → byte-stream，补齐帧边界
    vdec_ = gst_element_factory_make("mppvideodec", "vdec");     // RK MPP 硬解，DMA-BUF 输出，硬件旋转
    vqueue_ = gst_element_factory_make("queue", "vqueue");        // 视频后端入口缓冲（防 decode 动态接入反压网络源）
    g_object_set(vqueue_, "max-size-buffers", 8, nullptr);
    g_object_set(vqueue_, "max-size-bytes", 4 * 1024 * 1024, nullptr);
    g_object_set(vqueue_, "max-size-time", 0, nullptr);
    vconvert_ = gst_element_factory_make("videoconvert", "vconv"); // 格式协商缓冲（格式不匹配时兜底转换）
    // 【2026-08-14 悬浮控制栏 + 比例修复】视频链：videoscale(→内容尺寸) →
    // capsfilter(内容尺寸) → videobox(黑边补到画布) → gdkpixbufoverlay(控制栏) → videoconvert
    vscale_ = gst_element_factory_make("videoscale", "vscale");
    vcaps_ = gst_element_factory_make("capsfilter", "vcaps");
    vbox_ = gst_element_factory_make("videobox", "vbox");
    voverlay_ = gst_element_factory_make("gdkpixbufoverlay", "voverlay");
    vconvert2_ = gst_element_factory_make("videoconvert", "vconv2");
    decodebin_ = gst_element_factory_make("decodebin", "adec");  // 音频解码链（AAC/MP3 → raw）
    aconvert_ = gst_element_factory_make("audioconvert", "aconv"); // 音频格式协商（raw caps 与 alsasink 解耦）
    aresample_ = gst_element_factory_make("audioresample", "ares"); // 采样率协商（44.1k/48k 自适应）
    if (!src || !queue || !demux_ || !vparse_ || !vdec_ || !vqueue_ || !vconvert_ ||
        !vscale_ || !vcaps_ || !vbox_ || !voverlay_ || !vconvert2_ ||
        !decodebin_ || !aconvert_ || !aresample_) {
        PLAYER_LOG("factory failed src=%d queue=%d demux=%d vparsed=%d vdec=%d vqueue=%d vconv=%d vscale=%d vcaps=%d vbox=%d voverlay=%d vconv2=%d adec=%d aconv=%d ares=%d",
            src ? 1 : 0, queue ? 1 : 0, demux_ ? 1 : 0,
            vparse_ ? 1 : 0, vdec_ ? 1 : 0, vqueue_ ? 1 : 0, vconvert_ ? 1 : 0,
            vscale_ ? 1 : 0, vcaps_ ? 1 : 0, vbox_ ? 1 : 0, voverlay_ ? 1 : 0, vconvert2_ ? 1 : 0,
            decodebin_ ? 1 : 0, aconvert_ ? 1 : 0, aresample_ ? 1 : 0);
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
        if (vconvert2_) gst_object_unref(vconvert2_);
        if (decodebin_) gst_object_unref(decodebin_);
        if (aconvert_) gst_object_unref(aconvert_);
        if (aresample_) gst_object_unref(aresample_);
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
        vconvert2_ = nullptr;
        decodebin_ = nullptr;
        aconvert_ = nullptr;
        aresample_ = nullptr;
        return false;
    }

    // 网络队列缓冲上限（RK3562 内存带宽有限，防缓冲无限膨胀）。
    // 注意 max-size-time 必须显式设为 0：默认 2s 会在加载慢时提前打满，
    // 解码器反压到网络源导致首帧卡顿（历史踩坑，勿改）。
    g_object_set(queue, "max-size-buffers", 200, nullptr);
    g_object_set(queue, "max-size-bytes", 16 * 1024 * 1024, nullptr);
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
    // —— 不存在 plane 75！之前用 75 导致 kmssink 报 "Could not find a plane
    // for crtc" 打开失败。76 与历史实验中实测可用的 overlay 平面一致，选用之。
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
    // 保持视频原始比例（force-aspect-ratio 默认 true）：KMS 硬件在
    // render-rectangle 划定的物理矩形内等比缩放并居中，不拉伸变形。
    // 注意：16:9 视频放进 266×960 竖条矩形时会等比缩至 472 高、上下留黑边，
    // 这是预期行为（用户方案：只修正坐标对位，不改变画面比例）。
    // 此属性保持默认 true，无需显式设置——绝不设 false（避免强制拉伸变形）。
    PLAYER_LOG("kmssink created (plane-id=76 driver=rockchip, keep-aspect-ratio)");
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

    // 组装：src → queue → qtdemux（动态 pad 分流：
    //   video/x-h264 → h264parse → mppvideodec(硬件旋转) → vqueue → videoconvert → videoSink_
    //   audio/*      → decodebin(音频解码) → audioconvert → audioresample → audioSink_）
    // 【协商缓冲红线 2026-08-09 用户诊断】前版把 videoconvert/audioresample 一并
    // 精简掉，动态管线直接 mppvideodec(DMABuf NV12) → kmssink、decodebin raw →
    // alsasink：caps 协商过于刚性，任何格式不匹配都会让 preroll 卡死（真机
    // 15:37 卡第一帧实证：线程全线 futex、mpp_dec_hal 零 CPU 不消费）。
    // 恢复 videoconvert + audioconvert + audioresample 作为协商缓冲：
    // 格式匹配时 GStreamer 内部直通（零拷贝），不匹配时兜底转换，100% 协商成功。
    // 【静态后端+动态前端（用户方案）】后端链全部在 build 时静态链接，
    // pad-added 回调只做动态 pad → 静态后端入口（vqueue/aconvert）的 pad 级
    // 连接，绝不在 PLAYING/PAUSED 切换途中做元素级 gst_element_link——
    // 那会与状态迁移抢锁（3fc0948 lazy-link 实测死锁，回退该方案）。
    gst_bin_add_many(GST_BIN(pipeline_), src, queue, demux_, vparse_, vdec_,
        vqueue_, vconvert_, vscale_, vcaps_, vbox_, voverlay_, vconvert2_,
        decodebin_, aconvert_, aresample_, videoSink_, audioSink_, nullptr);
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
                               voverlay_, vconvert2_, videoSink_, nullptr)) {
        PLAYER_LOG("link vparse->vdec->vqueue->vconv->vscale->vcaps->vbox->overlay->vconv2->sink failed");
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
        if (!bar_->init(canvasW_, canvasH_, canvasH_ > canvasW_)) {
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
    // 音频后端链静态预链接：decodebin 音频 pad 出现时连 aconvert sink。
    if (!gst_element_link_many(aconvert_, aresample_, audioSink_, nullptr)) {
        PLAYER_LOG("link aconv->ares->asink failed");
        teardown();
        return false;
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
    // 音频解码链：decodebin（AAC/MP3 → raw）→ audioconvert → audioresample → alsasink
    g_signal_connect(decodebin_, "pad-added", G_CALLBACK(GstPlayer::decodebinPadAddedCb), this);
    PLAYER_LOG("pipeline linked (qtdemux>vparse>mppvideodec>vqueue>videoconvert:kmssink | audio>decodebin>audioconvert>audioresample>alsasink)");

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
        sink = decodebin_;
        PLAYER_LOG("audio %s -> decodebin", media);
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
    GstElement* sink = nullptr;
    if (media && g_str_has_prefix(media, "video/")) {
        // 非 h264 fallback 视频：接入静态视频后端入口 vqueue_（vqueue→videoconvert→
        // kmssink 已在 buildPipeline 静态链接）。旋转由 mppvideodec 硬链负责；
        // fallback（av1/hevc 软解）极少触发（API 已锁 codecid=7），保持直通即可。
        gint w = 0, h = 0;
        gst_structure_get_int(s, "width", &w);
        gst_structure_get_int(s, "height", &h);
        // 【seek 修复 2026-08-14】不再在 pad-added 动态改 capsfilter caps：
        // 真机实证动态重协商会让后续 FLUSH seek 失效（位置跳转后立即回退）。
        // B 站 durl 均为 16:9，构建期 applyCanvasContent(1280,720) 已按 266×473
        // 预置；如源尺寸非常规比例仅记录警告（黑边略有偏差，可接受）。
        if (w > 0 && h > 0 && (w != 1280 || h != 720)) {
            PLAYER_LOG("video src non-16:9 %dx%d (canvas keeps 16:9 default)", w, h);
        }
        sink = vqueue_;
        PLAYER_LOG("video %dx%d fallback -> vqueue (static backend)", w, h);
    } else if (media && g_str_has_prefix(media, "audio/")) {
        // 音频解码输出 → 静态音频后端入口 aconvert_（aconvert→audioresample→
        // alsasink 已在 buildPipeline 静态链接；audioresample 保证 44.1k/48k
        // 采样率不匹配时也能协商成功，杜绝 alsasink preroll 卡死）。
        sink = aconvert_;
        PLAYER_LOG("audio raw -> aconvert (static backend)");
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
    demux_ = nullptr;
    vparse_ = nullptr;
    vdec_ = nullptr;
    vqueue_ = nullptr;
    vconvert_ = nullptr;
    vscale_ = nullptr;
    vcaps_ = nullptr;
    vbox_ = nullptr;
    voverlay_ = nullptr;
    vconvert2_ = nullptr;
    decodebin_ = nullptr;
    aconvert_ = nullptr;
    aresample_ = nullptr;
    videoSink_ = nullptr;
    audioSink_ = nullptr;
    videoFlip_ = nullptr;
    if (barPixbuf_) {
        g_object_unref(barPixbuf_);
        barPixbuf_ = nullptr;
    }
    if (bar_) {
        delete bar_;
        bar_ = nullptr;
    }
    canvasW_ = 0;
    canvasH_ = 0;
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
    // 预热悬浮控制栏依赖库（cairo/gdk-pixbuf/fontconfig，RTLD_GLOBAL），
    // 避免首次使用时的懒解析崩溃（2026-08-14 闪退修复，见 ControlBar.cpp）
    ensureOverlayLibsGlobal();

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
