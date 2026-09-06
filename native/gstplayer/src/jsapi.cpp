// jsapi.cpp: gstplayer JSAPI 模块 (v2 全重写)
//
// 架构: gstreamer 一律运行在独立子进程 gstplayerd 中 (与 miniapp 宿主隔离,
// 同进程播放实测触发看门狗整机重启). 本模块只做子进程生命周期 + 行协议收发.
//
// JS 侧 (import { gstPlayer } from "gstplayer"):
//   gstPlayer.open(url, rect?)        rect 缺省 "auto": 设备侧等比拟合 UI 带
//   gstPlayer.start() / pause() / resume() / close()
//   gstPlayer.seek(ms)
//   gstPlayer.getPosition() / getDuration()     毫秒 (QUERY 轮询缓存)
//   gstPlayer.getVideoWidth() / getVideoHeight() 视频分辨率 (V 行缓存)
//   gstPlayer.stateChanged.on(fn)     "opening"/"ready"/"play"/"pause"/
//                                     "eos"/"closed"/"error: ..."
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
        GP_LOG("GstPlayer ctr (v2 process-backed)");
        // 守护进程先退时, 向已关闭的命令管道写 QUERY 会触发 SIGPIPE 杀宿主
        signal(SIGPIPE, SIG_IGN);
    }
    ~GstPlayer() { stopDaemon(); }

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
        std::string rect = "auto";
        if (info.Length() >= 2 && JS_IsString(info[1])) {
            const char* r = JS_ToCString(ctx, info[1]);
            if (r) { rect = r; JS_FreeCString(ctx, r); }
        }
        // uri 经 execl(argv) 直传 gstplayerd (不经 shell), URL 保留字符
        // (& ? # 等) 必须放行; 仅拒控制字符与非法 scheme.
        bool badUri = uri.empty() || uri.size() > 2048;
        for (size_t i = 0; !badUri && i < uri.size(); i++) {
            unsigned char uc = (unsigned char)uri[i];
            if (uc < 0x20 || uc == 0x7f) badUri = true;
        }
        if (!badUri && uri.rfind("http://", 0) != 0 && uri.rfind("https://", 0) != 0 &&
            uri.rfind("file://", 0) != 0) {
            badUri = true;
        }
        if (badUri) {
            info.GetReturnValue().ThrowInternalError("open: invalid uri");
            emitState("error: invalid uri");
            return;
        }
        GP_LOG("open uri(96)=%.96s", uri.c_str());

        std::string daemon = findDaemonPath();
        if (daemon.empty()) {
            GP_LOG("daemon path unknown");
            emitState("error: daemon not found");
            return;
        }
        // 安装器会剥离可执行位 (真机实测 0644), 补 chmod 后再验
        if (access(daemon.c_str(), X_OK) != 0 &&
            (chmod(daemon.c_str(), 0755) != 0 || access(daemon.c_str(), X_OK) != 0)) {
            GP_LOG("daemon not executable: %s errno=%d", daemon.c_str(), errno);
            emitState("error: daemon not executable");
            return;
        }

        stopDaemon();  // 幂等停旧实例

        int inPipe[2];   // 宿主写 -> 子进程 stdin
        int outPipe[2];  // 子进程 stdout -> 宿主读
        if (pipe(inPipe) != 0 || pipe(outPipe) != 0) {
            GP_LOG("pipe failed errno=%d", errno);
            emitState("error: pipe failed");
            return;
        }
        pid_t pid = fork();
        if (pid == 0) {
            dup2(inPipe[0], 0);
            dup2(outPipe[1], 1);
            ::close(inPipe[0]); ::close(inPipe[1]);
            ::close(outPipe[0]); ::close(outPipe[1]);
            execl(daemon.c_str(), "gstplayerd", uri.c_str(), rect.c_str(), (char*)NULL);
            _exit(127);
        }
        if (pid < 0) {
            ::close(inPipe[0]); ::close(inPipe[1]);
            ::close(outPipe[0]); ::close(outPipe[1]);
            GP_LOG("fork failed errno=%d", errno);
            emitState("error: fork failed");
            return;
        }
        ::close(inPipe[0]);
        ::close(outPipe[1]);
        {
            std::lock_guard<std::mutex> lock(m_lock);
            m_pid = pid;
            m_cmdFd = inPipe[1];
            m_outFd = outPipe[0];
            m_posMs = 0;
            m_durMs = 0;
            m_videoW = 0;
            m_videoH = 0;
        }
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
        stopDaemon();
        emitState("closed");
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
    void getVideoWidth(JQFunctionInfo& info)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        info.GetReturnValue().Set(m_videoW);
    }
    void getVideoHeight(JQFunctionInfo& info)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        info.GetReturnValue().Set(m_videoH);
    }

