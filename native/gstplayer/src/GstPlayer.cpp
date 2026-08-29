// gstplayer: 播放器原生 JSAPI 模块
//
// 架构 (参照 media-kms.md "Native 进程生命周期": start/stop/status/独立日志/不残留):
//   gstreamer 一律在独立子进程 gstplayerd 中运行, 与 miniapp 宿主进程隔离.
//   实测(本设备): gstreamer 管线建到 miniapp 同进程内播放数秒后宿主被看门狗查杀,
//   独立进程播放则稳定 (同 kmssink/plane 参数 gst-launch 全路径实测通过).
//
// JS 侧使用 (模块名 "gstplayer", 导出单例 gstPlayer):
//   gstPlayer.open(url, rect)      // rect: "x,y,w,h" 逻辑坐标, 缺省全屏
//   gstPlayer.start() / pause() / resume() / close()
//   gstPlayer.seek(ms)
//   gstPlayer.getPosition() / getDuration()   // 最近 QUERY 缓存, 毫秒
//   gstPlayer.stateChanged.on(fn)  // "opening"/"ready"/"play"/"pause"/"eos"/"error: ..."/"closed"
//
// 守护进程行协议: 见 src/daemon.cpp; 模块侧周期性 QUERY, 结果行即 "P <posMs> <durMs>"
#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "jqutil_v2/jqutil.h"

using namespace JQUTIL_NS;

namespace gstplayer {

#define GP_LOG(fmt, ...) syslog(LOG_ERR, "[gstplayer] " fmt, ##__VA_ARGS__)

class GstPlayer : public JQBaseObject {
public:
    JQSignal<std::string> stateChanged;

    GstPlayer()
    {
        GP_LOG("GstPlayer ctr (process-backed)");
        // 守护进程退出后向已关闭管道写 QUERY 会触发 SIGPIPE 终止宿主进程
        // (退出到一半整机看门狗重启的常见根因), 宿主层显式忽略.
        signal(SIGPIPE, SIG_IGN);
    }
    ~GstPlayer()
    {
        GP_LOG("GstPlayer dtr");
        stopDaemon();
    }

    void open(JQFunctionInfo& info)
    {
        if (info.Length() < 1 || !JS_IsString(info[0])) {
            info.GetReturnValue().ThrowTypeError("open: uri required");
            return;
        }
        JSContext* ctx = info.GetContext();
        const char* c = JS_ToCString(ctx, info[0]);
        if (!c) {
            info.GetReturnValue().ThrowTypeError("open: invalid uri");
            return;
        }
        std::string uri(c);
        JS_FreeCString(ctx, c);
        int r[4];
        std::string rect = "0,0,960,266";
        if (info.Length() >= 2 && JS_IsString(info[1])) {
            const char* rr = JS_ToCString(ctx, info[1]);
            if (rr) { rect = rr; JS_FreeCString(ctx, rr); }
        }
        // 校验 uri/rect (skill media-kms.md: 长度/ scheme / 控制字符校验).
        // uri 经 execl(argv) 直传 gstplayerd, 不走 shell 也不走命令管道,
        // 因此 & ? # 等 URL 保留字符必须放行; 之前黑名单含 & 会把
        // bilibili playurl(必带 query 参数)全部误拒.
        bool badUri = uri.empty() || uri.size() > 2048;
        for (size_t i = 0; !badUri && i < uri.size(); i++) {
            unsigned char uc = static_cast<unsigned char>(uri[i]);
            if (uc < 0x20 || uc == 0x7f) badUri = true;
        }
        if (!badUri && uri.rfind("http://", 0) != 0 && uri.rfind("https://", 0) != 0) badUri = true;
        if (badUri ||
            sscanf(rect.c_str(), "%d,%d,%d,%d", &r[0], &r[1], &r[2], &r[3]) != 4) {
            info.GetReturnValue().ThrowInternalError("open: invalid uri/rect");
            emitState("error: invalid uri/rect");
            return;
        }
        GP_LOG("open uri=%s rect=%s", uri.c_str(), rect.c_str());

        std::string daemon = findDaemonPath();
        if (daemon.empty()) {
            GP_LOG("daemon not found");
            emitState("error: daemon not found");
            return;
        }
        if (access(daemon.c_str(), X_OK) != 0 && chmod(daemon.c_str(), 0755) != 0) {
            GP_LOG("daemon not executable: %s errno=%d", daemon.c_str(), errno);
            emitState("error: daemon not executable");
            return;
        }

        stopDaemon();   // 幂等停旧守护进程 (无锁 join 版本)

        int inPipe[2];   // 我们写 -> 子进程 stdin
        int outPipe[2];  // 子进程 stdout -> 我们读
        if (pipe(inPipe) != 0 || pipe(outPipe) != 0) {
            GP_LOG("pipe failed errno=%d", errno);
            emitState("error: pipe failed");
            return;
        }

        pid_t pid = fork();
        if (pid == 0) {
            // 子进程
            dup2(inPipe[0], 0);
            dup2(outPipe[1], 1);
            ::close(inPipe[0]); ::close(inPipe[1]);
            ::close(outPipe[0]); ::close(outPipe[1]);
            execl(daemon.c_str(), "gstplayerd", uri.c_str(), rect.c_str(), (char*)NULL);
            _exit(127);
        }
        if (pid < 0) {
            GP_LOG("fork failed errno=%d", errno);
            ::close(inPipe[0]); ::close(inPipe[1]);
            ::close(outPipe[0]); ::close(outPipe[1]);
            emitState("error: fork failed");
            return;
        }
        ::close(inPipe[0]);
        ::close(outPipe[1]);
        m_pid = pid;
        m_cmdFd = inPipe[1];
        m_outFd = outPipe[0];
        m_posMs = 0;
        m_durMs = 0;
        GP_LOG("daemon spawned pid=%d", (int)pid);
        emitState("opening");
        m_running = true;
        m_reader = std::thread(&GstPlayer::readerLoop, this);
        m_poller = std::thread(&GstPlayer::pollLoop, this);
    }

