# -*- coding: utf-8 -*-
# GitHub Actions 存储占用统计 + 清理 (私有仓库的 artifact/日志计入 0.5GB 免费配额)
# 用法:
#   py -3 tools/gh-storage.py report
#       # 列出所有私有仓库的 artifact / 缓存占用与总量
#   py -3 tools/gh-storage.py prune [--keep 1] [--runs-days 7] [--runs-keep 3]
#                                   [--repo soarnext/bilibilipan] [--dry-run]
#       # 每个 artifact 名字只保留最新 keep 个; 删除 runs-days 天前且不在最新 runs-keep 个之内的 run (连带日志)
# 说明:
#   - 配额按账号统计, 私有仓库的 artifact + 日志 + 缓存都算; 公开仓库免费不计入.
#   - 只删旧产物/旧 run, 不动最新一次构建 (gh-ci.py download latest 仍然可用).
import sys, os, json, time, urllib.request, urllib.error

OWNER = 'soarnext'
ROOT = os.path.dirname(os.path.abspath(__file__))
TOKEN_FILE = os.path.normpath(os.path.join(ROOT, '..', '..', '.ghtok'))


def token():
    with open(TOKEN_FILE, 'r', encoding='utf-8') as f:
        return f.read().strip()


def api(path, method='GET', tries=4):
    last = None
    for i in range(tries):
        req = urllib.request.Request('https://api.github.com' + path, method=method, headers={
            'Authorization': 'Bearer ' + token(),
            'Accept': 'application/vnd.github+json',
            'User-Agent': 'bilibilipan-ci',
        })
        try:
            r = urllib.request.build_opener().open(req, timeout=60)
            body = r.read()
            return json.loads(body) if body else {}
        except urllib.error.HTTPError as e:
            if e.code in (403, 429) and i < tries - 1:
                last = e
                time.sleep(5 * (i + 1))
                continue
            raise
        except Exception as e:
            last = e
            time.sleep(3 * (i + 1))
    raise last


def paged(path):
    out, url = [], 'https://api.github.com' + path
    while url:
        req = urllib.request.Request(url, headers={
            'Authorization': 'Bearer ' + token(),
            'Accept': 'application/vnd.github+json',
            'User-Agent': 'bilibilipan-ci',
        })
        r = urllib.request.build_opener().open(req, timeout=60)
        data = json.loads(r.read() or b'{}')
        if isinstance(data, dict):
            for v in data.values():
                if isinstance(v, list):
                    out.extend(v)
        else:
            out.extend(data)
        link = r.headers.get('Link', '')
        url = None
        for part in link.split(','):
            if 'rel="next"' in part:
                url = part.split(';')[0].strip().strip('<>')
    return out


def private_repos():
    rs = [r for r in paged('/user/repos?per_page=100&affiliation=owner') if r.get('private')]
    return [r['full_name'] for r in sorted(rs, key=lambda x: x['full_name'])]


def repo_stat(repo):
    arts = paged('/repos/%s/actions/artifacts?per_page=100' % repo)
    cache = api('/repos/%s/actions/cache/usage' % repo)
    return arts, cache.get('active_caches_size_in_bytes', 0), cache.get('active_caches_count', 0)


def mb(n):
    return n / 1024.0 / 1024.0


def ts(s):
    """GitHub 时间串 -> epoch 秒 (容忍毫秒小数部分)."""
    s = s.strip().replace('Z', '')
    if '.' in s:
        s = s.split('.')[0]
    return time.mktime(time.strptime(s, '%Y-%m-%dT%H:%M:%S')) - time.timezone


def report(repos=None):
    repos = repos or private_repos()
    print('私有仓库 (计入 0.5GB 配额):')
    tot = 0
    for repo in repos:
        arts, cb, cc = repo_stat(repo)
        size = sum(a['size_in_bytes'] for a in arts)
        if not arts and not cc:
            continue
        tot += size + cb
        names = {}
        for a in arts:
            names[a['name']] = names.get(a['name'], 0) + 1
        print('  %-38s artifact=%-3d %8.1f MB  cache=%-2d %7.1f MB' % (
            repo, len(arts), mb(size), cc, mb(cb)))
        for n, c in sorted(names.items()):
            print('        - %-46s x%d' % (n, c))
    print('  合计: %.1f MB / 512 MB (配额 0.5 GB)' % mb(tot))
    return tot


