#ifndef VPROBE_CORE_H
#define VPROBE_CORE_H

#include <gstreamer-1.0/gst/gst.h>
#include <mutex>
#include <string>

// videoprobe 核心 (按 skill media-kms.md 从零实现的探测播放器):
//   源 -> qtdemux -+-> h264parse -> mppvideodec -> videoflip -> videoconvert -> kmssink
//                  +-> queue -> aacparse -> faad -> audioconvert -> audioresample -> alsasink
// 说明: qtdemux 的 src 是动态 pad, 所有分支在 pad-added 时挂接.
// 坐标与 plane 硬编码自 profile youdao-rk3562-x7 (logical 960x266, dsi 480x960 dir=270 yoff=107).
class VProbeCore {
 public:
  VProbeCore();
  ~VProbeCore();

  void setFlip(int method);         // videoflip method: 0 identity / 1 90r / 3 90l
  void setRectMode(int band);       // 0=full 960x266, 1=band 960x178
  void setHoleEnable(bool on);      // hole 在 UI 侧, native 端只记录

  bool open(const std::string& uri);
  void start();
  void stop();                      // 幂等

 private:
  void teardown();
  GstElement* makeSource(const std::string& uri);

  static void onDemuxPad(GstElement*, GstPad* pad, void* data);
  void linkVideo(GstPad* pad);
  void linkAudio(GstPad* pad);
  static void onAudioDecodePad(GstElement*, GstPad* pad, void* data);

  static gboolean busCb(GstBus*, GstMessage* msg, void* data);

  // logical(960x266) -> physical(480x960) 按 media-kms.md
  static void logicToPhys(int lx, int ly, int lw, int lh, int* px, int* py, int* pw, int* ph);

  GstElement* m_pipe;
  GstElement* m_aTail[4];           // audioconvert audioresample volume alsasink
  std::string m_uri;
  int  m_flip;
  int  m_rectMode;
  bool m_videoLinked;
  bool m_audioLinked;
  std::mutex m_lock;
};

#endif
