// videoprobe JSAPI (重复 gstplayer 的 daemon 模式, 只留最小控制面)
// open(uri, flip, rectMode)  / stop()  / 状态由 S 行 TAIL
#include "jqutil_v2/jqutil.h"

#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

using namespace JQUTIL_NS;

namespace videoprobe {

#define VP_SYSLOG(fmt, ...) syslog(LOG_ERR, "[videoprobe] " fmt, ##__VA_ARGS__)

class VideoProbe : public JQBaseObject {
 public:
  JQSignal<std::string> stateChanged;

  VideoProbe() { VP_SYSLOG("ctor"); ::signal(SIGPIPE, SIG_IGN); }
  ~VideoProbe() { stop(); }

  void open(JQFunctionInfo& info) {
    if (info.Length() < 1 || !JS_IsString(info[0])) { info.GetReturnValue().ThrowTypeError("open: uri required"); return; }
    JSContext* cx = info.GetContext();
    const char* uriC = JS_ToCString(cx, info[0]);
    if (!uriC) { info.GetReturnValue().ThrowTypeError("open: uri invalid"); return; }
    std::string uri(uriC);
    JS_FreeCString(cx, uriC);
    if (uri.size() > 2000) { info.GetReturnValue().ThrowInternalError("open: uri too long"); return; }
    for (size_t i = 0; i < uri.size(); i++) {
      unsigned char c = (unsigned char)uri[i];
      if (c < 0x20 || c == 0x7f) { info.GetReturnValue().ThrowInternalError("open: uri bad char"); return; }
    }
    int flip = 0;
    int rectMode = 0;
    if (info.Length() >= 2 && JS_IsNumber(info[1])) { double d = 0; JS_ToFloat64(cx, &d, info[1]); flip = (int)d; }
    if (info.Length() >= 3 && JS_IsNumber(info[2])) { double d = 0; JS_ToFloat64(cx, &d, info[2]); rectMode = (int)d; }
    if (flip < 0 || flip > 3 || rectMode < 0 || rectMode > 1) {
      info.GetReturnValue().ThrowInternalError("open: flip/rectMode invalid");
      return;
    }

    std::string path = daemonPath();
    if (path.empty()) { info.GetReturnValue().ThrowInternalError("open: daemon not found"); return; }
    if (access(path.c_str(), X_OK) != 0 && chmod(path.c_str(), 0755) != 0) {
      VP_SYSLOG("chmod fail %s", path.c_str());
    }

    stop();
    int inPipe[2], outPipe[2];
    if (pipe(inPipe) != 0 || pipe(outPipe) != 0) { VP_SYSLOG("pipe fail errno=%d", errno); return; }
    pid_t pid = fork();
    if (pid == 0) {
      dup2(inPipe[0], 0);
      dup2(outPipe[1], 1);
      ::close(inPipe[0]); ::close(inPipe[1]);
      ::close(outPipe[0]); ::close(outPipe[1]);
      execl(path.c_str(), "videoprobed", uri.c_str(), std::to_string(flip).c_str(), std::to_string(rectMode).c_str(), (char*)NULL);
      _exit(127);
    }
    if (pid < 0) {
      ::close(inPipe[0]); ::close(inPipe[1]);
      ::close(outPipe[0]); ::close(outPipe[1]);
      info.GetReturnValue().ThrowInternalError("open: fork fail");
      return;
    }
    ::close(inPipe[0]);
    ::close(outPipe[1]);
    m_pid = pid;
    m_cmdFd = inPipe[1];
    m_outFd = outPipe[0];
    m_running = true;
    m_reader = std::thread(&VideoProbe::readerLoop, this);
    emitState("opening");
  }

  void start(JQFunctionInfo&) { cmd("START\n"); }
  void stopCommand(JQFunctionInfo&) { cmd("STOP\n"); }
  void close(JQFunctionInfo&) { cmd("CLOSE\n"); stop(); emitState("closed"); }

