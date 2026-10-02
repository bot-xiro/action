// 图片查看器 (独立模块 libjsapi_imageviewer.so) —— 只做图片查看, 不与 bilinet/gstplayer 混在一起.
//
// 设计: 解码/缩放/裁剪都在这里做, 结果编码成 JPEG 落到 app 自己的目录, JS 侧只拿到 file:// 路径
// 交给 <image> 渲染. 这样 JS 主线程不做图像运算, 也不会碰系统文件.
//
// 依赖: 设备自带 libturbojpeg.so.0 / 系统 curl —— 都用 dlopen/popen 运行时获取,
// 交叉编译环境无需 aarch64 的 -dev 包.
//
// JS 用法:
//   import { imageviewer } from 'imageviewer'
//   const info = imageviewer.open(url)            // {ret:0, width, height}  (url 或 file:///绝对路径)
//   const path = imageviewer.view(cx, cy, zoom, outW, outH)   // -> 'file:///userdisk/xiro/iview.jpg'
//   imageviewer.info()                            // {ret:0, width, height, hasImage}
//   imageviewer.close()                           // 释放内存

#include "jqutil_v2/jqutil.h"
#include "jsmodules/JSCModuleExtension.h"
#include "jquick_config.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <ctime>
#include <vector>
#include <mutex>
#include <dlfcn.h>
#include <syslog.h>

namespace imageviewer {

#define IV_LOG(fmt, ...) do { syslog(LOG_DEBUG, "[imageviewer] " fmt, ##__VA_ARGS__); } while (0)

// ---------------- turbojpeg 运行时加载 ----------------
// 只需要这几个符号; 设备上是 libturbojpeg.so.0 (SIMD 加速, 自带 1/1,1/2,1/4.. 缩放)
extern "C" {
typedef void* tjhandle;
typedef struct { int num; int denom; } tjscalingfactor_t;
}

// 注意: 下面这些签名与 turbojpeg.h 一致, 但我们不 include 它 (交叉编译环境没有头文件).
static tjhandle (*p_tjInitDecompress)(void) = NULL;
static int (*p_tjDecompressHeader3)(tjhandle, const unsigned char*, unsigned long, int*, int*, int*, int*) = NULL;
static int (*p_tjDecompress2)(tjhandle, const unsigned char*, unsigned long, unsigned char*, int, int, int, int, int) = NULL;
static tjhandle (*p_tjInitCompress)(void) = NULL;
static int (*p_tjCompress2)(tjhandle, const unsigned char*, int, int, int, int, unsigned char**, unsigned long*, int, int, int) = NULL;
static int (*p_tjDestroy)(tjhandle) = NULL;
static void (*p_tjFree)(void*) = NULL;
static const char* (*p_tjGetErrorStr2)(tjhandle) = NULL;

static const int TJPF_RGB = 0;
static const int TJSAMP_420 = 2;
static const int TJCS_RGB = 2;

// dlopen 一次, 常驻
static bool ivLoadTj()
{
    static bool tried = false;
    static bool ok = false;
    if (tried) return ok;
    tried = true;
    const char* cands[] = { "libturbojpeg.so.0", "libturbojpeg.so", "/usr/lib/libturbojpeg.so.0" };
    void* h = NULL;
    for (size_t i = 0; i < sizeof(cands) / sizeof(cands[0]) && !h; i++) h = dlopen(cands[i], RTLD_NOW);
    if (!h) { IV_LOG("dlopen libturbojpeg 失败: %s", dlerror()); return false; }
    p_tjInitDecompress = (tjhandle(*)())dlsym(h, "tjInitDecompress");
    p_tjDecompressHeader3 = (int(*)(tjhandle, const unsigned char*, unsigned long, int*, int*, int*, int*))dlsym(h, "tjDecompressHeader3");
    p_tjDecompress2 = (int(*)(tjhandle, const unsigned char*, unsigned long, unsigned char*, int, int, int, int, int))dlsym(h, "tjDecompress2");
    p_tjInitCompress = (tjhandle(*)())dlsym(h, "tjInitCompress");
    p_tjCompress2 = (int(*)(tjhandle, const unsigned char*, int, int, int, int, unsigned char**, unsigned long*, int, int, int))dlsym(h, "tjCompress2");
    p_tjDestroy = (int(*)(tjhandle))dlsym(h, "tjDestroy");
    p_tjFree = (void(*)(void*))dlsym(h, "tjFree");
    p_tjGetErrorStr2 = (const char*(*)(tjhandle))dlsym(h, "tjGetErrorStr2");
    ok = p_tjInitDecompress && p_tjDecompressHeader3 && p_tjDecompress2 && p_tjInitCompress && p_tjCompress2 && p_tjDestroy && p_tjFree;
    IV_LOG("turbojpeg 加载: %s", ok ? "OK" : "符号缺失");
    return ok;
}

// ---------------- 抓取 / 读文件 ----------------
static std::string ivShellQuote(const std::string& s)
{
    std::string out = "'";
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\'') out += "'\\''";
        else out += s[i];
    }
    out += "'";
    return out;
}

