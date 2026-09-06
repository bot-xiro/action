// gstplayerd: 独立播放守护进程 (v2 全重写)
//
// gstreamer 必须独立于 miniapp 宿主进程运行 (同进程播放实测触发看门狗整机重启).
//
// 行协议:
//   启动:   gstplayerd <uri> [rect]      rect: "auto" 或 "x,y,w,h" (Weston 全局坐标)
//   stdin:  START | PAUSE | SEEK <ms> | QUERY | CLOSE
//   stdout: S <state>        状态事件: opening/ready/play/pause/eos/closed/error: ...
//           P <posMs> <durMs> QUERY 应答
//           V <w> <h>        视频分辨率 (caps 探针)
//           L <text>         日志回声
// 退出: stdin EOF 或 CLOSE -> teardown -> exit(0)
//
// 环境: WAYLAND_DISPLAY 由 miniapp 宿主继承; adb 直启时兜底 wayland-0.
#include <signal.h>
#include <syslog.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "core.h"

using namespace gstplayer;

namespace {

std::mutex g_outMutex;  // 多线程 (总线/流线程/主线程) 写行, 防止行内交错

void writeLine(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> lock(g_outMutex);
    fputs(buf, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

// core 事件回调 (总线线程/流线程) -> stdout 行
void onCoreEvent(const std::string& state, void*)
{
    writeLine("S %s", state.c_str());
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: gstplayerd <uri> [rect]\n");
        return 2;
    }
    setenv("WAYLAND_DISPLAY", "wayland-0", 0);   // 0: 已有则不覆盖
    setenv("XDG_RUNTIME_DIR", "/run", 0);
    signal(SIGPIPE, SIG_IGN);                    // 宿主先退时写 stdout 不被杀
    syslog(LOG_ERR, "[gstplayer] daemon start pid=%d", (int)getpid());

    std::string uri = argv[1];
    std::string rect = argc >= 3 ? argv[2] : "auto";

    PlayCore core;
    core.setEventCallback(&onCoreEvent, nullptr);
    if (!core.open(uri, rect)) {
        return 3;
    }

    char line[1024];
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
        } else if (strcmp(p, "QUERY") == 0) {
            writeLine("P %.0f %.0f", core.positionMs(), core.durationMs());
        } else if (strcmp(p, "CLOSE") == 0) {
            done = true;
        } else {
            writeLine("L unknown-cmd: %.64s", p);
        }
    }

    core.close();
    syslog(LOG_ERR, "[gstplayer] daemon exit");
    return 0;
}
