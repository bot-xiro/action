#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bilibilipan 电脑端 Cookie 同步工具 (配套词典笔 App「电脑同步」登录)

用法 (电脑需与词典笔同一 WiFi 局域网):
  1. 电脑运行:  python pc-cookie-server.py            (默认端口 9527)
     启动后打印本机局域网 IP, 例如 http://192.168.1.100
  2. 电脑浏览器打开:  http://127.0.0.1:9527
     在文本框粘贴 B 站 Cookie (要求包含 SESSDATA), 点「保存」
     (获取方式: 电脑登录 bilibili.com → F12 → Network → 任一
      api.bilibili.com 请求 → Request Headers → 复制整行 Cookie)
  3. 词典笔 App: 我的 → 登录 → 电脑同步 → 输入电脑 IP → 「获取并登录」

安全提示: Cookie 相当于登录凭证, 本服务只监听局域网, 保存后请尽快
关闭程序 (Ctrl+C)。笔端获取成功后可立即关闭。

仅使用 Python 标准库, 无第三方依赖。Ctrl+C 退出。
"""
import http.server
import json
import os
import socket
import sys
import time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9527
COOKIE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bilibili-cookie.json')

PAGE = """<!doctype html>
<html lang="zh">
<head><meta charset="utf-8"><title>bilibilipan Cookie 同步</title>
<style>
body{font-family:-apple-system,"Segoe UI",Microsoft YaHei,sans-serif;background:#16181c;color:#e8edf3;
     display:flex;justify-content:center;padding:40px 16px}
.box{width:720px;max-width:96vw}
h1{font-size:22px;color:#fb7299;margin:0 0 8px}
p{color:#8a94a6;font-size:14px;line-height:1.7}
textarea{width:100%;height:120px;box-sizing:border-box;background:#21242b;color:#e8edf3;
         border:1px solid #37404a;border-radius:10px;padding:12px;font-size:13px}
button{margin-top:12px;background:#fb7299;color:#fff;border:0;border-radius:20px;
       padding:10px 28px;font-size:15px;cursor:pointer}
#msg{margin-left:12px;font-size:14px}
.ok{color:#3fd67a}.err{color:#ff7a7a}
code{background:#21242b;padding:2px 6px;border-radius:4px}
</style></head>
<body><div class="box">
<h1>bilibilipan Cookie 同步</h1>
<p>1. 电脑浏览器登录 <code>bilibili.com</code><br>
2. F12 打开开发者工具 → Network → 刷新页面 → 点任意 <code>api.bilibili.com</code> 请求<br>
3. Request Headers 里复制整行 <code>Cookie:</code> 值 (必须包含 SESSDATA)<br>
4. 粘贴到下面并保存, 然后在词典笔上操作: 我的 → 登录 → 电脑同步 → 输入电脑 IP → 获取并登录</p>
<textarea id="ck" placeholder="SESSDATA=xxx; bili_jct=xxx; DedeUserID=xxx; ..."></textarea><br>
<button onclick="save()">保存到本机服务</button><span id="msg"></span>
<script>
function save(){
  var ck=document.getElementById('ck').value;
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({cookie:ck})})
    .then(function(r){return r.json()})
    .then(function(j){
      var m=document.getElementById('msg');
      m.textContent=j.message; m.className=j.ok?'ok':'err';
    })
    .catch(function(e){var m=document.getElementById('msg');m.textContent='失败: '+e;m.className='err'});
}
fetch('/current').then(function(r){return r.json()}).then(function(j){ if(j.raw) document.getElementById('ck').value=j.raw; });
</script>
</div></body></html>"""

state = {'raw': '', 'parsed': {}, 'updated_at': 0}


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
        json.dump({'raw': state['raw'], 'updated_at': state['updated_at']}, f, ensure_ascii=False)


def load_state():
    global state
    if os.path.exists(COOKIE_FILE):
        try:
            with open(COOKIE_FILE, 'r', encoding='utf-8') as f:
                saved = json.load(f)
            state['raw'] = saved.get('raw', '')
            state['updated_at'] = saved.get('updated_at', 0)
            state['parsed'] = parse_cookie(state['raw'])
        except Exception as e:
            print('[!] 读取历史 Cookie 失败:', e)


class Handler(http.server.BaseHTTPRequestHandler):
    def _send(self, code, body, ctype='text/html; charset=utf-8'):
        data = body.encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == '/':
            self._send(200, PAGE)
        elif self.path.startswith('/current'):
            self._send(200, json.dumps({'ok': True, 'raw': state['raw']},
                                       ensure_ascii=False), 'application/json; charset=utf-8')
        elif self.path.startswith('/bilibilipan/cookie'):
            p = state['parsed']
            ok = bool(p.get('sessdata'))
            print('[<-] 笔端获取 Cookie (%s): %s' % (
                self.client_address[0], '成功' if ok else '未保存'))
            self._send(200, json.dumps({
                'ok': ok,
                'sessdata': p.get('sessdata', ''),
                'bili_jct': p.get('bili_jct', ''),
                'dedeuserid': p.get('dedeuserid', ''),
                'updated_at': state['updated_at']
            }, ensure_ascii=False), 'application/json; charset=utf-8')
        else:
            self._send(404, 'not found', 'text/plain')

    def do_POST(self):
        if self.path != '/save':
            self._send(404, 'not found', 'text/plain')
            return
        n = int(self.headers.get('Content-Length') or 0)
        body = self.rfile.read(n).decode('utf-8', 'replace')
        try:
            data = json.loads(body)
            raw = data.get('cookie', '')
        except Exception:
            raw = ''
        parsed = parse_cookie(raw)
        if parsed['sessdata']:
            state['raw'] = raw.strip()
            state['parsed'] = parsed
            state['updated_at'] = int(time.time())
            save_state()
            print('[+] Cookie 已保存 (SESSDATA %d 字节, csrf %s)' % (
                len(parsed['sessdata']), '有' if parsed['bili_jct'] else '无'))
            self._send(200, json.dumps({'ok': True, 'message': '✓ 已保存, 现在到词典笔上点「获取并登录」'},
                                       ensure_ascii=False), 'application/json; charset=utf-8')
        else:
            print('[!] 提交内容中未发现 SESSDATA')
            self._send(200, json.dumps({'ok': False, 'message': '未识别到 SESSDATA, 请复制完整的 Cookie 行'},
                                       ensure_ascii=False), 'application/json; charset=utf-8')

    def log_message(self, fmt, *args):
        pass  # 静默默认访问日志 (SESSDATA 会出现在 /current 请求里? 不会, 仅路径)


def main():
    load_state()
    ip = get_local_ip()
    print('bilibilipan Cookie 同步服务')
    print('  本机局域网 IP: %s  (词典笔上输入这个 IP)' % ip)
    print('  电脑浏览器打开: http://127.0.0.1:%d  粘贴 Cookie 并保存' % PORT)
    print('  笔端获取地址:  http://%s:%d/bilibilipan/cookie' % (ip, PORT))
    print('  Ctrl+C 退出')
    server = http.server.ThreadingHTTPServer(('0.0.0.0', PORT), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print('\n已退出')


if __name__ == '__main__':
    main()
