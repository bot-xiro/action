#!/usr/bin/env python3
"""
Bilibili Cookie 同步服务 - 电脑端运行
从浏览器提取 Bilibili Cookie，提供 HTTP 接口供设备拉取
"""

import json
import sqlite3
import os
import sys
import platform
import shutil
import tempfile
from pathlib import Path
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
import threading
import webbrowser

try:
    from browser_cookie3 import chrome, firefox, edge, chromium
except ImportError:
    print("安装依赖: pip install browser-cookie3 flask")
    sys.exit(1)

try:
    from flask import Flask, jsonify, request
except ImportError:
    print("安装依赖: pip install flask")
    sys.exit(1)

app = Flask(__name__)

# 全局缓存
cookie_cache = None
last_error = None


def get_browser_cookie_domains():
    """获取需要提取的域名"""
    return [
        '.bilibili.com',
        'bilibili.com',
        '.hdslb.com',
        'hdslb.com',
    ]


def extract_cookies_from_browser(browser_name='chrome'):
    """从指定浏览器提取 Bilibili 相关 Cookie"""
    cookies = {}
    domains = get_browser_cookie_domains()
    
    try:
        if browser_name == 'chrome':
            cj = chrome(domain_name='bilibili.com')
        elif browser_name == 'firefox':
            cj = firefox(domain_name='bilibili.com')
        elif browser_name == 'edge':
            cj = edge(domain_name='bilibili.com')
        elif browser_name == 'chromium':
            cj = chromium(domain_name='bilibili.com')
        else:
            return None, f"不支持的浏览器: {browser_name}"
    except Exception as e:
        return None, f"打开浏览器 Cookie 数据库失败 ({browser_name}): {e}"

    # 过滤 Bilibili 相关域名
    for cookie in cj:
        for domain in domains:
            if domain in cookie.domain or cookie.domain.endswith(domain.lstrip('.')):
                cookies[cookie.name] = cookie.value
                break
    
    return cookies, None


def try_all_browsers():
    """尝试所有支持的浏览器，返回第一个成功的"""
    browsers = ['chrome', 'edge', 'chromium', 'firefox']
    for browser in browsers:
        cookies, error = extract_cookies_from_browser(browser)
        if cookies and len(cookies) > 0:
            # 验证关键字段
            required = ['SESSDATA', 'bili_jct', 'DedeUserID']
            has_required = all(k in cookies for k in required)
            if has_required:
                return cookies, browser, None
            else:
                missing = [k for k in ['SESSDATA', 'bili_jct', 'DedeUserID'] if k not in cookies]
                print(f"  {browser}: 缺少关键字段 {missing}")
        elif error:
            print(f"  {browser}: {error}")
    
    return None, None, "所有浏览器均未获取到有效的 Bilibili Cookie (需包含 SESSDATA, bili_jct, DedeUserID)"


@app.route('/cookie')
def get_cookie():
    """返回 Cookie 供设备拉取"""
    global cookie_cache, last_error
    
    # 支持查询参数指定浏览器
    browser = request.args.get('browser', 'auto')
    
    if browser != 'auto':
        cookies, error = extract_cookies_from_browser(browser)
        if error:
            return jsonify({'code': -1, 'message': error, 'browser': browser}), 400
        if not cookies:
            return jsonify({'code': -1, 'message': f'{browser} 未找到 Cookie', 'browser': browser}), 404
    else:
        # 自动模式：使用缓存或重新获取
        if cookie_cache is None:
            cookies, browser_used, error = try_all_browsers()
            if error:
                last_error = error
                return jsonify({'code': -1, 'message': error}), 500
            cookie_cache = cookies
        else:
            cookies = cookie_cache
    
    # 验证关键字段
    required = ['SESSDATA', 'bili_jct', 'DedeUserID']
    missing = [k for k in required if k not in cookies]
    if missing:
        return jsonify({
            'code': -1, 
            'message': f'Cookie 缺少关键字段: {missing}',
            'cookie': cookies
        }), 400
    
    return jsonify({
        'code': 0,
        'message': 'success',
        'cookie': cookies,
        'cookie_string': '; '.join([f'{k}={v}' for k, v in cookies.items()])
    })