    void start(JQFunctionInfo&) { sendCmd("START\n"); }
    void pause(JQFunctionInfo&) { sendCmd("PAUSE\n"); }
    void resume(JQFunctionInfo&) { sendCmd("START\n"); }

    void close(JQFunctionInfo&)
    {
        GP_LOG("close");
        stopDaemon();   // 不在 m_lock 下 join 读写线程, 见 stopDaemon 注释
        emitState("closed");
    }

    void setRect(JQFunctionInfo& info)
    {
        if (info.Length() < 1 || !JS_IsString(info[0])) {
            info.GetReturnValue().ThrowTypeError("setRect: rect required");
            return;
        }
        const char* r = JS_ToCString(info.GetContext(), info[0]);
        if (!r) return;
        // rect 走命令管道, 禁止换行/控制字符避免注入多条指令
        bool ok = true;
        for (const char* p = r; *p; ++p) {
            if (*p == '\n' || *p == '\r' || *p == '\t') { ok = false; break; }
        }
        char buf[64];
        if (ok) {
            snprintf(buf, sizeof(buf), "SETRECT %s\n", r);
            sendCmd(buf);
        }
        JS_FreeCString(info.GetContext(), r);
    }

    void seek(JQFunctionInfo& info)
    {
        if (info.Length() < 1 || !JS_IsNumber(info[0])) {
            info.GetReturnValue().ThrowTypeError("seek: ms required");
            return;
        }
        double ms = 0;
        JS_ToFloat64(info.GetContext(), &ms, info[0]);
        if (ms < 0) ms = 0;
        char buf[64];
        snprintf(buf, sizeof(buf), "SEEK %.0f\n", ms);
        sendCmd(buf);
    }

    void getPosition(JQFunctionInfo& info)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        info.GetReturnValue().Set(m_posMs);
    }

    void getDuration(JQFunctionInfo& info)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        info.GetReturnValue().Set(m_durMs);
    }