private:
    // .so 安装后改名 libjsapi_gstplayer_<id>.so, 但 gstplayerd 不改名, 邻接同目录
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
        ssize_t w = write(m_cmdFd, cmd, strlen(cmd));
        if (w < 0) GP_LOG("cmd write failed errno=%d", errno);
    }

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
                std::string lineStr = acc.substr(0, nl);
                acc.erase(0, nl + 1);
                handleLine(lineStr);
            }
        }
        if (m_running) {
            GP_LOG("reader EOF (daemon exited)");
            emitState("error: daemon exited");
        }
    }

    void handleLine(const std::string& lineStr)
    {
        if (lineStr.rfind("S ", 0) == 0) {
            std::string s = lineStr.substr(2);
            GP_LOG("state: %s", s.c_str());
            emitState(s);
        } else if (lineStr.rfind("P ", 0) == 0) {
            double pos = 0, dur = 0;
            if (sscanf(lineStr.c_str(), "P %lf %lf", &pos, &dur) == 2) {
                std::lock_guard<std::mutex> lock(m_lock);
                m_posMs = pos;
                if (dur > 0) m_durMs = dur;
            }
        } else if (lineStr.rfind("V ", 0) == 0) {
            int w = 0, h = 0;
            if (sscanf(lineStr.c_str(), "V %d %d", &w, &h) == 2 && w > 0 && h > 0) {
                std::lock_guard<std::mutex> lock(m_lock);
                m_videoW = w;
                m_videoH = h;
            }
            GP_LOG("video size: %s", lineStr.c_str() + 2);
        } else if (lineStr.rfind("L ", 0) == 0) {
            GP_LOG("daemon: %s", lineStr.c_str() + 2);
        }
    }

    // 每 700ms QUERY 一次 position/duration (结果走 P 行)
    void pollLoop()
    {
        while (m_running) {
            int fd;
            {
                std::lock_guard<std::mutex> lock(m_lock);
                fd = m_cmdFd;
            }
            if (fd < 0) break;
            if (write(fd, "QUERY\n", 6) < 0) break;
            for (int i = 0; i < 7 && m_running; i++) usleep(100000);
        }
    }

    void emitState(const std::string& s)
    {
        try { stateChanged.emit(s); } catch (...) {}
    }

    // 停守护进程. 红线: 绝不在持锁状态下 join reader/poller —— reader 线程可能
    // 正 emit 状态回 JS 线程, 若 JS 线程持锁 join 即互等死锁 -> 看门狗重启整机.
    // 顺序: 标志位 -> 闭 stdin (daemon 自动退) -> waitpid 兜底 SIGKILL -> 闭 stdout.
    void stopDaemon()
    {
        m_running = false;
        {
            std::lock_guard<std::mutex> lock(m_lock);
            if (m_cmdFd >= 0) { ::close(m_cmdFd); m_cmdFd = -1; }
        }
        pid_t pid;
        {
            std::lock_guard<std::mutex> lock(m_lock);
            pid = m_pid;
            m_pid = -1;
        }
        if (pid > 0) {
            // stdin EOF 后 daemon ~1.5s 内自行退出; 超时强杀, 不留僵尸
            for (int i = 0; i < 30; i++) {
                if (waitpid(pid, NULL, WNOHANG) != 0) { pid = -1; break; }
                usleep(50000);
            }
            if (pid > 0) {
                GP_LOG("daemon alive after EOF, SIGKILL pid=%d", (int)pid);
                kill(pid, SIGKILL);
                waitpid(pid, NULL, 0);
            }
        }
        {
            std::lock_guard<std::mutex> lock(m_lock);
            if (m_outFd >= 0) { ::close(m_outFd); m_outFd = -1; }
        }
        // detach 替代 join: m_running=false + fd 关闭保证线程随即退出;
        // GstPlayer 单例贯穿进程生命周期, 无悬挂对象风险.
        if (m_reader.joinable() && std::this_thread::get_id() != m_reader.get_id()) {
            m_reader.detach();
        }
        if (m_poller.joinable() && std::this_thread::get_id() != m_poller.get_id()) {
            m_poller.detach();
        }
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
    int m_videoW = 0;
    int m_videoH = 0;
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
    tpl->SetProtoMethod("getPosition", &GstPlayer::getPosition);
    tpl->SetProtoMethod("getDuration", &GstPlayer::getDuration);
    tpl->SetProtoMethod("getVideoWidth", &GstPlayer::getVideoWidth);
    tpl->SetProtoMethod("getVideoHeight", &GstPlayer::getVideoHeight);
    tpl->InstanceTemplate()->Set("stateChanged", &GstPlayer::stateChanged);
    return tpl->CallConstructor();
}

void gstplayer_init(JQModuleEnv* env)
{
    env->setModuleExport("gstPlayer", createGstPlayer(env));
}

}  // namespace gstplayer

// ---- 模块注册 (三名一致: libjsapi_gstplayer.so / "gstplayer" / import gstPlayer) ----
#include "jsmodules/JSCModuleExtension.h"
#include "jquick_config.h"

using namespace JQUTIL_NS;

namespace gstplayer {

extern void gstplayer_init(JQModuleEnv* env);  // 定义于上方

static std::vector<std::string> exportList = { "gstPlayer" };

static int module_init(JSContext *ctx, JSModuleDef *m)
{
    JQuick::sp<JQModuleEnv> env = JQModuleEnv::CreateModule(ctx, m, "gstplayer");
    gstplayer_init(env.get());
    env->setModuleExportDone(JS_UNDEFINED, exportList);
    return 0;
}

DEF_MODULE_LOAD_FUNC_EXPORT(gstplayer, module_init, exportList)

}  // namespace gstplayer

extern "C" JQUICK_EXPORT void custom_init_jsapis()
{
    registerCModuleLoader("gstplayer", &gstplayer::gstplayer_module_load);
}
