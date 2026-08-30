#include "VProbeCore.h"
#include <cstdio>
#include <cstring>

// --- 词典笔面板常量 (实测 profile youdao-rk3562-x7) ---
static const int LOGIC_W = 960;
static const int LOGIC_H = 266;
static const int PANEL_W = 480;
static const int PANEL_H = 960;
static const int KMS_PLANE_ID = 76;     // Esmart1-win0, 实测可用
static const int Y_OFFSET = 107;        // 面板内容 y 偏移 (压护身棒条)

static const int BAND_LX = 0;
static const int BAND_LY = 44;
static const int BAND_LW = 960;
static const int BAND_LH = 178;         // 266 - 44*2

#define VPL(...) do { fprintf(stderr, "[vp] " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)

// logical (960x266) -> physical DSI-1 (480x960, dir=270, yoff=107)
// 像素映射: p_x = l_y + 107, p_y = 959 - (l_x + l_w), p_w = l_h, p_h = l_w
void VProbeCore::logicToPhys(int lx, int ly, int lw, int lh, int* px, int* py, int* pw, int* ph) {
  int x = ly + Y_OFFSET;
  int y = PANEL_H - 1 - lx - lw;
  int w = lh;
  int h = lw;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x + w > PANEL_W) w = PANEL_W - x;
  if (y + h > PANEL_H) h = PANEL_H - y;
  if (px) *px = x;
  if (py) *py = y;
  if (pw) *pw = w;
  if (ph) *ph = h;
}

VProbeCore::VProbeCore()
  : m_pipe(NULL), m_flip(0), m_rectMode(0), m_videoLinked(false), m_audioLinked(false) {}

VProbeCore::~VProbeCore() { stop(); }

void VProbeCore::setFlip(int v) { m_flip = v; }
void VProbeCore::setRectMode(int v) { m_rectMode = v; }
void VProbeCore::setHoleEnable(bool) {}

GstElement* VProbeCore::makeSource(const std::string& uri) {
  GstElement* src;
  if (uri.rfind("file://", 0) == 0) {
    src = gst_element_factory_make("filesrc", "src");
    if (!src) return NULL;
    g_object_set(src, "location", uri.c_str() + 7, NULL);
  } else {
    src = gst_element_factory_make("souphttpsrc", "src");
    if (!src) return NULL;
    g_object_set(src, "location", uri.c_str(), NULL);
    GstStructure* hdrs = gst_structure_new("request-headers",
      "User-Agent", G_TYPE_STRING, "Mozilla/5.0 videoprobe/1.0",
      "Referer", G_TYPE_STRING, "https://www.bilibili.com",
      NULL);
    g_object_set(src, "extra-headers", hdrs, NULL);
    gst_structure_free(hdrs);
  }
  return src;
}

