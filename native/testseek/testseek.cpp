// testseek: 设备端 seek 链路实验（交叉编译，-Wl,-unresolved-symbols=ignore-all，
// 运行时从设备 GStreamer 解析符号）。测试 4 种视频链变体的 FLUSH seek 是否生效。
// 用法: TEST_URL=<视频URL> ./testseek
#include <gst/gst.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

static const char* kUA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

// 构建链路：souphttpsrc → queue → qtdemux → [video: h264parse → mppvideodec(270)
// → vqueue → videoconvert → videoscale → capsfilter(可选) → videobox(可选)
// → videoconvert → fakesink] [audio: decodebin → audioconvert → audioresample → alsasink]
static GstElement* build(const char* url, bool contentCaps, bool withVbox, const char* label)
{
    GstElement* pipe = gst_pipeline_new("t");
    GstElement* src = gst_element_factory_make("souphttpsrc", "src");
    GstElement* queue = gst_element_factory_make("queue", "qsrc");
    GstElement* demux = gst_element_factory_make("qtdemux", "demux");
    GstElement* vparse = gst_element_factory_make("h264parse", "vparse");
    GstElement* vdec = gst_element_factory_make("mppvideodec", "vdec");
    GstElement* vqueue = gst_element_factory_make("queue", "vqueue");
    GstElement* vconv = gst_element_factory_make("videoconvert", "vconv");
    GstElement* vscale = gst_element_factory_make("videoscale", "vscale");
    GstElement* vcaps = gst_element_factory_make("capsfilter", "vcaps");
    GstElement* vbox = withVbox ? gst_element_factory_make("videobox", "vbox") : NULL;
    GstElement* vconv2 = gst_element_factory_make("videoconvert", "vconv2");
    GstElement* vsink = gst_element_factory_make("fakesink", "vsink");
    GstElement* adec = gst_element_factory_make("decodebin", "adec");
    GstElement* aconv = gst_element_factory_make("audioconvert", "aconv");
    GstElement* ares = gst_element_factory_make("audioresample", "ares");
    GstElement* asink = gst_element_factory_make("fakesink", "asink");  // fakesink 避免占声卡

    if (!pipe || !src || !queue || !demux || !vparse || !vdec || !vqueue || !vconv ||
        !vscale || !vcaps || !vconv2 || !vsink || !adec || !aconv || !ares || !asink ||
        (withVbox && !vbox)) {
        printf("[%s] factory failed\n", label);
        return NULL;
    }

    g_object_set(src, "location", url, NULL);
    GstStructure* hdrs = gst_structure_new_empty("headers");
    gst_structure_set(hdrs, "referer", G_TYPE_STRING, "https://www.bilibili.com/", NULL);
    g_object_set(src, "extra-headers", hdrs, NULL);
    gst_structure_free(hdrs);
    g_object_set(src, "user-agent", kUA, NULL);
    g_object_set(src, "timeout", 15, NULL);
    g_object_set(vqueue, "max-size-buffers", 8, NULL);
    g_object_set(vqueue, "max-size-bytes", 4 * 1024 * 1024, NULL);
    g_object_set(vqueue, "max-size-time", 0, NULL);
    g_object_set(queue, "max-size-buffers", 200, NULL);
    g_object_set(queue, "max-size-bytes", 16 * 1024 * 1024, NULL);
    g_object_set(queue, "max-size-time", 0, NULL);

    g_object_set(vscale, "add-borders", FALSE, NULL);
    if (contentCaps) {
        GstCaps* caps = gst_caps_new_simple("video/x-raw",
            "width", G_TYPE_INT, 266, "height", G_TYPE_INT, 473, NULL);
        g_object_set(vcaps, "caps", caps, NULL);
        gst_caps_unref(caps);
    } else {
        GstCaps* caps = gst_caps_new_simple("video/x-raw",
            "width", G_TYPE_INT, 266, "height", G_TYPE_INT, 960, NULL);
        g_object_set(vcaps, "caps", caps, NULL);
        gst_caps_unref(caps);
    }
    if (withVbox) {
        g_object_set(vbox, "left", 0, "right", 0, "top", -243, "bottom", -244, NULL);
    }

    gst_bin_add_many(GST_BIN(pipe), src, queue, demux, vparse, vdec, vqueue, vconv,
        vscale, vcaps, vconv2, vsink, adec, aconv, ares, asink, NULL);
    if (withVbox) gst_bin_add(GST_BIN(pipe), vbox);

    // 静态链接（demo 简化：qtdemux pad-added 直接连到静态后端）
    gst_element_link_many(src, queue, demux, NULL);
    gst_element_link_many(vparse, vdec, vqueue, vconv, vscale, vcaps, NULL);
    if (withVbox) {
        gst_element_link_many(vcaps, vbox, vconv2, vsink, NULL);
    } else {
        gst_element_link_many(vcaps, vconv2, vsink, NULL);
    }
    gst_element_link_many(aconv, ares, asink, NULL);

    // pad-added：视频→vparse sink；音频→adec
    GstPad* vp = gst_element_get_static_pad(vparse, "sink");
    GstPad* ap = gst_element_get_static_pad(adec, "sink");
    g_signal_connect(demux, "pad-added", G_CALLBACK(+[](GstElement*, GstPad* pad, gpointer ud) {
        GstCaps* caps = gst_pad_get_current_caps(pad);
        const GstStructure* s = caps ? gst_caps_get_structure(caps, 0) : NULL;
        const gchar* media = s ? gst_structure_get_name(s) : NULL;
        GstPad* target = NULL;
        if (media && g_str_has_prefix(media, "video/")) target = (GstPad*)g_object_get_data(G_OBJECT(ud), "vp");
        else if (media && g_str_has_prefix(media, "audio/")) target = (GstPad*)g_object_get_data(G_OBJECT(ud), "ap");
        if (target) {
            gst_pad_link(pad, target);
            printf("  pad-added: %s linked\n", media ? media : "?");
        }
        if (caps) gst_caps_unref(caps);
    }), pipe);
    // 音频解码输出 → aconv
    GstPad* ac = gst_element_get_static_pad(aconv, "sink");
    g_signal_connect(adec, "pad-added", G_CALLBACK(+[](GstElement*, GstPad* pad, gpointer ud) {
        GstPad* target = (GstPad*)g_object_get_data(G_OBJECT(ud), "ac");
        gst_pad_link(pad, target);
        printf("  audio raw linked\n");
    }), pipe);
    g_object_set_data(G_OBJECT(pipe), "vp", vp);
    g_object_set_data(G_OBJECT(pipe), "ap", ap);
    g_object_set_data(G_OBJECT(pipe), "ac", ac);
    gst_object_unref(vp);
    gst_object_unref(ap);
    gst_object_unref(ac);
    return pipe;
}

