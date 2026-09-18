# -*- coding: utf-8 -*-
# CI 构建状态查询 + 产物下载 (私有仓库需 token; 走本机可用代理 41499)
# 用法:
#   gh-ci.py status                查询 miniapp 最新 run 状态
#   gh-ci.py watch                 轮询直到完成
#   gh-ci.py download [latest|ID] [dest]   下载 run 产物 zip
import sys, os, json, time, urllib.request

OWNER = 'soarnext'
REPO = 'bilibilipan'
BRANCH = 'miniapp'
PROXY = 'http://127.0.0.1:10808'
ROOT = os.path.dirname(os.path.abspath(__file__))
TOKEN_FILE = os.path.normpath(os.path.join(ROOT, '..', '..', '.ghtok'))


def token():
    with open(TOKEN_FILE, 'r', encoding='utf-8') as f:
        return f.read().strip()


def opener(use_proxy=False):
    # 2026-09-13 实测: 41499 代理未运行, 直连 GitHub 可用 (git push 同路径).
    # 保留 PROXY 常量: 代理恢复后把默认值改 True 即可.
    if use_proxy:
        return urllib.request.build_opener(
            urllib.request.ProxyHandler({'http': PROXY, 'https': PROXY}))
    return urllib.request.build_opener()


def api(path, tries=4):
    last = None
    for i in range(tries):
        req = urllib.request.Request('https://api.github.com' + path, headers={
            'Authorization': 'Bearer ' + token(),
            'Accept': 'application/vnd.github+json',
            'User-Agent': 'bilibilipan-ci',
        })
        try:
            return json.load(opener().open(req, timeout=60))
        except Exception as e:
            last = e
            time.sleep(3 * (i + 1))
    raise last


def latest_run():
    runs = api('/repos/%s/%s/actions/runs?branch=%s&per_page=1' % (OWNER, REPO, BRANCH))
    return runs['workflow_runs'][0] if runs.get('workflow_runs') else None


def status():
    r = latest_run()
    if not r:
        print('NO RUN on', BRANCH)
        return None
    print('run %s status=%s conclusion=%s head=%s' % (
        r['id'], r['status'], r['conclusion'], r['head_sha'][:7]))
    for j in api(r['jobs_url']).get('jobs', []):
        print('  job %s: %s %s' % (j['name'], j['status'], j['conclusion']))
    return r


def watch():
    while True:
        r = status()
        if r and r['status'] == 'completed':
            print('DONE conclusion=%s' % r['conclusion'])
            return r
        time.sleep(20)


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def download(run_id, dest):
    if run_id in ('latest', None):
        run_id = str(latest_run()['id'])
    art = api('/repos/%s/%s/actions/runs/%s/artifacts' % (OWNER, REPO, run_id))
    os.makedirs(dest, exist_ok=True)
    for a in art.get('artifacts', []):
        print('artifact %s (%d bytes)' % (a['name'], a['size_in_bytes']))
        req = urllib.request.Request(a['archive_download_url'], headers={
            'Authorization': 'Bearer ' + token(),
            'User-Agent': 'bilibilipan-ci',
        })
        # 1) 带 auth + 不跟随重定向, 拿 blob Location
        op1 = urllib.request.build_opener(NoRedirect)
        loc = None
        try:
            op1.open(req, timeout=60)
            # 未重定向: 直接读
            data = op1.open(req, timeout=300).read()
        except urllib.error.HTTPError as e:
            loc = e.headers.get('Location')
            if not loc:
                raise
        if loc:
            # 2) blob 存储直连被拒 (objects.githubusercontent.com), 走代理; 必须去 auth
            data = None
            last = None
            for i in range(5):
                try:
                    op2 = urllib.request.build_opener(
                        urllib.request.ProxyHandler({'http': PROXY, 'https': PROXY}))
                    r = op2.open(urllib.request.Request(loc, headers={'User-Agent': 'bilibilipan-ci'}), timeout=300)
                    data = r.read()
                    break
                except Exception as e2:
                    last = e2
                    time.sleep(3 * (i + 1))
            if data is None:
                raise last
        fn = os.path.join(dest, a['name'] + '.zip')
        with open(fn, 'wb') as f:
            f.write(data)
        print('  saved', fn, len(data))


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'status'
    if cmd == 'status':
        status()
    elif cmd == 'watch':
        watch()
    elif cmd == 'download':
        run_id = sys.argv[2] if len(sys.argv) > 2 else 'latest'
        dest = sys.argv[3] if len(sys.argv) > 3 else os.path.normpath(
            os.path.join(ROOT, '..', '..', 'ci-out'))
        download(run_id, dest)
