// gstplayerd: 独立守护进程播放器 (gstreamer 全在子进程中, miniapp 宿主进程零风险)
//
// 协议 (全文本行协议, 一行一条):
//   启动:   gstplayerd <uri> <rect>
//   stdin:  START | PAUSE | SEEK <ms> | SETRECT <x,y,w,h> | QUERY | CLOSE
//   stdout: S <state>              状态事件 (opening/play/pause/ready/eos/closed/error: ...)
//           P <posMs> <durMs>      QUERY 应答
//           L <text>               日志回声
// 退出: stdin EOF 或 CLOSE 后 teardown 并 exit(0)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <syslog.h>
#include <unistd.h>

#include "PlayCore.h"

using namespace gstplayer;

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: gstplayerd <uri> [rect]\n");
        return 2;
    }
    std::string uri = argv[1];
    std::string rect = argc >= 3 ? argv[2] : "0,0,960,266";

    PlayCore core;
    core.setEventCallback([](const std::string& s) {
        printf("S %s\n", s.c_str());
        fflush(stdout);
    });

    if (!core.open(uri, rect)) {
        return 3;
    }

    char line[512];
    bool done = false;
    while (!done && fgets(line, sizeof(line), stdin)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        size_t len = strlen(p);
        while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r')) p[--len] = '\0';
        if (len == 0) continue;

        if (strcmp(p, "START") == 0) {
            core.start();
        } else if (strcmp(p, "PAUSE") == 0) {
            core.pause();
        } else if (strncmp(p, "SEEK ", 5) == 0) {
            core.seekMs(atof(p + 5));
        } else if (strncmp(p, "SETRECT ", 8) == 0) {
            core.setRect(p + 8);
        } else if (strcmp(p, "QUERY") == 0) {
            printf("P %.0f %.0f\n", core.positionMs(), core.durationMs());
            fflush(stdout);
        } else if (strcmp(p, "CLOSE") == 0) {
            done = true;
        } else {
            printf("L unknown-cmd: %s\n", p);
            fflush(stdout);
        }
    }

    core.close();
    return 0;
}
