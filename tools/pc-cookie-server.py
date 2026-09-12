#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bilibilipan 电脑端登录 / Cookie 同步服务 (配套词典笔 App「电脑同步」)

用法 (电脑需与词典笔同一 WiFi 局域网):
  1. 电脑运行:  python pc-cookie-server.py            (默认端口 9527)
     一键启动:   start-server.bat  (Windows) / start-server.sh (Linux,macOS)
  2. 浏览器会自动打开 http://127.0.0.1:9527  —— 这是「登录页面」:
       · 方式一: 点「从浏览器读取登录态」, 自动读取 Chrome/Edge 里
                 已登录的 B 站账号 (要求浏览器当前完全退出)
       · 方式二: 在页面里粘贴 B 站 Cookie (页面会自动检测剪贴板)
  3. 词典笔 App: 我的 → 登录 → 电脑同步 → 输入电脑 IP → 「获取并登录」

安全提示: Cookie 相当于登录凭证, 本服务只监听局域网, 用完后请尽快
关闭程序 (Ctrl+C)。笔端获取成功后可立即关闭。

仅使用 Python 标准库; 「从浏览器读取」为可选能力, 需要
tools/browser-login.py 与 (解密旧版 Cookie 时的) cryptography。
"""
import http.server
import importlib.util
import json
import os
import socket
import sys
import time
import urllib.error
import urllib.request
import webbrowser
import threading

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9527
COOKIE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bilibili-cookie.json')

UA = ('Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 '
      '(KHTML, like Gecko) Chrome/132.0.0.0 Safari/537.36')
NAV_URL = 'https://api.bilibili.com/x/web-interface/nav'

PAGE = """<!doctype html>
<html lang="zh">
<head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>bilibilipan 登录</title>
<style>
*{box-sizing:border-box}
body{font-family:-apple-system,"Segoe UI",Microsoft YaHei,sans-serif;background:#16181c;
     color:#e8edf3;margin:0;display:flex;justify-content:center;padding:36px 16px 60px}
.box{width:760px;max-width:96vw}
h1{font-size:24px;color:#fb7299;margin:0 0 4px}
.sub{color:#8a94a6;font-size:13px;margin:0 0 22px}
.card{background:#1d2027;border:1px solid #2b313a;border-radius:14px;padding:20px;margin-bottom:16px}
.card h2{font-size:15px;margin:0 0 14px;color:#c9d3e0;font-weight:600}
.row{display:flex;align-items:center;gap:14px}
.face{width:52px;height:52px;border-radius:50%;background:#2b313a;object-fit:cover;flex:none}
.dot{width:10px;height:10px;border-radius:50%;background:#5a6472;flex:none}
.dot.on{background:#3fd67a;box-shadow:0 0 8px #3fd67a88}
.dot.off{background:#ff7a7a}
.uname{font-size:18px;font-weight:600}
.meta{color:#8a94a6;font-size:13px;margin-top:3px}
button{background:#fb7299;color:#fff;border:0;border-radius:20px;padding:10px 24px;
       font-size:14px;cursor:pointer;font-family:inherit}
button:disabled{background:#4a525d;cursor:not-allowed}
button.ghost{background:transparent;border:1px solid #4a525d;color:#c9d3e0}
button.sm{padding:7px 16px;font-size:13px}
textarea{width:100%;height:104px;background:#141710;color:#d6e4c8;border:1px solid #37404a;
         border-radius:10px;padding:12px;font-size:12px;font-family:Consolas,monospace;resize:vertical}
.tip{color:#8a94a6;font-size:12.5px;line-height:1.75;margin:10px 0 0}
.tip b{color:#c9d3e0}
code{background:#21242b;padding:1px 6px;border-radius:4px;color:#fb7299}
.msg{margin-top:12px;font-size:13.5px;min-height:20px;word-break:break-all}
.ok{color:#3fd67a}.err{color:#ff7a7a}.dim{color:#8a94a6}
.ip{font-size:26px;color:#fb7299;font-weight:700;letter-spacing:1px;font-family:Consolas,monospace}
.grow{flex:1}
.split{display:flex;gap:12px;align-items:flex-start}
.split>div:first-child{flex:1}
</style></head>
<body><div class="box">
<h1>bilibilipan 登录</h1>
<p class="sub">把电脑上已登录的 B 站账号同步到词典笔</p>

<div class="card">
  <h2>当前登录状态</h2>
  <div class="row">
    <img class="face" id="face" alt="">
    <div class="grow">
      <div><span class="dot" id="dot"></span> <span class="uname" id="uname">检查中…</span></div>
      <div class="meta" id="meta">正在校验 Cookie…</div>
    </div>
    <button class="ghost sm" onclick="check()">刷新</button>
  </div>
  <div class="msg" id="stmsg"></div>
</div>

<div class="card">
  <h2>方式一 · 从浏览器自动读取</h2>
  <div class="row">
    <button id="btnAuto" onclick="autoRead()">从 Chrome / Edge 读取登录态</button>
    <span class="tip" style="margin:0">读取前请<b>完全退出</b>浏览器（含后台进程）</span>
  </div>
  <p class="tip">原理：以调试端口启动浏览器实例，通过浏览器自身读出已登录的 Cookie。
     若浏览器正在运行，profile 被独占，会读取失败。</p>
  <div class="msg" id="automsg"></div>
</div>

<div class="card">
  <h2>方式二 · 手动粘贴 Cookie</h2>
  <textarea id="ck" placeholder="SESSDATA=xxx; bili_jct=xxx; DedeUserID=xxx; ...&#10;&#10;（在本页面复制后会自动填入，无需手动粘贴）"
            oninput="onEdit()"></textarea>
  <div class="row" style="margin-top:12px">
    <button onclick="save()">保存并使用</button>
    <button class="ghost sm" onclick="copySteps()">复制获取步骤</button>
    <span class="tip" style="margin:0" id="clipTip">等待剪贴板…</span>
  </div>
  <p class="tip">获取方法：浏览器登录 <code>bilibili.com</code> → F12 → Network →
     刷新 → 点任一 <code>api.bilibili.com</code> 请求 → Request Headers → 复制整行
     <code>Cookie:</code>。复制后回到本页会<b>自动填入并保存</b>。</p>
  <div class="msg" id="savemsg"></div>
</div>

<div class="card">
  <h2>词典笔获取</h2>
  <div class="split">
    <div>
      <div class="meta">在词典笔「电脑同步」里输入这个 IP：</div>
      <div class="ip" id="ip">--</div>
    </div>
    <div class="tip" style="margin:0">笔端：<code>我的 → 登录 → 电脑同步</code><br>
      输入 IP → 点「获取并登录」</div>
  </div>
</div>
</div>
<script>
var lastSaved='';
function set(el,t,cls){var e=document.getElementById(el);e.textContent=t;e.className='msg '+(cls||'');}
function check(){
  document.getElementById('uname').textContent='检查中…';
  document.getElementById('meta').textContent='正在校验 Cookie…';
  document.getElementById('dot').className='dot';
  fetch('/api/account').then(r=>r.json()).then(j=>{
    document.getElementById('face').src=j.face||'';
    document.getElementById('dot').className='dot '+(j.ok?'on':'off');
    document.getElementById('uname').textContent=j.ok?j.uname:'未登录';
    document.getElementById('meta').textContent=j.ok?('UID '+j.mid+(j.level?' · LV'+j.level:'')):(j.message||'');
    if(j.raw) lastSaved=j.raw;
  }).catch(e=>set('stmsg','校验失败: '+e,'err'));
}
function autoRead(){
  var b=document.getElementById('btnAuto');b.disabled=true;b.textContent='读取中…';
  set('automsg','正在扫描浏览器，约需 10~60 秒…','dim');
  fetch('/api/browser',{method:'POST'}).then(r=>r.json()).then(j=>{
    b.disabled=false;b.textContent='从 Chrome / Edge 读取登录态';
    set('automsg',j.message,j.ok?'ok':'err');
    if(j.ok){document.getElementById('ck').value=j.raw;lastSaved=j.raw;check();}
  }).catch(e=>{b.disabled=false;b.textContent='从 Chrome / Edge 读取登录态';
    set('automsg','读取失败: '+e,'err');});
}
function onEdit(){ /* 手动编辑时不做自动保存 */ }
function save(){
  var v=document.getElementById('ck').value.trim();
  if(!v){set('savemsg','请先粘贴 Cookie','err');return;}
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},
        body:JSON.stringify({cookie:v})}).then(r=>r.json()).then(j=>{
    set('savemsg',j.message,j.ok?'ok':'err');
    if(j.ok){lastSaved=v;check();}
  });
}
function copySteps(){
  var s='1. 浏览器登录 bilibili.com\\n2. F12 → Network → 刷新页面\\n'+
        '3. 点任一 api.bilibili.com 请求 → Request Headers\\n4. 复制 Cookie: 整行';
  if(navigator.clipboard&&navigator.clipboard.writeText){navigator.clipboard.writeText(s);}
}
// 剪贴板自动检测: 复制含 SESSDATA 的内容后自动填入并保存
async function watchClip(){
  try{
    var t=await navigator.clipboard.readText();
    if(t&&t.indexOf('SESSDATA=')>=0&&t.trim()!==lastSaved.trim()){
      var ck=document.getElementById('ck');
      ck.value=t.trim();
      document.getElementById('clipTip').textContent='已自动填入';
      fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},
            body:JSON.stringify({cookie:t.trim()})}).then(r=>r.json()).then(j=>{
        lastSaved=t.trim();
        set('savemsg','检测到剪贴板 Cookie，已自动保存','ok');
        check();
      });
    }
  }catch(e){ document.getElementById('clipTip').textContent='剪贴板不可用，请手动粘贴'; }
}
fetch('/api/info').then(r=>r.json()).then(j=>{document.getElementById('ip').textContent=j.ip;});
fetch('/current').then(r=>r.json()).then(j=>{if(j.raw){document.getElementById('ck').value=j.raw;lastSaved=j.raw;}});
check();
setInterval(watchClip,1500);
</script>
</div></body></html>"""

state = {'raw': '', 'parsed': {}, 'updated_at': 0}
_busy = {'running': False}


def parse_cookie(raw):
    """从整行 Cookie / url 参数 / 裸 SESSDATA 中提取关键字段"""
    out = {'sessdata': '', 'bili_jct': '', 'dedeuserid': ''}
    for part in raw.replace('&', ';').replace('?', ';').split(';'):
        if '=' not in part:
            continue
        k, v = part.split('=', 1)
        k = k.strip().strip('"')
        v = v.strip().strip('"')
        lk = k.lower()
        if lk == 'sessdata' and not out['sessdata']:
            out['sessdata'] = v
        elif lk == 'bili_jct' and not out['bili_jct']:
            out['bili_jct'] = v
        elif lk == 'dedeuserid' and not out['dedeuserid']:
            out['dedeuserid'] = v
    if not out['sessdata'] and '=' not in raw:
        out['sessdata'] = raw.strip()
    return out


def save_state():
    with open(COOKIE_FILE, 'w', encoding='utf-8') as f:
        json.dump({'raw': state['raw'], 'updated_at': state['updated_at']}, f,
                  ensure_ascii=False, indent=2)


def load_state():
    if os.path.exists(COOKIE_FILE):
        try:
            with open(COOKIE_FILE, 'r', encoding='utf-8') as f:
                saved = json.load(f)
            state['raw'] = saved.get('raw', '')
            state['updated_at'] = saved.get('updated_at', 0)
            state['parsed'] = parse_cookie(state['raw'])
        except Exception as e:
            print('[!] 读取历史 Cookie 失败:', e)


def get_local_ip():
    """获取本机局域网 IP (给笔端用来访问).

    方法: 临时 UDP 连接一个公网地址 (不发数据), 让内核选路由, 从 socket
    读出本机对外 IP. 拿不到时按网卡名顺序 fallback.
    始终返回字符串, 失败兜底为 '127.0.0.1'."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.connect(('8.8.8.8', 80))
            ip = s.getsockname()[0]
            if ip and not ip.startswith('127.'):
                return ip
        finally:
            s.close()
    except Exception:
        pass
    try:
        host = socket.gethostname()
        infos = socket.gethostbyname_ex(host)
        for ip in infos[2]:
            if not ip.startswith('127.') and ':' not in ip:
                return ip
    except Exception:
        pass
    return '127.0.0.1'


def nav_check():
    """用当前 Cookie 调 B 站 nav 接口, 返回 (ok, uname, mid, face, level, message)"""
    p = state['parsed']
    if not p.get('sessdata'):
        return False, '', '', '', '', '还没有保存 Cookie'
    cookie = 'SESSDATA=%s; bili_jct=%s; DedeUserID=%s' % (
        p['sessdata'], p.get('bili_jct', ''), p.get('dedeuserid', ''))
    req = urllib.request.Request(NAV_URL, headers={'User-Agent': UA, 'Cookie': cookie})
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            j = json.loads(r.read().decode('utf-8', 'replace'))
    except urllib.error.HTTPError as e:
        return False, '', '', '', '', 'HTTP %s' % e.code
    except Exception as e:
        return False, '', '', '', '', str(e)
    if j.get('code') == 0 and j.get('data', {}).get('isLogin'):
        d = j['data']
        return (True, d.get('uname', ''), str(d.get('mid', '')), d.get('face', ''),
                d.get('level_info', {}).get('current_level', ''), 'ok')
    return False, '', '', '', '', j.get('message') or 'Cookie 无效或已过期'


def load_browser_login():
    """动态加载 tools/browser-login.py (文件名带连字符, 不能直接 import)。"""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'browser-login.py')
    if not os.path.isfile(path):
        return None, '缺少 tools/browser-login.py'
    try:
        spec = importlib.util.spec_from_file_location('browser_login', path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        return mod, ''
    except Exception as e:
        return None, '加载 browser-login.py 失败: %s' % e


def do_browser_import():
    """从浏览器读取登录态并写入。返回 (ok, message, raw)"""
    mod, err = load_browser_login()
    if not mod:
        return False, err, ''
    try:
        found = mod.collect(log=print)
    except Exception as e:
        return False, '扫描出错: %s' % e, ''
    if not found:
        return False, ('没有读到浏览器登录态。请先完全退出 Chrome / Edge '
                       '(含后台进程), 并确认浏览器里已登录 bilibili.com'), ''
    msgs = []
    for item in found:
        ok, desc, info = mod.verify(item['cookies'])
        msgs.append('%s: %s' % (item['label'], desc))
        if ok:
            raw = mod.build_raw(item['cookies'])
            state['raw'] = raw
            state['parsed'] = mod.parse_cookie(raw) if hasattr(mod, 'parse_cookie') \
                else parse_cookie(raw)
            state['updated_at'] = int(time.time())
            save_state()
            return True, '已读取并保存: %s (%s)' % (desc, item['via']), raw
    return False, '读到但已失效: ' + '; '.join(msgs), ''


class Handler(http.server.BaseHTTPRequestHandler):
    def _send(self, code, body, ctype='text/html; charset=utf-8'):
        data = body.encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _json(self, obj):
        self._send(200, json.dumps(obj, ensure_ascii=False),
                   'application/json; charset=utf-8')

    def do_GET(self):
        if self.path == '/':
            self._send(200, PAGE)
        elif self.path.startswith('/api/info'):
            self._json({'ip': get_local_ip(), 'port': PORT})
        elif self.path.startswith('/api/account'):
            ok, uname, mid, face, level, msg = nav_check()
            self._json({'ok': ok, 'uname': uname, 'mid': mid, 'face': face,
                        'level': level, 'message': msg, 'raw': state['raw']})
        elif self.path.startswith('/current'):
            self._json({'ok': True, 'raw': state['raw']})
        elif self.path.startswith('/bilibilipan/cookie'):
            p = state['parsed']
            ok = bool(p.get('sessdata'))
            print('[<-] 笔端获取 Cookie (%s): %s' % (
                self.client_address[0], '成功' if ok else '未保存'))
            self._json({
                'ok': ok,
                'sessdata': p.get('sessdata', ''),
                'bili_jct': p.get('bili_jct', ''),
                'dedeuserid': p.get('dedeuserid', ''),
                'updated_at': state['updated_at']
            })
        else:
            self._send(404, 'not found', 'text/plain')

    def do_POST(self):
        if self.path == '/save':
            n = int(self.headers.get('Content-Length') or 0)
            body = self.rfile.read(n).decode('utf-8', 'replace')
            try:
                raw = json.loads(body).get('cookie', '')
            except Exception:
                raw = ''
            parsed = parse_cookie(raw)
            if parsed['sessdata']:
                state['raw'] = raw.strip()
                state['parsed'] = parsed
                state['updated_at'] = int(time.time())
                save_state()
                print('[+] Cookie 已保存 (SESSDATA %d 字符, csrf %s)' % (
                    len(parsed['sessdata']), '有' if parsed['bili_jct'] else '无'))
                self._json({'ok': True, 'message': '✓ 已保存, 现在到词典笔上点「获取并登录」'})
            else:
                print('[!] 提交内容中未发现 SESSDATA')
                self._json({'ok': False, 'message': '未识别到 SESSDATA, 请复制完整的 Cookie 行'})
            return

        if self.path == '/api/browser':
            if _busy['running']:
                self._json({'ok': False, 'message': '上一次读取还在进行中'})
                return
            _busy['running'] = True
            try:
                ok, msg, raw = do_browser_import()
                self._json({'ok': ok, 'message': msg, 'raw': raw})
            finally:
                _busy['running'] = False
            return

        self._send(404, 'not found', 'text/plain')

    def log_message(self, fmt, *args):
        pass  # 静默默认访问日志


def main(open_browser=None):
    if open_browser is None:
        open_browser = '--no-open' not in sys.argv
    load_state()
    ip = get_local_ip()
    print('bilibilipan 登录 / Cookie 同步服务')
    print('  电脑登录页面: http://127.0.0.1:%d' % PORT)
    print('  词典笔输入 IP: %s  (笔端: 我的 → 登录 → 电脑同步)' % ip)
    print('  笔端获取地址:  http://%s:%d/bilibilipan/cookie' % (ip, PORT))
    print('  Ctrl+C 退出')
    if open_browser:
        threading.Timer(0.8, lambda: webbrowser.open(
            'http://127.0.0.1:%d' % PORT)).start()
    server = http.server.ThreadingHTTPServer(('0.0.0.0', PORT), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print('\n已退出')


if __name__ == '__main__':
    main()
