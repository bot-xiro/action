// 图片查看器 (独立模块 libjsapi_imageviewer.so) —— 只做图片查看, 不与 bilinet/gstplayer 混在一起.
//
// 设计: 解码/缩放/裁剪都在这里做, 结果编码成 JPEG 落到 app 自己的目录, JS 侧只拿到 file:// 路径
// 交给 <image> 渲染. 这样 JS 主线程不做图像运算, 也不会碰系统文件.
//
// 依赖: 设备自带 libturbojpeg.so.0 / 系统 curl —— 都用 dlopen/popen 运行时获取,
// 交叉编译环境无需 aarch64 的 -dev 包.
//
// 0.9.56 修正 (用户反馈「放大逻辑有问题 / 没用原图 / 预览背景和页面同色」):
//   1) 越界不再 clamp 拉边 —— 图外区域填纯黑背景, 预览背景与页面区分开;
//   2) 采样改「降采样盒平均 + 放大双线性」, 放大不再是大色块;
//   3) 解码缩放档按 zoom 动态选, 保证解码分辨率与采样步长同量级;
//   4) 输出 JPEG 质量 88 -> 90.

#include "jqutil_v2/jqutil.h"
#include "jsmodules/JSCModuleExtension.h"
#include "jquick_config.h"

using namespace JQUTIL_NS;

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <ctime>
#include <vector>
#include <mutex>
#include <dlfcn.h>
#include <syslog.h>

namespace imageviewer {

#define IV_LOG(fmt, ...) do { syslog(LOG_DEBUG, "[imageviewer] " fmt, ##__VA_ARGS__); } while (0)

static const unsigned char IV_BG_R = 0;
static const unsigned char IV_BG_G = 0;
static const unsigned char IV_BG_B = 0;

extern "C" {
typedef void* tjhandle;
}

static tjhandle (*p_tjInitDecompress)(void) = NULL;
static int (*p_tjDecompressHeader3)(tjhandle, const unsigned char*, unsigned long, int*, int*, int*, int*) = NULL;
static int (*p_tjDecompress2)(tjhandle, const unsigned char*, unsigned long, unsigned char*, int, int, int, int, int) = NULL;
static tjhandle (*p_tjInitCompress)(void) = NULL;
static int (*p_tjCompress2)(tjhandle, const unsigned char*, int, int, int, int, unsigned char**, unsigned long*, int, int, int) = NULL;
static int (*p_tjDestroy)(tjhandle) = NULL;
static void (*p_tjFree)(void*) = NULL;

static const int TJPF_RGB = 0;
static const int TJSAMP_420 = 2;

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
    ok = p_tjInitDecompress && p_tjDecompressHeader3 && p_tjDecompress2 && p_tjInitCompress && p_tjCompress2 && p_tjDestroy && p_tjFree;
    IV_LOG("turbojpeg 加载: %s", ok ? "OK" : "符号缺失");
    return ok;
}

static std::string ivShellQuote(const std::string& s)
{
    std::string out = "'";
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == 0x27) out += "'\'\''";
        else out += s[i];
    }
    out += "'";
    return out;
}