static bool ivFetch(const std::string& url, std::string& out)
{
    out.clear();
    std::string cmd;
    if (url.compare(0, 7, "file://") == 0) {
        std::string path = url.substr(7);
        FILE* fp = fopen(path.c_str(), "rb");
        if (!fp) return false;
        char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) out.append(buf, n);
        fclose(fp);
        return !out.empty();
    }
    // http(s): 用系统 curl, 带浏览器 UA/Referer (B 站图床需要)
    cmd = "curl -s --compressed --max-time 20 -A "
        + ivShellQuote("Mozilla/5.0 (Windows NT 10.0; Win64; x64)")
        + " -e " + ivShellQuote("https://www.bilibili.com/")
        + " " + ivShellQuote(url);
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return false;
    char buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) out.append(buf, n);
    pclose(fp);
    return !out.empty();
}

// ---------------- 状态 ----------------
static std::string g_bytes;      // 原图字节 (JPEG/PNG)
static int g_w = 0, g_h = 0;     // 原图尺寸
static std::string g_outPath = "/userdisk/xiro/iview.jpg";
static std::mutex g_mtx;

// 选一个不超过 zoom 的 turbojpeg 缩放档 (1/1,1/2,1/4,1/8)
static void ivPickScale(double zoom, int& num, int& den)
{
    num = 1; den = 1;
    if (zoom <= 0.13) { num = 1; den = 8; }
    else if (zoom <= 0.26) { num = 1; den = 4; }
    else if (zoom <= 0.51) { num = 1; den = 2; }
}

class ImageViewer : public JQUTIL_NS::JQBaseObject {
public:
    // open(url) -> {ret, width, height}
    void open(JQUTIL_NS::JQFunctionInfo& info)
    {
        JSContext* ctx = info.GetContext();
        if (info.Length() < 1 || !JS_IsString(info[0])) {
            info.GetReturnValue().ThrowTypeError("imageviewer.open: url required");
            return;
        }
        const char* urlC = JS_ToCString(ctx, info[0]);
        std::string url = urlC ? urlC : "";
        if (urlC) JS_FreeCString(ctx, urlC);
        if (url.empty()) { info.GetReturnValue().ThrowTypeError("imageviewer.open: invalid url"); return; }
        if (!ivLoadTj()) { info.GetReturnValue().ThrowTypeError("imageviewer: 设备缺少 libturbojpeg"); return; }

        std::lock_guard<std::mutex> lock(g_mtx);
        std::string body;
        if (!ivFetch(url, body)) { info.GetReturnValue().ThrowTypeError("imageviewer.open: 拉取图片失败"); return; }
        tjhandle dh = p_tjInitDecompress();
        if (!dh) { info.GetReturnValue().ThrowTypeError("imageviewer: 解码器初始化失败"); return; }
        int w = 0, h = 0, sub = 0, cs = 0;
        int rc = p_tjDecompressHeader3(dh, (const unsigned char*)body.data(), (unsigned long)body.size(), &w, &h, &sub, &cs);
        p_tjDestroy(dh);
        if (rc != 0 || w <= 0 || h <= 0) { info.GetReturnValue().ThrowTypeError("imageviewer: 不是可解码的 JPEG 图片"); return; }
        g_bytes.swap(body);
        g_w = w; g_h = h;
        IV_LOG("open ok: %dx%d (%zu bytes)", w, h, g_bytes.size());

        std::string js = "{\"ret\":0,\"width\":" + std::to_string(w) + ",\"height\":" + std::to_string(h) + "}";
        info.GetReturnValue().Set(js);
    }

