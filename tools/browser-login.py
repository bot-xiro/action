#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bilibilipan · 浏览器登录态导入工具
=================================

自动读取电脑上 Chrome / Edge 里**已经登录好的 B 站账号**, 取出
SESSDATA / bili_jct / DedeUserID, 校验是否有效, 然后自动填入
bilibili-cookie.json, 供词典笔 App 通过「电脑同步」获取。

用法:
    python browser-login.py              # 读取 -> 校验 -> 写入 -> 启动同步服务
    python browser-login.py --scan       # 只扫描列出找到的账号, 不写入
    python browser-login.py --no-serve   # 只读取写入, 不启动服务
    python browser-login.py 8080         # 指定同步服务端口 (默认 9527)

两条读取路径 (自动依次尝试):
  A. CDP 直读 (推荐)
     以 --remote-debugging-port 启动一个浏览器实例 (沿用你原本的 profile,
     登录态在), 用 DevTools 协议 Network.getAllCookies 直接拿明文 Cookie。
     这是**浏览器自己解密**的, 所以即使是 Chrome 127+ 的 v20
     (app-bound) 加密也能正常读出。
     前提: 该浏览器当前没有在运行 (否则 profile 被独占, 起不来调试实例)。

  B. 磁盘解密
     复制 <profile>/Network/Cookies (SQLite) 到临时文件, 用 DPAPI 解出
     Local State 里的 AES 主密钥, 再 AES-256-GCM 解 v10 前缀的
     encrypted_value。仅适用于未启用 app-bound 加密的旧版本浏览器。

如果两条都失败, 工具会打印具体原因和操作建议。