static void run_variant(const char* url, bool contentCaps, bool withVbox, const char* label)
{
    printf("==== %s (contentCaps=%d vbox=%d) ====\n", label, contentCaps ? 1 : 0, withVbox ? 1 : 0);
    GstElement* pipe = build(url, contentCaps, withVbox, label);
    if (!pipe) { printf("[%s] build failed\n", label); return; }
    GstStateChangeReturn sr = gst_element_set_state(pipe, GST_STATE_PLAYING);
    printf("[%s] set PLAYING ret=%d\n", label, (int)sr);
    // 等待预滚+播放
    GstState st;
    gst_element_get_state(pipe, &st, NULL, 12 * GST_SECOND);
    printf("[%s] state=%s\n", label, gst_element_state_get_name(st));
    sleep(4);

    gint64 pos = 0;
    gst_element_query_position(pipe, GST_FORMAT_TIME, &pos);
    printf("[%s] pos before seek: %lld ms\n", label, (long long)(pos / 1000000));

    bool ok = gst_element_seek_simple(pipe, GST_FORMAT_TIME,
        (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), 60LL * GST_SECOND);
    printf("[%s] seek(60s) ret=%d\n", label, ok ? 1 : 0);
    sleep(3);
    gst_element_query_position(pipe, GST_FORMAT_TIME, &pos);
    printf("[%s] pos after seek: %lld ms  <<< 期望 ~63000\n", label, (long long)(pos / 1000000));

    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
    printf("---- %s done ----\n\n", label);
}

int main(int argc, char** argv)
{
    gst_init(&argc, &argv);
    const char* url = getenv("TEST_URL");
    if (!url || !*url) {
        FILE* f = fopen("/tmp/playurl2.txt", "r");
        char buf[2048];
        if (f && fgets(buf, sizeof(buf), f)) {
            buf[strcspn(buf, "\r\n")] = 0;
            url = buf;
        }
        if (f) fclose(f);
    }
    if (!url || !*url) {
        printf("no TEST_URL\n");
        return 1;
    }
    printf("URL: %.120s...\n", url);
    // 4 变体
    run_variant(url, true, true, "contentcaps+vbox (app current)");
    run_variant(url, false, false, "fullcaps  no-vbox (art7 style)");
    run_variant(url, true, false, "contentcaps no-vbox");
    run_variant(url, false, true, "fullcaps + vbox");
    return 0;
}