 private:
  static std::string daemonPath() {
    Dl_info di;
    memset(&di, 0, sizeof(di));
    if (dladdr((void*)&daemonPath, &di) != 0 && di.dli_fname) {
      std::string p = di.dli_fname;
      size_t n = p.find_last_of('/');
      return n == std::string::npos ? "" : p.substr(0, n + 1) + "videoprobed";
    }
    return "";
  }

  void cmd(const char* s) {
    std::lock_guard<std::mutex> lk(m_lock);
    if (m_cmdFd < 0) return;
    ssize_t w = write(m_cmdFd, s, strlen(s));
    if (w < 0) VP_SYSLOG("cmd write fail errno=%d", errno);
  }

  void readerLoop() {
    std::string acc;
    char buf[256];
    while (m_running) {
      int fd;
      { std::lock_guard<std::mutex> lk(m_lock); fd = m_outFd; }
      if (fd < 0) break;
      pollfd pfd = { fd, POLLIN, 0 };
      if (poll(&pfd, 1, 150) <= 0) continue;
      ssize_t n = read(fd, buf, sizeof(buf));
      if (n <= 0) break;
      acc.append(buf, (size_t)n);
      size_t nl;
      while ((nl = acc.find('\n')) != std::string::npos) {
        std::string line = acc.substr(0, nl);
        acc.erase(0, nl + 1);
        handleLine(line);
      }
    }
    handleLine("S daemon eof");
  }

  void handleLine(const std::string& line) {
    if (line.rfind("S ", 0) == 0) emitState(line.substr(2));
    else if (line.rfind("E ", 0) == 0) emitState(std::string("error: ") + line.substr(2));
  }

  void emitState(const std::string& s) {
    try { stateChanged.emit(s); } catch (...) {}
  }

  // 绝不持 m_lock 后 join, 避免 reader 在 JS 线程里 emit 与被死记攀.
  void stop() {
    m_running = false;
    {
      std::lock_guard<std::mutex> lk(m_lock);
      if (m_cmdFd >= 0) { ::close(m_cmdFd); m_cmdFd = -1; }
    }
    {
      pid_t pid;
      { std::lock_guard<std::mutex> lk(m_lock); pid = m_pid; }
      if (pid > 0) {
        for (int i = 0; i < 50; i++) {
          if (waitpid(pid, NULL, WNOHANG) > 0) break;
          usleep(50000);
        }
        if (waitpid(pid, NULL, WNOHANG) <= 0) {
          kill(pid, SIGKILL);
          waitpid(pid, NULL, 0);
        }
        std::lock_guard<std::mutex> lk(m_lock);
        m_pid = -1;
        if (m_outFd >= 0) { ::close(m_outFd); m_outFd = -1; }
      }
    }
    if (m_reader.joinable() && m_reader.get_id() != std::this_thread::get_id()) m_reader.detach();
  }

  std::mutex m_lock;
  pid_t m_pid = -1;
  int m_cmdFd = -1;
  int m_outFd = -1;
  std::atomic<bool> m_running{false};
  std::thread m_reader;
};

static JSValue createVideoProbe(JQModuleEnv* env) {
  JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "videoprobe");
  tpl->InstanceTemplate()->setObjectCreator([]() {
    static VideoProbe* one = []() { VideoProbe* p = new VideoProbe(); p->REF(); return p; }();
    return one;
  });
  tpl->SetProtoMethod("open", &VideoProbe::open);
  tpl->SetProtoMethod("start", &VideoProbe::start);
  tpl->SetProtoMethod("stop", &VideoProbe::stopCommand);
  tpl->SetProtoMethod("close", &VideoProbe::close);
  tpl->InstanceTemplate()->Set("stateChanged", &VideoProbe::stateChanged);
  return tpl->CallConstructor();
}

void videoprobe_init(JQModuleEnv* env) {
  env->setModuleExport("videoprobe", createVideoProbe(env));
}

}  // namespace videoprobe