依赖: 解密 v10 需要 cryptography (pip install cryptography); CDP 路径无依赖。
"""
import base64
import ctypes
import ctypes.wintypes as wintypes
import json
import os
import random
import shutil
import socket
import sqlite3
import struct
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
COOKIE_FILE = os.path.join(HERE, 'bilibili-cookie.json')

NEED = ('SESSDATA', 'bili_jct', 'DedeUserID')
UA = ('Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 '
      '(KHTML, like Gecko) Chrome/132.0.0.0 Safari/537.36')
NAV_URL = 'https://api.bilibili.com/x/web-interface/nav'

IS_WIN = sys.platform.startswith('win')


# ============================================================ 定位浏览器
def find_browsers():
    """找出本机可用的 Chromium 系浏览器。

    返回 [(显示名, 可执行文件, user_data_dir), ...], 按「安装完整度」排序:
    完整安装优先 (自带 app-bound 解密组件), 残缺的 WebView2/EdgeCore 靠后。
    """
    out = []

    def add(label, exe, udd, full):
        if exe and udd and os.path.isfile(exe) and os.path.isdir(udd):
            out.append((label, exe, udd, full))

    if IS_WIN:
        pf = os.environ.get('ProgramFiles', r'C:\Program Files')
        pf86 = os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')
        la = os.environ.get('LOCALAPPDATA', '')
        # Chrome: 官方安装路径 + 用户级安装
        for exe in [os.path.join(pf, r'Google\Chrome\Application\chrome.exe'),
                    os.path.join(pf86, r'Google\Chrome\Application\chrome.exe'),
                    os.path.join(la, r'Google\Chrome\Application\chrome.exe')]:
            add('Chrome', exe, os.path.join(la, r'Google\Chrome\User Data'), True)
        # Edge: 完整安装
        add('Edge', os.path.join(pf86, r'Microsoft\Edge\Application\msedge.exe'),
            os.path.join(la, r'Microsoft\Edge\User Data'), True)
        # Edge 残缺安装 (只剩 EdgeCore): 能启动但没有 app-bound 解密组件
        core = os.path.join(pf86, r'Microsoft\EdgeCore')
        if os.path.isdir(core):
            try:
                vers = sorted([d for d in os.listdir(core)
                               if os.path.isdir(os.path.join(core, d))], reverse=True)
            except Exception:
                vers = []
            for v in vers[:1]:
                add('Edge(精简)', os.path.join(core, v, 'msedge.exe'),
                    os.path.join(la, r'Microsoft\Edge\User Data'), False)
    elif sys.platform == 'darwin':
        home = os.path.expanduser('~')
        add('Chrome', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
            os.path.join(home, 'Library/Application Support/Google/Chrome'), True)
        add('Edge', '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge',
            os.path.join(home, 'Library/Application Support/Microsoft Edge'), True)
    else:
        home = os.path.expanduser('~')
        for exe in ['/usr/bin/google-chrome', '/usr/bin/chromium',
                    '/usr/bin/microsoft-edge']:
            if os.path.isfile(exe):
                add(os.path.basename(exe), exe,
                    os.path.join(home, '.config', os.path.basename(exe)), True)

    # 去重 (同名只留第一个)
    seen, uniq = set(), []
    for it in out:
        if it[0] in seen:
            continue
        seen.add(it[0])
        uniq.append(it)
    # 完整安装优先
    uniq.sort(key=lambda x: 0 if x[3] else 1)
    return [(a, b, c) for a, b, c, _ in uniq]


def proc_running(exe_path):
    """判断该浏览器是否正在运行 (用其 profile 里的 SingletonLock 更准, 这里用进程名)。"""
    name = os.path.basename(exe_path).lower()
    try:
        out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq %s' % name],
                             capture_output=True, timeout=10)
        return name.encode('utf-8', 'replace').decode() in out.stdout.decode(
            'utf-8', 'replace').lower()
    except Exception:
        return False


def profile_has_cookies(udd, sub='Default'):
    """快速检查 profile 里是否有 Cookies 数据库 (不需要解密)。"""
    for rel in ['Network/Cookies', 'Cookies']:
        p = os.path.join(udd, sub, rel.replace('/', os.sep))
        if os.path.isfile(p) and os.path.getsize(p) > 0:
            return p
    return None


# ============================================================ 极简 WS 客户端
class MiniWS(object):
    """够用的 RFC6455 客户端 (CDP 只需要发文本帧/收文本帧)。"""

    def __init__(self, url, timeout=10):
        rest = url[5:]
        hostport, path = rest.split('/', 1)
        host, port = hostport.split(':')
        self.s = socket.create_connection((host, int(port)), timeout=timeout)
        key = base64.b64encode(bytes(random.getrandbits(8) for _ in range(16))).decode()
        self.s.sendall((
            'GET /%s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\n'
            'Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n'
            'Sec-WebSocket-Version: 13\r\n\r\n' % (path, hostport, key)).encode())
        buf = b''
        while b'\r\n\r\n' not in buf:
            d = self.s.recv(4096)
            if not d:
                raise RuntimeError('连接被关闭')
            buf += d
        if b'101' not in buf.split(b'\r\n')[0]:
            raise RuntimeError('WebSocket 握手失败')
        self.buf = buf.split(b'\r\n\r\n', 1)[1]

    def _recv(self, n):
        while len(self.buf) < n:
            d = self.s.recv(65536)
            if not d:
                raise RuntimeError('连接被关闭')
            self.buf += d
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def send(self, text):
        data = text.encode('utf-8')
        n = len(data)
        h = bytearray([0x81])
        if n < 126:
            h.append(0x80 | n)
        elif n < 65536:
            h.append(0x80 | 126)
            h += struct.pack('>H', n)
        else:
            h.append(0x80 | 127)
            h += struct.pack('>Q', n)
        mask = bytes(random.getrandbits(8) for _ in range(4))
        h += mask
        self.s.sendall(bytes(h) + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def recv(self):
        """收下一个文本帧 (CDP 响应通常单帧, 够用)。"""
        while True:
            b1, b2 = self._recv(2)
            op = b1 & 0x0f
            ln = b2 & 0x7f
            if ln == 126:
                ln = struct.unpack('>H', self._recv(2))[0]
            elif ln == 127:
                ln = struct.unpack('>Q', self._recv(8))[0]
            payload = self._recv(ln) if ln else b''
            if op == 0x8:
                raise RuntimeError('对端关闭')
            if op == 0x1:
                return payload.decode('utf-8', 'replace')

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass


# ============================================================ 路径 A: CDP
def cdp_cookies(exe, udd, headless=True, wait=25):
    """启动一个带调试端口的浏览器实例读取明文 Cookie。

    返回 (cookies_dict, 说明字符串)。失败时 cookies 为 None。
    """
    port = random.randint(20000, 40000)
    args = [exe, '--remote-debugging-port=%d' % port,
            '--user-data-dir=%s' % udd,
            '--no-first-run', '--no-default-browser-check',
            '--no-default-browser-check', '--disable-gpu',
            '--disable-extensions', '--no-sandbox']
    if headless:
        args.append('--headless=new')
    args.append('about:blank')

    proc = None
    try:
        proc = subprocess.Popen(args, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        ver = None
        deadline = time.time() + wait
        while time.time() < deadline:
            if proc.poll() is not None and ver is None:
                # 进程已退出: 多半是浏览器已在运行, 实例被接管
                return None, '浏览器已在运行, 无法启动调试实例'
            time.sleep(0.4)
            try:
                with urllib.request.urlopen(
                        'http://127.0.0.1:%d/json/version' % port, timeout=1.5) as r:
                    ver = json.loads(r.read().decode('utf-8', 'replace'))
                    break
            except Exception:
                pass
        if not ver:
            return None, '调试端口未就绪'

        with urllib.request.urlopen(
                'http://127.0.0.1:%d/json/list' % port, timeout=5) as r:
            targets = json.loads(r.read().decode('utf-8', 'replace'))
        pages = [t for t in targets if t.get('type') == 'page']
        if not pages:
            return None, '没有可用的页面'
        ws = MiniWS(pages[0]['webSocketDebuggerUrl'])
        try:
            ws.send(json.dumps({'id': 1, 'method': 'Network.enable', 'params': {}}))
            time.sleep(0.3)
            # Cookie 库是异步加载的, 轮询几次
            best = {}
            for mid in range(10, 16):
                ws.send(json.dumps({'id': mid, 'method': 'Network.getAllCookies',
                                    'params': {}}))
                end = time.time() + 4
                while time.time() < end:
                    try:
                        msg = json.loads(ws.recv())
                    except Exception:
                        break
                    if msg.get('id') != mid:
                        continue
                    cks = msg.get('result', {}).get('cookies', [])
                    for c in cks:
                        if c.get('name') in NEED and 'bilibili' in (c.get('domain') or ''):
                            best.setdefault(c['name'], c.get('value', ''))
                    break
                if len(best) >= 3:
                    break
                time.sleep(1.2)
            if best.get('SESSDATA'):
                return best, 'CDP 读取 (%s)' % ('headless' if headless else '窗口模式')
            return None, '该 profile 里没有 B 站登录态, 或浏览器无法解密本机 Cookie'
        finally:
            ws.close()
    except Exception as e:
        return None, 'CDP 出错: %s' % e
    finally:
        if proc:
            try:
                proc.terminate()
                proc.wait(timeout=6)
            except Exception:
                try:
                    proc.kill()
                except Exception:
                    pass


# ============================================================ 路径 B: 磁盘解密
def dpapi_decrypt(blob):
    """Windows DPAPI 解密 (当前用户凭据)。失败返回 None。"""
    if not IS_WIN:
        return None

    class DATA_BLOB(ctypes.Structure):
        _fields_ = [('cbData', wintypes.DWORD),
                    ('pbData', ctypes.POINTER(ctypes.c_char))]

    try:
        buf = ctypes.create_string_buffer(blob, len(blob))
        inb = DATA_BLOB(len(blob), buf)
        outb = DATA_BLOB()
        ok = ctypes.windll.crypt32.CryptUnprotectData(
            ctypes.byref(inb), None, None, None, None, 0, ctypes.byref(outb))
        if not ok:
            return None
        data = ctypes.string_at(outb.pbData, outb.cbData)
        ctypes.windll.kernel32.LocalFree(outb.pbData)
        return data
    except Exception:
        return None


def get_aes_key(udd):
    """读 Local State -> os_crypt.encrypted_key -> DPAPI -> AES 主密钥。"""
    ls = os.path.join(udd, 'Local State')
    if not os.path.isfile(ls):
        return None, '没有 Local State'
    try:
        with open(ls, 'r', encoding='utf-8') as f:
            enc_b64 = json.load(f)['os_crypt']['encrypted_key']
    except Exception as e:
        return None, '解析 Local State 失败: %s' % e
    try:
        blob = base64.b64decode(enc_b64)
    except Exception as e:
        return None, 'encrypted_key 不是合法 base64: %s' % e
    if blob[:5] == b'DPAPI':
        blob = blob[5:]
    key = dpapi_decrypt(blob)
    if not key:
        return None, 'DPAPI 解密主密钥失败'
    return key, ''


def decrypt_value(raw, key):
    """解 cookies.encrypted_value -> 明文。v20(app-bound) 无法解, 返回 None。"""
    if not raw:
        return None
    if isinstance(raw, str):
        raw = raw.encode('utf-8', 'replace')
    if raw[:3] in (b'v10', b'v11', b'v20'):
        if raw[:3] == b'v20':
            return None          # app-bound: 只有浏览器自己能解
        if not key:
            return None
        try:
            from cryptography.hazmat.primitives.ciphers.aead import AESGCM
            pt = AESGCM(key).decrypt(raw[3:15], raw[15:], None)
            return pt.decode('utf-8', 'replace')
        except Exception:
            return None
    pt = dpapi_decrypt(raw)
    return pt.decode('utf-8', 'replace') if pt else None


def disk_cookies(db_path, key):
    """复制 Cookie 库到临时文件后读出 B 站字段。"""
    res, tmp = {}, None
    try:
        fd, tmp = tempfile.mkstemp(prefix='bili_ck_', suffix='.sqlite')
        os.close(fd)
        shutil.copyfile(db_path, tmp)
        con = sqlite3.connect(tmp)
        con.text_factory = bytes        # 关键: 加密值是 BLOB, 不能按 UTF-8 解
        try:
            cur = con.cursor()
            cur.execute(
                "SELECT name, value, encrypted_value FROM cookies "
                "WHERE host_key LIKE '%bilibili.com%'")
            for name, value, enc in cur.fetchall():
                try:
                    name = name.decode('utf-8', 'replace')
                except Exception:
                    continue
                if name not in NEED or name in res:
                    continue
                v = decrypt_value(enc, key)
                if v is None and value:
                    try:
                        v = value.decode('utf-8', 'replace')
                    except Exception:
                        v = None
                if v:
                    res[name] = v
        finally:
            con.close()
    except Exception as e:
        res['__error__'] = str(e)
    finally:
        if tmp and os.path.exists(tmp):
            try:
                os.remove(tmp)
            except Exception:
                pass
    return res


def disk_read(udd, sub='Default'):
    """路径 B 入口。返回 (cookies, 说明)。"""
    db = profile_has_cookies(udd, sub)
    if not db:
        return None, '没有 Cookie 数据库'
    key, err = get_aes_key(udd)
    ck = disk_cookies(db, key)
    if ck.pop('__error__', None):
        return None, 'Cookie 数据库被占用 (请关闭浏览器后重试)'
    if ck.get('SESSDATA'):
        return ck, '磁盘解密'
    if not key:
        return None, err or '无法取得解密密钥'
    return None, 'Cookie 是 v20(app-bound) 加密, 需由浏览器自己解密'


# ============================================================ 校验 & 保存
def http_get(url, cookie, timeout=10):
    req = urllib.request.Request(url, headers={'User-Agent': UA, 'Cookie': cookie})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode('utf-8', 'replace'))
    except urllib.error.HTTPError as e:
        return {'code': -1, 'message': 'HTTP %s' % e.code}
    except Exception as e:
        return {'code': -1, 'message': str(e)}


def verify(ck):
    """调 B 站 nav 接口确认 Cookie 是否有效。返回 (ok, 描述, 详情)。"""
    cookie = 'SESSDATA=%s; bili_jct=%s; DedeUserID=%s' % (
        ck.get('SESSDATA', ''), ck.get('bili_jct', ''), ck.get('DedeUserID', ''))
    j = http_get(NAV_URL, cookie)
    if j.get('code') == 0 and j.get('data', {}).get('isLogin'):
        d = j['data']
        return True, '%s (UID %s)' % (d.get('uname', '?'), d.get('mid', '?')), {
            'uname': d.get('uname', ''),
            'mid': d.get('mid', ''),
            'face': d.get('face', ''),
            'level': d.get('level_info', {}).get('current_level', ''),
        }
    return False, j.get('message') or 'Cookie 无效或已过期', {}


def build_raw(ck):
    return 'SESSDATA=%s; bili_jct=%s; DedeUserID=%s' % (
        ck.get('SESSDATA', ''), ck.get('bili_jct', ''), ck.get('DedeUserID', ''))


def save_cookies(ck, extra=None):
    """写入 bilibili-cookie.json (与 pc-cookie-server.py 格式一致)。"""
    data = {'raw': build_raw(ck), 'updated_at': int(time.time())}
    if extra:
        data['account'] = extra
    with open(COOKIE_FILE, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    return COOKIE_FILE


# ============================================================ 汇总扫描
def collect(log=print):
    """依次尝试所有浏览器/所有路径。返回 [{'label','via','cookies'}]。"""
    found = []
    browsers = find_browsers()
    if not browsers:
        log('  [!] 没有找到 Chrome / Edge')
        return found
    for label, exe, udd in browsers:
        if profile_has_cookies(udd) is None:
            continue
        running = proc_running(exe)
        # 路径 A: CDP (要求浏览器未运行)
        if not running:
            log('  · %s: 尝试用浏览器自身读取 (CDP)...' % label)
            ck, why = cdp_cookies(exe, udd, headless=True)
            if not ck:
                log('      headless 未成功 (%s), 再试窗口模式...' % why)
                ck, why = cdp_cookies(exe, udd, headless=False)
            if ck:
                found.append({'label': label, 'via': why, 'cookies': ck})
                continue
            log('      CDP 失败: %s' % why)
        else:
            log('  · %s: 正在运行, 跳过 CDP (profile 被独占)' % label)
        # 路径 B: 磁盘解密
        ck, why = disk_read(udd)
        if ck:
            found.append({'label': label, 'via': why, 'cookies': ck})
        else:
            log('      磁盘解密失败: %s' % why)
    return found


def auto_import():
    """一站式: 返回第一个校验通过的 (cookies, 描述, 详情)。供 web 服务调用。"""
    for item in collect(log=lambda *a, **k: None):
        ok, desc, info = verify(item['cookies'])
        if ok:
            return item['cookies'], desc, info
    return None, '未找到浏览器登录态', {}


# ============================================================ main
def main():
    argv = sys.argv[1:]
    scan_only = '--scan' in argv
    no_serve = '--no-serve' in argv
    port = 9527
    for a in argv:
        if a.isdigit():
            port = int(a)

    print('=' * 62)
    print('bilibilipan · 浏览器登录态导入')
    print('=' * 62)
    if not IS_WIN:
        print('[!] 当前系统 %s 尚未支持自动读取, 请用手动方式。' % sys.platform)
        return 1

    print('\n[1/3] 扫描本机浏览器里的 B 站登录态...')
    found = collect()
    if not found:
        print('\n[×] 没能自动读到。常见原因:')
        print('    1) 浏览器正在运行 -> 完全退出 Chrome/Edge 后重试')
        print('       (托盘图标也要退, 后台进程会独占 profile)')
        print('    2) 浏览器没登录 bilibili.com -> 先登录再运行本工具')
        print('    3) Cookie 是 v20(app-bound) 加密且浏览器安装不完整')
        print('\n    可改用: start-server.bat -> 在登录页面粘贴 Cookie')
        return 1

    print('\n[2/3] 校验登录态是否有效...')
    picked = None
    for item in found:
        ok, desc, info = verify(item['cookies'])
        print('  [%s] %-14s %-12s %s' % ('√' if ok else '×', item['label'],
                                         item['via'], desc))
        if ok and picked is None:
            picked = (item, desc, info)

    if scan_only:
        print('\n(--scan 模式, 未写入文件)')
        return 0
    if not picked:
        print('\n[×] 读到的登录态都已失效, 请先在浏览器重新登录 bilibili.com。')
        return 1

    item, desc, info = picked
    print('\n[3/3] 自动填入 Cookie...')
    print('  来源: %s (%s)' % (item['label'], item['via']))
    print('  账号: %s' % desc)
    print('  已写入: %s' % save_cookies(item['cookies'], info))

    if no_serve:
        print('\n完成。可运行 start-server.bat 启动同步服务。')
        return 0

    print('\n启动同步服务, 供词典笔获取 (Ctrl+C 退出)...\n')
    # pc-cookie-server.py 文件名带连字符, 不能直接 import, 用 importlib 加载
    import importlib.util
    srv = os.path.join(HERE, 'pc-cookie-server.py')
    if not os.path.isfile(srv):
        print('[!] 找不到 pc-cookie-server.py, 请手动运行 start-server.bat')
        return 1
    spec = importlib.util.spec_from_file_location('pc_cookie_server', srv)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    mod.PORT = port                      # exec_module 之后再覆盖端口
    mod.main()
    return 0


if __name__ == '__main__':
    sys.exit(main() or 0)