bool VProbeCore::open(const std::string& uri) {
  std::lock_guard<std::mutex> lock(m_lock);
  teardown();
  m_uri = uri;
  m_videoLinked = false;
  m_audioLinked = false;

  gst_init(NULL, NULL);
  m_pipe = gst_pipeline_new("vp");
  if (!m_pipe) return false;

  GstElement* src = makeSource(uri);
  GstElement* queue = gst_element_factory_make("queue", "demuxqueue");
  GstElement* demux = gst_element_factory_make("qtdemux", "demux");
  if (!src || !queue || !demux) {
    VPL("src/demux fail s=%d q=%d d=%d", !!src, !!queue, !!demux);
    teardown();
    return false;
  }
  GstElement* parse = gst_element_factory_make("h264parse", "h264parse");
  GstElement* dec = gst_element_factory_make("mppvideodec", "vdec");
  GstElement* flip = gst_element_factory_make("videoflip", "vflip");
  GstElement* conv = gst_element_factory_make("videoconvert", "vconv");
  GstElement* vs = gst_element_factory_make("kmssink", "vsink");
  if (!parse || !dec || !flip || !conv || !vs) {
    VPL("video factory fail p=%d d=%d f=%d c=%d s=%d",
        !!parse, !!dec, !!flip, !!conv, !!vs);
    teardown();
    return false;
  }
  g_object_set(flip, "method", m_flip, NULL);

  // kmssink: 固定 render-rectangle, 不依赖 hole (UI 采 hole 时才允许透明区透出; 无 hole 则为黑底叠加)
  // 本词典笔只有这两种布局: full (0,0,960,266) 或 band (0,44,960,178).
  int lx = 0, ly = 0, lw = LOGIC_W, lh = LOGIC_H;
  if (m_rectMode == 1) { lx = BAND_LX; ly = BAND_LY; lw = BAND_LW; lh = BAND_LH; }
  int px, py, pw, ph;
  logicToPhys(lx, ly, lw, lh, &px, &py, &pw, &ph);
  VPL("rect %d,%d,%d,%d -> phys (%d,%d,%d,%d) flip=%d mode=%d", lx, ly, lw, lh, px, py, pw, ph, m_flip, m_rectMode);

  g_object_set(vs,
               "driver-name", "rockchip",
               "plane-id", KMS_PLANE_ID,
               "can-scale", TRUE,
               "sync", TRUE,
               NULL);
  {
    GValue rv = G_VALUE_INIT;
    g_value_init(&rv, GST_TYPE_ARRAY);
    GstStructure* s = gst_structure_new("r",
      "x", G_TYPE_INT, px, "y", G_TYPE_INT, py,
      "width", G_TYPE_INT, pw, "height", G_TYPE_INT, ph, NULL);
    GValue st = G_VALUE_INIT;
    g_value_init(&st, GST_TYPE_STRUCTURE);
    g_value_set_boxed(&st, s);
    gst_value_array_append_value(&rv, &st);
    g_value_unset(&st);
    gst_structure_free(s);
    g_object_set_property(G_OBJECT(vs), "render-rectangle", &rv);
    g_value_unset(&rv);
  }

  gst_bin_add_many(GST_BIN(m_pipe),
                   src, queue, demux,
                   parse, dec, flip, conv, vs, NULL);

  // 源到 demux 以前静态链
  if (!gst_element_link_many(src, queue, demux, NULL)) {
    VPL("link src/demux fail");
    teardown();
    return false;
  }
  g_signal_connect(demux, "pad-added", G_CALLBACK(&VProbeCore::onDemuxPad), this);

  GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipe));
  gst_bus_add_watch_full(bus, G_PRIORITY_DEFAULT, busCb, this, NULL);
  gst_object_unref(bus);

  VPL("open ok uri=%s", uri.c_str());
  return true;
}

void VProbeCore::onDemuxPad(GstElement* elem, GstPad* pad, void* data) {
  VProbeCore* self = (VProbeCore*)data;
  GstCaps* caps = gst_pad_get_current_caps(pad);
  if (!caps) return;
  const gchar* nm = gst_structure_get_name(gst_caps_get_structure(caps, 0));
  VPL("pad-added: %s", nm);
  if (g_str_has_prefix(nm, "video/")) self->linkVideo(pad);
  else if (g_str_has_prefix(nm, "audio/")) self->linkAudio(pad);
  gst_caps_unref(caps);
}

void VProbeCore::linkVideo(GstPad* demuxPad) {
  if (m_videoLinked) return;
  m_videoLinked = true;
  GstElement* parse = gst_bin_get_by_name(GST_BIN(m_pipe), "h264parse");
  GstElement* dec = gst_bin_get_by_name(GST_BIN(m_pipe), "vdec");
  GstElement* flip = gst_bin_get_by_name(GST_BIN(m_pipe), "vflip");
  GstElement* conv = gst_bin_get_by_name(GST_BIN(m_pipe), "vconv");
  GstElement* vs = gst_bin_get_by_name(GST_BIN(m_pipe), "vsink");
  if (!parse || !dec || !flip || !conv) {
    VPL("video link resolve fail");
    return;
  }
  GstPad* psink = gst_element_get_static_pad(parse, "sink");
  GstPadLinkReturn r = gst_pad_link(demuxPad, psink);
  gst_object_unref(psink);
  if (r != GST_PAD_LINK_OK) { VPL("video pad link fail ret=%d", r); return; }
  // 把已建好但状态未跑的元素迁到 PLAYING
  gst_element_sync_state_with_parent(parse);
  gst_element_sync_state_with_parent(dec);
  gst_element_sync_state_with_parent(flip);
  gst_element_sync_state_with_parent(conv);
  VPL("video attached");
}