    // view(cx, cy, zoom, outW, outH) -> file:// path (视口内的画面)
    void view(JQUTIL_NS::JQFunctionInfo& info)
    {
        JSContext* ctx = info.GetContext();
        double cx = 0, cy = 0, zoom = 1;
        int outW = 960, outH = 266;
        if (info.Length() >= 1 && JS_IsNumber(info[0])) JS_ToFloat64(ctx, &cx, info[0]);
        if (info.Length() >= 2 && JS_IsNumber(info[1])) JS_ToFloat64(ctx, &cy, info[1]);
        if (info.Length() >= 3 && JS_IsNumber(info[2])) JS_ToFloat64(ctx, &zoom, info[2]);
        if (info.Length() >= 4 && JS_IsNumber(info[3])) { double v = 0; JS_ToFloat64(ctx, &v, info[3]); outW = (int)v; }
        if (info.Length() >= 5 && JS_IsNumber(info[4])) { double v = 0; JS_ToFloat64(ctx, &v, info[4]); outH = (int)v; }
        if (outW <= 0 || outW > 2048) outW = 960;
        if (outH <= 0 || outH > 2048) outH = 266;
        if (zoom < 0.05) zoom = 0.05;
        if (zoom > 8) zoom = 8;

        std::lock_guard<std::mutex> lock(g_mtx);
        if (g_bytes.empty() || g_w <= 0) { info.GetReturnValue().ThrowTypeError("imageviewer.view: 先 open"); return; }
        if (!ivLoadTj()) { info.GetReturnValue().ThrowTypeError("imageviewer: 缺少 libturbojpeg"); return; }

        int num = 1, den = 1;
        ivPickScale(zoom, num, den);
        int dw = (g_w * num + den - 1) / den;   // 缩放解码后的尺寸
        int dh = (g_h * num + den - 1) / den;
        if (dw <= 0 || dh <= 0) { info.GetReturnValue().ThrowTypeError("imageviewer: 尺寸异常"); return; }

        std::vector<unsigned char> dec((size_t)dw * dh * 3);
        tjhandle dh2 = p_tjInitDecompress();
        if (!dh2) { info.GetReturnValue().ThrowTypeError("imageviewer: 解码器初始化失败"); return; }
        int rc = p_tjDecompress2(dh2, (const unsigned char*)g_bytes.data(), (unsigned long)g_bytes.size(),
                                 dec.data(), dw, dw * 3, dh, TJPF_RGB, 0);
        p_tjDestroy(dh2);
        if (rc != 0) { info.GetReturnValue().ThrowTypeError("imageviewer: 解码失败"); return; }

        // 视口: 以 (cx,cy) 为中心, 宽 = outW/zoom, 高 = outH/zoom (源图坐标), 再最近邻缩放到 outW x outH
        double srcW = (double)outW / zoom;
        double srcH = (double)outH / zoom;
        double x0 = cx - srcW / 2.0;
        double y0 = cy - srcH / 2.0;
        std::vector<unsigned char> out((size_t)outW * outH * 3);
        for (int y = 0; y < outH; y++) {
            int sy = (int)(y0 + (double)y * srcH / (double)outH);
            if (sy < 0) sy = 0; if (sy >= dh) sy = dh - 1;
            const unsigned char* srow = dec.data() + (size_t)sy * dw * 3;
            unsigned char* drow = out.data() + (size_t)y * outW * 3;
            for (int x = 0; x < outW; x++) {
                int sx = (int)(x0 + (double)x * srcW / (double)outW);
                if (sx < 0) sx = 0; if (sx >= dw) sx = dw - 1;
                const unsigned char* sp = srow + (size_t)sx * 3;
                drow[x * 3 + 0] = sp[0];
                drow[x * 3 + 1] = sp[1];
                drow[x * 3 + 2] = sp[2];
            }
        }

        unsigned char* jpg = NULL;
        unsigned long jpgSize = 0;
        tjhandle ch = p_tjInitCompress();
        if (!ch) { info.GetReturnValue().ThrowTypeError("imageviewer: 编码器初始化失败"); return; }
        rc = p_tjCompress2(ch, out.data(), outW, outW * 3, outH, TJPF_RGB, &jpg, &jpgSize, TJSAMP_420, 88, 0);
        p_tjDestroy(ch);
        if (rc != 0 || !jpg) { info.GetReturnValue().ThrowTypeError("imageviewer: 编码失败"); return; }
        FILE* fp = fopen(g_outPath.c_str(), "wb");
        if (!fp) { p_tjFree(jpg); info.GetReturnValue().ThrowTypeError("imageviewer: 写文件失败"); return; }
        fwrite(jpg, 1, jpgSize, fp);
        fclose(fp);
        p_tjFree(jpg);

        std::string res = "file://" + g_outPath + "?t=" + std::to_string((long)time(NULL));
        info.GetReturnValue().Set(res);
    }

    // info() -> {ret, width, height, hasImage}
    void info(JQUTIL_NS::JQFunctionInfo& info)
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        std::string js = "{\"ret\":0,\"width\":" + std::to_string(g_w)
            + ",\"height\":" + std::to_string(g_h)
            + ",\"hasImage\":" + (g_bytes.empty() ? "false" : "true") + "}";
        info.GetReturnValue().Set(js);
    }

    // close()
    void close(JQUTIL_NS::JQFunctionInfo& info)
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        std::string().swap(g_bytes);
        g_w = 0; g_h = 0;
        info.GetReturnValue().Set(std::string("{\"ret\":0}"));
    }
};

static JSValue createImageViewer(JQModuleEnv* env)
{
    JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "imageviewer");
    tpl->InstanceTemplate()->setObjectCreator([]() {
        static ImageViewer* instance = []() {
            ImageViewer* p = new ImageViewer();
            p->REF();
            return p;
        }();
        return instance;
    });
    tpl->SetProtoMethod("open", &ImageViewer::open);
    tpl->SetProtoMethod("view", &ImageViewer::view);
    tpl->SetProtoMethod("info", &ImageViewer::info);
    tpl->SetProtoMethod("close", &ImageViewer::close);
    return tpl->CallConstructor();
}

void imageviewer_init(JQModuleEnv* env)
{
    env->setModuleExport("imageviewer", createImageViewer(env));
}

}  // namespace imageviewer
