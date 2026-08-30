// videoprobed 概率同款守护进程: 把 gstreamer 全部关在子进程, miniapp 宿主零风险
//
// 启动:
//   videoprobed <uri> <flip> <rectMode>
//   flip:      0/1/3 (identity/90r/90l, videoflip method)
//   rectMode:  0=full 960x266, 1=band 0,44,960,178(物理 151,0,178,960)
// stdin 命令协议 (与 gstplayer 简洁化):
//   START | STOP | CLOSE
// stdout: 打印 "S ev" / "E msg"
#include <csignal>
#include <cstdio>
#include <cstring>
#include "VProbeCore.h"

#define O(...) do { fprintf(stdout, __VA_ARGS__); fputc('\n', stdout); fflush(stdout); } while(0)

int main(int argc, char** argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: videoprobed <uri> <flip> <rectMode>\n");
    return 2;
  }
  const char* uri = argv[1];
  int flip = atoi(argv[2]);
  int rectMode = atoi(argv[3]);

  VProbeCore core;
  core.setFlip(flip);
  core.setRectMode(rectMode);
  core.setHoleEnable(true);

  if (!core.open(uri)) {
    O("E open fail")
    return 3;
  }

  char buf[128];
  while (fgets(buf, sizeof buf, stdin)) {
    size_t n = strlen(buf);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = 0;
    if (strcmp(buf, "START") == 0) core.start();
    else if (strcmp(buf, "STOP") == 0) core.stop();
    else if (strcmp(buf, "CLOSE") == 0) break;
  }
  core.stop();
  O("S bye")
  return 0;
}