void VProbeCore::linkAudio(GstPad* demuxPad) {
  if (m_audioLinked) return;
  m_audioLinked = true;
  GstElement* aqueue = gst_element_factory_make("queue", "aqueue");
  GstElement* decode = gst_element_factory_make("decodebin", "adec");
  GstElement* convert = gst_element_factory_make("audioconvert", "aconv");
  GstElement* resample = gst_element_factory_make("audioresample", "aresm");
  GstElement* volume = gst_element_factory_make("volume", "avol");
  GstElement* sink = gst_element_factory_make("alsasink", "asink");
  if (!aqueue || !decode || !convert || !resample || !volume || !sink) {
    VPL("audio factory fail q=%d d=%d c=%d r=%d v=%d s=%d",
        !!aqueue, !!decode, !!convert, !!resample, !!volume, !!sink);
    return;
  }
  gst_bin_add_many(GST_BIN(m_pipe), aqueue, decode, convert, resample, volume, sink, NULL);
  if (!gst_element_link_many(convert, resample, volume, sink, NULL)) {
    VPL("audio tail link fail");
    gst_bin_remove_many(GST_BIN(m_pipe), aqueue, decode, convert, resample, volume, sink, NULL);
    return;
  }
  GstPad* aqsrc = gst_element_get_static_pad(aqueue, "sink");
  GstPadLinkReturn r = gst_pad_link(demuxPad, aqsrc);
  gst_object_unref(aqsrc);
  if (r != GST_PAD_LINK_OK) {
    VPL("audio demux link fail ret=%d", r);
    gst_bin_remove_many(GST_BIN(m_pipe), aqueue, decode, convert, resample, volume, sink, NULL);
    return;
  }
  gboolean okl = gst_element_link(aqueue, decode); // queue -> decodebin (内部 request sink)
  VPL("queue->decodebin link=%s", okl ? "ok" : "fail");
  g_signal_connect(decode, "pad-added", G_CALLBACK(&VProbeCore::onAudioDecodePad), this);
  m_aTail[0] = convert; m_aTail[1] = resample; m_aTail[2] = volume; m_aTail[3] = sink;
  gst_element_sync_state_with_parent(aqueue);
  gst_element_sync_state_with_parent(decode);
  VPL("audio attached (decodebin 可能稍后再 link pad)");
}

void VProbeCore::onAudioDecodePad(GstElement* elem, GstPad* pad, void* data) {
  VProbeCore* self = (VProbeCore*)data;
  if (!self->m_aTail[0]) return;
  GstPad* conv = gst_element_get_static_pad(self->m_aTail[0], "sink");
  if (!conv) return;
  if (!gst_pad_is_linked(conv)) {
    GstPadLinkReturn r = gst_pad_link(pad, conv);
    VPL("audio decodebin pad link ret=%d", (int)r);
    for (int i = 0; i < 4; i++) {
      if (self->m_aTail[i]) gst_element_sync_state_with_parent(self->m_aTail[i]);
    }
  }
  gst_object_unref(conv);
}

gboolean VProbeCore::busCb(GstBus* bus, GstMessage* msg, void* data) {
  VProbeCore* self = (VProbeCore*)data;
  switch (GST_MESSAGE_TYPE(msg)) {
  case GST_MESSAGE_EOS:
    VPL("eos");
    self->stop();
    break;
  case GST_MESSAGE_ERROR: {
    GError* err = NULL; gchar* dbg = NULL;
    gst_message_parse_error(msg, &err, &dbg);
    VPL("bus error: %s (%s)", err ? err->message : "?", dbg ? dbg : "");
    if (err) g_error_free(err);
    if (dbg) g_free(dbg);
    self->stop();
    break;
  }
  case GST_MESSAGE_WARNING: {
    GError* w = NULL;
    gst_message_parse_warning(msg, &w, NULL);
    VPL("warn: %s", w ? w->message : "?");
    if (w) g_error_free(w);
    break;
  }
  default:
    break;
  }
  return TRUE;
}

void VProbeCore::start() {
  std::lock_guard<std::mutex> lock(m_lock);
  if (m_pipe) {
    gst_element_set_state(m_pipe, GST_STATE_PLAYING);
    VPL("start");
  }
}

void VProbeCore::stop() {
  std::lock_guard<std::mutex> lock(m_lock);
  teardown();
  VPL("stop");
}

void VProbeCore::teardown() {
  if (!m_pipe) return;
  gst_element_set_state(m_pipe, GST_STATE_NULL);
  gst_object_unref(m_pipe);
  m_pipe = NULL;
  m_videoLinked = false;
  m_audioLinked = false;
}