private:
    // 根据本 .so 的映射路径推导 gstplayerd 邻接路径
    // (说明: 安装器会给 .so 改名成 libjsapi_gstplayer_<id>.so, 但 gstplayerd 非 .so 不改名)
    static std::string findDaemonPath()
    {
        Dl_info di;
        memset(&di, 0, sizeof(di));
        if (dladdr((void*)&findDaemonPath, &di) != 0 && di.dli_fname) {
            std::string p = di.dli_fname;
            size_t pos = p.find_last_of('/');
            if (pos == std::string::npos) return "";
            return p.substr(0, pos + 1) + "gstplayerd";
        }
        return "";
    }

    void sendCmd(const char* cmd)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_cmdFd < 0) return;
        GP_LOG("cmd: %.*s", (int)strlen(cmd) - 1, cmd);
        ssize_t w = write(m_cmdFd, cmd, strlen(cmd));
        if (w < 0) GP_LOG("cmd write failed errno=%d", errno);
    }

    // 读子进程 stdout 行: S/P/L 三类
    void readerLoop()
    {
        std::string acc;
        char buf[512];
        while (m_running) {
            int fd = m_outFd;
            if (fd < 0) break;
            struct pollfd pfd = { fd, POLLIN, 0 };
            if (poll(&pfd, 1, 200) <= 0) continue;
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n <= 0) break;  // EOF: 子进程退出
            acc.append(buf, (size_t)n);
            size_t nl;
            while ((nl = acc.find('\n')) != std::string::npos) {
                std::string line = acc.substr(0, nl);
                acc.erase(0, nl + 1);
                handleLine(line);
            }
        }
        // EOF: 子进程死了
        handleLine(std::string("S error: daemon exited"));
    }

    void handleLine(const std::string& line)
    {
        if (line.rfind("S ", 0) == 0) {
            std::string s = line.substr(2);
            { std::lock_guard<std::mutex> lock(m_lock); if (s == "play") m_lastState = s; }
            GP_LOG("state: %s", s.c_str());
            emitState(s);
        } else if (line.rfind("P ", 0) == 0) {
            double pos = 0, dur = 0;
            if (sscanf(line.c_str(), "P %lf %lf", &pos, &dur) == 2) {
                std::lock_guard<std::mutex> lock(m_lock);
                m_posMs = pos;
                m_durMs = dur > 0 ? dur : m_durMs;
            }
        } else if (line.rfind("L ", 0) == 0) {
            GP_LOG("daemon: %s", line.c_str() + 2);
        }
    }

    // 每 700ms 向子进程发 QUERY, 拿回 position/duration
    void pollLoop()
    {
        while (m_running) {
            int fd = m_cmdFd;
            if (fd < 0) break;
            if (write(fd, "QUERY\n", 6) < 0) break;
            for (int i = 0; i < 7 && m_running; i++) usleep(100000);
        }
    }

    void emitState(const std::string& s)
    {
        try {
            stateChanged.emit(s);
        } catch (...) {}
    }

    // 停掉守护进程. 绝不在调用方持有的 m_lock 内 join reader/poller:
    // reader 线程可能正 emit 状态回 JS, 若 JS 线程持锁等待 join 而 emit
    // 又要同线程派发, 两方互等即死锁 -> 退出卡死 -> 看门狗重启整机.
    // 顺序: 跑锁外设置标志 -> 闭 stdin (daemon teardown, ~1.5s 超时强杀)
    //       -> 闭 stdout -> join.
    void stopDaemon()
    {
        m_running = false;
        {
            std::lock_guard<std::mutex> lock(m_lock);
            if (m_cmdFd >= 0) { ::close(m_cmdFd); m_cmdFd = -1; }
        }
        {
            std::lock_guard<std::mutex> lock(m_lock);
            if (m_pid > 0) {
                for (int i = 0; i < 30; i++) {
                    if (waitpid(m_pid, NULL, WNOHANG) > 0) break;
                    usleep(50000);
                }
                if (waitpid(m_pid, NULL, WNOHANG) <= 0) {
                    GP_LOG("daemon alive after EOF, SIGKILL pid=%d", (int)m_pid);
                    kill(m_pid, SIGKILL);
                    waitpid(m_pid, NULL, 0);
                }
                m_pid = -1;
            }
        }
        {
            std::lock_guard<std::mutex> lock(m_lock);
            if (m_outFd >= 0) { ::close(m_outFd); m_outFd = -1; }
        }
        // reader 可能正阻塞在 stateChanged.emit (向 JS 线程投递); 若 JS 线程在
        // close()->join 里等它就互锁. detach 替代 join, 靠 m_running/fd 关闭让任务
        // 自然结束 (GstPlayer 是单例且生命周期贯穿进程, detach 无悬挂对象风险).
        if (m_reader.joinable() && std::this_thread::get_id() != m_reader.get_id()) m_reader.detach();
        if (m_poller.joinable() && std::this_thread::get_id() != m_poller.get_id()) m_poller.detach();
    }

    std::mutex m_lock;
    pid_t m_pid = -1;
    int m_cmdFd = -1;
    int m_outFd = -1;
    std::atomic<bool> m_running{ false };
    std::thread m_reader;
    std::thread m_poller;
    double m_posMs = 0;
    double m_durMs = 0;
    std::string m_lastState;
};

static JSValue createGstPlayer(JQModuleEnv* env)
{
    JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "gstPlayer");
    tpl->InstanceTemplate()->setObjectCreator([]() {
        static GstPlayer* instance = []() {
            GstPlayer* p = new GstPlayer();
            p->REF();
            return p;
        }();
        return instance;
    });
    tpl->SetProtoMethod("open", &GstPlayer::open);
    tpl->SetProtoMethod("start", &GstPlayer::start);
    tpl->SetProtoMethod("pause", &GstPlayer::pause);
    tpl->SetProtoMethod("resume", &GstPlayer::resume);
    tpl->SetProtoMethod("close", &GstPlayer::close);
    tpl->SetProtoMethod("seek", &GstPlayer::seek);
    tpl->SetProtoMethod("setRect", &GstPlayer::setRect);
    tpl->SetProtoMethod("getPosition", &GstPlayer::getPosition);
    tpl->SetProtoMethod("getDuration", &GstPlayer::getDuration);
    tpl->InstanceTemplate()->Set("stateChanged", &GstPlayer::stateChanged);
    return tpl->CallConstructor();
}

void gstplayer_init(JQModuleEnv* env)
{
    env->setModuleExport("gstPlayer", createGstPlayer(env));
}

}  // namespace gstplayer