static bool ivFetch(const std::string& url, std::string& out)
{
    out.clear();
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
    std::string cmd = "curl -s --compressed --connect-timeout 4 --retry 1 --retry-delay 1 --max-time 20 -A "
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

// 降采样: 盒平均
static void ivSampleBox(const unsigned char* dec, int dw, int dh, double dx, double dy, double foot, int* acc)
{
    int x0 = (int)floor(dx - foot * 0.5);
    int x1 = (int)ceil(dx + foot * 0.5);
    if (x1 <= x0) x1 = x0 + 1;
    if (x0 < 0) x0 = 0;
    if (x1 > dw) x1 = dw;
    int y0 = (int)floor(dy - foot * 0.5);
    int y1 = (int)ceil(dy + foot * 0.5);
    if (y1 <= y0) y1 = y0 + 1;
    if (y0 < 0) y0 = 0;
    if (y1 > dh) y1 = dh;
    long r = 0, g = 0, b = 0, n = 0;
    for (int y = y0; y < y1; y++) {
        const unsigned char* row = dec + (size_t)y * dw * 3;
        for (int x = x0; x < x1; x++) {
            r += row[x * 3 + 0]; g += row[x * 3 + 1]; b += row[x * 3 + 2]; n++;
        }
    }
    if (n <= 0) { acc[0] = IV_BG_R; acc[1] = IV_BG_G; acc[2] = IV_BG_B; return; }
    acc[0] = (int)(r / n); acc[1] = (int)(g / n); acc[2] = (int)(b / n);
}

// 放大: 双线性
static void ivSampleBilinear(const unsigned char* dec, int dw, int dh, double dx, double dy, int* acc)
{
    if (dx < 0) dx = 0;
    if (dy < 0) dy = 0;
    if (dx > (double)(dw - 1)) dx = (double)(dw - 1);
    if (dy > (double)(dh - 1)) dy = (double)(dh - 1);
    int x0 = (int)dx, y0 = (int)dy;
    int x1 = (x0 + 1 < dw) ? x0 + 1 : x0;
    int y1 = (y0 + 1 < dh) ? y0 + 1 : y0;
    double fx = dx - (double)x0, fy = dy - (double)y0;
    const unsigned char* p00 = dec + (size_t)y0 * dw * 3 + x0 * 3;
    const unsigned char* p10 = dec + (size_t)y0 * dw * 3 + x1 * 3;
    const unsigned char* p01 = dec + (size_t)y1 * dw * 3 + x0 * 3;
    const unsigned char* p11 = dec + (size_t)y1 * dw * 3 + x1 * 3;
    for (int c = 0; c < 3; c++) {
        double v = p00[c] * (1 - fx) * (1 - fy) + p10[c] * fx * (1 - fy)
                 + p01[c] * (1 - fx) * fy + p11[c] * fx * fy;
        int iv = (int)(v + 0.5);
        if (iv < 0) iv = 0;
        if (iv > 255) iv = 255;
        acc[c] = iv;
    }
}

static std::string g_bytes;
static int g_w = 0, g_h = 0;
static int g_outSeq = 0;
static std::string g_outDir = "/userdisk/xiro";
static std::mutex g_mtx;

class ImageViewer : public JQUTIL_NS::JQBaseObject {
public:
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
        tjhandle hd = p_tjInitDecompress();
        if (!hd) { info.GetReturnValue().ThrowTypeError("imageviewer: 解码器初始化失败"); return; }
        int w = 0, h = 0, sub = 0, cs = 0;
        int rc = p_tjDecompressHeader3(hd, (const unsigned char*)body.data(), (unsigned long)body.size(), &w, &h, &sub, &cs);
        p_tjDestroy(hd);
        if (rc != 0 || w <= 0 || h <= 0) { info.GetReturnValue().ThrowTypeError("imageviewer: 不是可解码的 JPEG 图片"); return; }
        g_bytes.swap(body);
        g_w = w; g_h = h;
        IV_LOG("open ok: %dx%d (%u bytes)", w, h, (unsigned)g_bytes.size());

        char js[128];
        snprintf(js, sizeof(js), "{\"ret\":0,\"width\":%d,\"height\":%d}", w, h);
        info.GetReturnValue().Set(std::string(js));
    }

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
        if (!(zoom > 0.0)) zoom = 1.0;
        if (zoom < 0.02) zoom = 0.02;
        if (zoom > 16.0) zoom = 16.0;

        std::lock_guard<std::mutex> lock(g_mtx);
        if (g_bytes.empty() || g_w <= 0 || g_h <= 0) { info.GetReturnValue().ThrowTypeError("imageviewer.view: 先 open"); return; }
        if (!ivLoadTj()) { info.GetReturnValue().ThrowTypeError("imageviewer: 缺少 libturbojpeg"); return; }

        int den = 1;
        if (zoom < 0.1875) den = 8;
        else if (zoom < 0.375) den = 4;
        else if (zoom < 0.75) den = 2;
        int dw = (g_w + den - 1) / den;
        int dh = (g_h + den - 1) / den;
        while ((double)dw * (double)dh * 3.0 > 32.0 * 1024.0 * 1024.0 && den < 8) {
            den *= 2;
            dw = (g_w + den - 1) / den;
            dh = (g_h + den - 1) / den;
        }
        if (dw <= 0 || dh <= 0) { info.GetReturnValue().ThrowTypeError("imageviewer: 尺寸异常"); return; }

        std::vector<unsigned char> dec((size_t)dw * (size_t)dh * 3);
        tjhandle hd2 = p_tjInitDecompress();
        if (!hd2) { info.GetReturnValue().ThrowTypeError("imageviewer: 解码器初始化失败"); return; }
        int rc = p_tjDecompress2(hd2, (const unsigned char*)g_bytes.data(), (unsigned long)g_bytes.size(),
                                 dec.data(), dw, dw * 3, dh, TJPF_RGB, 0);
        p_tjDestroy(hd2);
        if (rc != 0) { info.GetReturnValue().ThrowTypeError("imageviewer: 解码失败"); return; }

        const double srcW = (double)outW / zoom;
        const double srcH = (double)outH / zoom;
        const double x0 = cx - srcW * 0.5;
        const double y0 = cy - srcH * 0.5;
        const double stepDec = 1.0 / (double)den;
        const double foot = (1.0 / zoom) * stepDec;
        const bool useBox = foot >= 1.6;
        const double maxOx = (double)g_w - 0.5;
        const double maxOy = (double)g_h - 0.5;

        std::vector<unsigned char> out((size_t)outW * (size_t)outH * 3);
        for (int y = 0; y < outH; y++) {
            const double oy = y0 + ((double)y + 0.5) / zoom;
            unsigned char* drow = out.data() + (size_t)y * outW * 3;
            const bool yOut = (oy < -0.5 || oy > maxOy);
            for (int x = 0; x < outW; x++) {
                unsigned char* dp = drow + x * 3;
                const double ox = x0 + ((double)x + 0.5) / zoom;
                if (yOut || ox < -0.5 || ox > maxOx) {
                    dp[0] = IV_BG_R; dp[1] = IV_BG_G; dp[2] = IV_BG_B;
                    continue;
                }
                int acc[3] = { 0, 0, 0 };
                const double ddx = ox * stepDec - 0.5;
                const double ddy = oy * stepDec - 0.5;
                if (useBox) ivSampleBox(dec.data(), dw, dh, ddx, ddy, foot, acc);
                else ivSampleBilinear(dec.data(), dw, dh, ddx, ddy, acc);
                dp[0] = (unsigned char)acc[0];
                dp[1] = (unsigned char)acc[1];
                dp[2] = (unsigned char)acc[2];
            }
        }

        unsigned char* jpg = NULL;
        unsigned long jpgSize = 0;
        tjhandle ch = p_tjInitCompress();
        if (!ch) { info.GetReturnValue().ThrowTypeError("imageviewer: 编码器初始化失败"); return; }
        rc = p_tjCompress2(ch, out.data(), outW, outW * 3, outH, TJPF_RGB, &jpg, &jpgSize, TJSAMP_420, 90, 0);
        p_tjDestroy(ch);
        if (rc != 0 || !jpg) { info.GetReturnValue().ThrowTypeError("imageviewer: 编码失败"); return; }
        g_outSeq = (g_outSeq % 4) + 1;
        std::string outPath = g_outDir + "/iview_" + std::to_string(g_outSeq) + ".jpg";
        for (int k = 1; k <= 4; k++) {
            if (k == g_outSeq) continue;
            std::string old = g_outDir + "/iview_" + std::to_string(k) + ".jpg";
            remove(old.c_str());
        }
        FILE* fp = fopen(outPath.c_str(), "wb");
        if (!fp) { p_tjFree(jpg); info.GetReturnValue().ThrowTypeError("imageviewer: 写文件失败"); return; }
        fwrite(jpg, 1, jpgSize, fp);
        fclose(fp);
        p_tjFree(jpg);

        IV_LOG("view ok: zoom=%.3f cx=%.1f cy=%.1f -> %s", zoom, cx, cy, outPath.c_str());
        info.GetReturnValue().Set(std::string("file://" + outPath));
    }

    void info(JQUTIL_NS::JQFunctionInfo& info)
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        char js[160];
        snprintf(js, sizeof(js), "{\"ret\":0,\"width\":%d,\"height\":%d,\"hasImage\":%s}",
                 g_w, g_h, g_bytes.empty() ? "false" : "true");
        info.GetReturnValue().Set(std::string(js));
    }

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
