#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bilibilipan 扫码登录工具 (PC 端一键获取 Cookie)

用法:
    python tools/qr-login.py
    或一键脚本: tools/start-login.sh / start-login.bat

流程:
    1. 调用 B 站 /x/passport-login/web/qrcode/generate 拿到 auth_code + 二维码 URL
    2. 自动用默认浏览器打开 URL (用户拿手机 B 站 App 扫)
    3. 轮询 /x/passport-login/web/qrcode/poll 直至用户扫码确认 (code=0)
    4. 从响应体 redirect url 的 query 里解出 SESSDATA / bili_jct / DedeUserID
    5. 写入同目录的 bilibili-cookie.json (与 pc-cookie-server.py 共享)

成功后:
    - 笔端 App: 我的 → 登录 → 电脑同步 → 输入电脑 IP → 获取并登录
    - 或保持 tools/pc-cookie-server.py 运行, 笔端会直接拉取

仅使用 Python 标准库, 无第三方依赖. Ctrl+C 中断.
"""
import json
import os
import sys
import time
import urllib.parse
import urllib.request
import webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
COOKIE_FILE = os.path.join(HERE, 'bilibili-cookie.json')

UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 ' \
     '(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36'

GEN_URL = 'https://passport.bilibili.com/x/passport-login/web/qrcode/generate'
POLL_URL = 'https://passport.bilibili.com/x/passport-login/web/qrcode/poll'


def http_get(url, timeout=10):
    """GET 一个 JSON, 返回 dict. 出错抛 RuntimeError."""
    req = urllib.request.Request(url, headers={'User-Agent': UA})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        body = r.read().decode('utf-8', 'replace')
    try:
        return json.loads(body)
    except Exception as e:
        raise RuntimeError('响应非 JSON: ' + body[:200]) from e


def parse_login_url(url):
    """从扫码成功返回的 redirect url 里解出三个 Cookie 字段."""
    out = {'sessdata': '', 'bili_jct': '', 'dedeuserid': ''}
    if not url:
        return None
    qs = url.split('?', 1)[1] if '?' in url else ''
    for kv in qs.split('&'):
        if '=' not in kv:
            continue
        k, v = kv.split('=', 1)
        try:
            v = urllib.parse.unquote(v)
        except Exception:
            pass
        if k == 'SESSDATA':
            out['sessdata'] = v
        elif k == 'bili_jct':
            out['bili_jct'] = v
        elif k == 'DedeUserID':
            out['dedeuserid'] = v
    return out if out['sessdata'] else None


def save_cookies(sessdata, bili_jct, dedeuserid, raw=''):
    """写入同目录的 bilibili-cookie.json (pc-cookie-server.py 直接读它)."""
    data = {
        'raw': raw or '; '.join(
            filter(None, [
                f'SESSDATA={sessdata}',
                f'bili_jct={bili_jct}' if bili_jct else '',
                f'DedeUserID={dedeuserid}' if dedeuserid else '',
            ])
        ),
        'updated_at': int(time.time())
    }
    with open(COOKIE_FILE, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)


def main():
    print('=== bilibilipan 扫码登录 ===')
    print()

    # 1) 生成二维码
    try:
        body = http_get(GEN_URL)
    except Exception as e:
        print('[错误] 生成二维码失败:', e)
        print('        请检查网络, 或确认 B 站接口未被本地代理拦截')
        sys.exit(1)

    if body.get('code') != 0 or not body.get('data'):
        print('[错误] 二维码接口返回异常:', body.get('message') or ('code=' + str(body.get('code'))))
        sys.exit(1)

    data = body['data']
    qrcode_key = data.get('qrcode_key', '')
    qr_url = data.get('url', '')
    if not qrcode_key or not qr_url:
        print('[错误] 响应缺少 qrcode_key / url')
        sys.exit(1)

    print('二维码已生成, 正在打开默认浏览器...')
    print('  若浏览器未自动打开, 请手动访问下方 URL:')
    print('  ' + qr_url)
    print()
    print('操作步骤:')
    print('  1. 在浏览器打开的页面里, 用【手机 B 站 App】扫二维码')
    print('  2. 手机 App 上点确认登录')
    print('  3. 这里会等待到确认成功, 然后自动写入 Cookie')
    print()
    try:
        webbrowser.open(qr_url)
    except Exception as e:
        print('[警告] 自动打开浏览器失败:', e)
        print('        请手动复制上面那行 URL 到浏览器')

    print('等待扫码... (3 分钟内有效, Ctrl+C 中断)')

    # 2) 轮询
    deadline = time.time() + 180  # 3 分钟过期
    interval = 2.0
    last_state = ''
    while time.time() < deadline:
        time.sleep(interval)
        try:
            body = http_get(POLL_URL + '?qrcode_key=' + urllib.parse.quote(qrcode_key))
        except Exception as e:
            print('  [网络抖动] ' + str(e) + ', 重试中...')
            continue
        if body.get('code') != 0 or not body.get('data'):
            continue
        c = body['data'].get('code')
        if c == 0:
            # 扫码成功
            url = body['data'].get('url', '')
            cookies = parse_login_url(url)
            if not cookies or not cookies['sessdata']:
                print('[错误] 已确认登录, 但响应里没有 SESSDATA')
                sys.exit(2)
            save_cookies(cookies['sessdata'], cookies['bili_jct'], cookies['dedeuserid'], raw=url)
            print()
            print('✓ 登录成功, Cookie 已写入:', COOKIE_FILE)
            print('  SESSDATA : %d 字节' % len(cookies['sessdata']))
            print('  bili_jct : %s' % ('有' if cookies['bili_jct'] else '无'))
            print('  DedeUserID: %s' % (cookies['dedeuserid'] or '无'))
            print()
            print('下一步: 保持 tools/pc-cookie-server.py 运行,')
            print('        在词典笔上: 我的 → 登录 → 电脑同步 → 输入电脑 IP → 获取并登录')
            return
        elif c == 86090:
            state = '已扫描, 请在手机上确认'
        elif c == 86038:
            print()
            print('[已过期] 请重新运行本脚本')
            sys.exit(3)
        else:
            state = '等待扫描...'
        if state != last_state:
            print('  ' + state)
            last_state = state

    print()
    print('[已超时] 3 分钟内未确认, 请重新运行本脚本')
    sys.exit(4)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('\n[中断] 用户取消')
        sys.exit(130)