@app.route('/browsers')
def list_browsers():
    """列出可用浏览器及其 Cookie 状态"""
    result = {}
    for browser in ['chrome', 'edge', 'chromium', 'firefox']:
        cookies, error = extract_cookies_from_browser(browser)
        result[browser] = {
            'available': cookies is not None and len(cookies) > 0,
            'count': len(cookies) if cookies else 0,
            'has_required': all(k in (cookies or {}) for k in ['SESSDATA', 'bili_jct', 'DedeUserID']),
            'error': error
        }
    return jsonify(result)


@app.route('/refresh')
def refresh_cache():
    """强制刷新缓存"""
    global cookie_cache
    cookie_cache = None
    cookies, browser_used, error = try_all_browsers()
    if error:
        return jsonify({'code': -1, 'message': error}), 500
    cookie_cache = cookies
    return jsonify({'code': 0, 'message': 'refreshed', 'browser': browser_used, 'cookie': cookies})


@app.route('/')
def index():
    """简单的状态页面"""
    return '''
    <html>
    <head><title>Bilibili Cookie Sync Server</title>
    <style>body{font-family:monospace;padding:20px;max-width:800px;margin:auto}
    pre{background:#f4f4f4;padding:10px;border-radius:4px;overflow:auto}
    .ok{color:green}.err{color:red}</style></head>
    <body>
    <h1>Bilibili Cookie Sync Server</h1>
    <p>设备端请求: <code>GET http://<IP>:5000/cookie</code></p>
    <p>指定浏览器: <code>GET http://<IP>:5000/cookie?browser=chrome</code></p>
    <p>刷新缓存: <code>GET http://<IP>:5000/refresh</code></p>
    <hr>
    <h2>可用浏览器状态</h2>
    <div id="browsers">加载中...</div>
    <hr>
    <h2>Cookie 数据</h2>
    <pre id="cookie">点击刷新...</pre>
    <button onclick="load()">刷新</button>
    <script>
    async function load(){
        document.getElementById('browsers').innerHTML='加载中...';
        document.getElementById('cookie').innerText='加载中...';
        const [b, c] = await Promise.all([
            fetch('/browsers').then(r=>r.json()),
            fetch('/cookie').then(r=>r.json())
        ]);
        document.getElementById('browsers').innerHTML = 
            Object.entries(b).map(([k,v])=>`<div>${k}: <span class="${v.available?'ok':'err'}">${v.available?'可用('+v.count+'个)':'不可用'}</span> ${v.has_required?'✓关键字段完整':'✗缺关键字段'}</div>`).join('');
        document.getElementById('cookie').innerText = JSON.stringify(c, null, 2);
    }
    load();
    </script>
    </body>
    </html>
    '''


def get_local_ip():
    """获取本机局域网 IP"""
    try:
        import socket
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except:
        return "127.0.0.1"


def main():
    import argparse
    parser = argparse.ArgumentParser(description='Bilibili Cookie Sync Server')
    parser.add_argument('--port', type=int, default=5000, help='端口 (默认 5000)')
    parser.add_argument('--host', default='0.0.0.0', help='监听地址 (默认 0.0.0.0)')
    parser.add_argument('--browser', default='auto', choices=['auto', 'chrome', 'edge', 'chromium', 'firefox'], help='指定浏览器')
    args = parser.parse_args()
    
    # 预热缓存
    if args.browser != 'auto':
        cookies, error = extract_cookies_from_browser(args.browser)
        if error:
            print(f"错误: {error}")
            return
        global cookie_cache
        cookie_cache = cookies
        print(f"已从 {args.browser} 加载 Cookie: {len(cookies)} 项")
    else:
        cookies, browser_used, error = try_all_browsers()
        if error:
            print(f"警告: {error}")
        else:
            print(f"已从 {browser_used} 加载 Cookie: {len(cookies)} 项")
    
    ip = get_local_ip()
    print(f"\n服务启动: http://{ip}:{args.port}")
    print(f"设备端请求: http://{ip}:{args.port}/cookie")
    print(f"Web 界面:   http://{ip}:{args.port}/\n")
    
    app.run(host=args.host, port=args.port, debug=False, threaded=True)


if __name__ == '__main__':
    main()