def prune(keep_runs=2, keep=1, cache_days=7, runs_days=0, runs_keep=3, repos=None, dry=False):
    """keep_runs: 每个仓库保留最新 N 次 run 的全部产物; keep: 不属于任何 run 的产物按名字保留 N 个;
    cache_days: 删除 N 天未访问的缓存; runs_days>0 时删除 N 天前且不在最新 runs_keep 个之内的 run."""
    repos = repos or private_repos()
    freed = 0
    for repo in repos:
        arts, _, _ = repo_stat(repo)
        if arts:
            # 按 workflow_run.id 分组, 组按该组最新产物的时间排序
            groups = {}
            loose = []
            for a in arts:
                rid = (a.get('workflow_run') or {}).get('id')
                if rid is None:
                    loose.append(a)
                else:
                    groups.setdefault(rid, []).append(a)
            order = sorted(groups.values(), key=lambda g: max(x['created_at'] for x in g), reverse=True)
            victims = []
            for g in order[keep_runs:]:
                victims += g
            by_name = {}
            for a in loose:
                by_name.setdefault(a['name'], []).append(a)
            for name, lst in by_name.items():
                lst.sort(key=lambda a: a['created_at'], reverse=True)
                victims += lst[keep:]
            for a in victims:
                freed += a['size_in_bytes']
                print('%s delete artifact %s %s (%.1f MB)' % (
                    '[dry]' if dry else '[del]', a['id'], a['name'], mb(a['size_in_bytes'])))
                if not dry:
                    api('/repos/%s/actions/artifacts/%s' % (repo, a['id']), method='DELETE')
            print('    %s 产物: %d -> %d 个' % ('[dry]' if dry else '[ok]', len(arts), len(arts) - len(victims)))
        # 缓存
        if cache_days > 0:
            cut_c = time.time() - cache_days * 86400
            caches = paged('/repos/%s/actions/caches?per_page=100' % repo)
            for c in caches:
                if ts(c['last_accessed_at']) < cut_c:
                    freed += c['size_in_bytes']
                    print('%s delete cache %s %s (%.1f MB, 最后访问 %s)' % (
                        '[dry]' if dry else '[del]', c['id'], c['key'], mb(c['size_in_bytes']),
                        c['last_accessed_at'][:10]))
                    if not dry:
                        api('/repos/%s/actions/caches/%s' % (repo, c['id']), method='DELETE')
        # 旧 run (连带日志一起释放); 默认不动 run
        if runs_days > 0:
            runs = paged('/repos/%s/actions/runs?per_page=100' % repo)
            runs.sort(key=lambda r: r['created_at'], reverse=True)
            cut = time.time() - runs_days * 86400
            for r in runs[runs_keep:]:
                if ts(r['created_at']) < cut:
                    print('%s delete run %s %s (%s)' % (
                        '[dry]' if dry else '[del]', r['id'], r['head_sha'][:7], r['created_at']))
                    if not dry:
                        api('/repos/%s/actions/runs/%s' % (repo, r['id']), method='DELETE')
    print('%s 释放约 %.1f MB' % ('[dry] 预计' if dry else '已', mb(freed)))


if __name__ == '__main__':
    args = sys.argv[1:]
    cmd = args[0] if args else 'report'

    def opt(name, dflt):
        if name in args:
            return args[args.index(name) + 1]
        return dflt

    repo = opt('--repo', None)
    only = [repo] if repo else None
    if cmd == 'report':
        report(only)
    elif cmd == 'prune':
        prune(keep_runs=int(opt('--keep-runs', 2)), keep=int(opt('--keep', 1)),
              cache_days=int(opt('--cache-days', 7)), runs_days=int(opt('--runs-days', 0)),
              runs_keep=int(opt('--runs-keep', 3)), repos=only, dry=('--dry-run' in args))
    else:
        print(__doc__ or 'usage: report | prune')
