# -*- coding: utf-8 -*-
# 给仓库内所有 actions/upload-artifact 步骤补 retention-days (默认 90 天留存是 0.5GB 配额的最大杀手)
# 用法:
#   py -3 tools/gh-workflow-retention.py <owner/repo> [--days 3] [--dry-run] [--backup DIR]
# 说明:
#   - 逐行最小改动: 只在上传步骤的 with: 块里插入一行 retention-days, 已存在的跳过.
#   - 改前把原文件存到 --backup 目录 (默认 ../ci-out/wf-backup/<repo>).
import sys, os, json, base64, time, urllib.request, urllib.error

ROOT = os.path.dirname(os.path.abspath(__file__))
TOKEN_FILE = os.path.normpath(os.path.join(ROOT, '..', '..', '.ghtok'))


def token():
    with open(TOKEN_FILE, 'r', encoding='utf-8') as f:
        return f.read().strip()


def api(path, method='GET', data=None, raw=False):
    body = json.dumps(data).encode('utf-8') if data is not None else None
    req = urllib.request.Request('https://api.github.com' + path, method=method, data=body, headers={
        'Authorization': 'Bearer ' + token(),
        'Accept': 'application/vnd.github.raw' if raw else 'application/vnd.github+json',
        'User-Agent': 'bilibilipan-ci',
        'Content-Type': 'application/json',
    })
    for i in range(4):
        try:
            r = urllib.request.build_opener().open(req, timeout=60)
            b = r.read()
            return b if raw else (json.loads(b) if b else {})
        except urllib.error.HTTPError as e:
            if e.code in (403, 429) and i < 3:
                time.sleep(5 * (i + 1))
                continue
            raise
    raise RuntimeError('api failed ' + path)


def patch(text, days):
    """在上传步骤的 with: 块里补一行 retention-days; 返回 (新文本, 改动条数)."""
    lines = text.split('\n')
    out, i, n = [], 0, 0
    while i < len(lines):
        line = lines[i]
        out.append(line)
        if 'uses: actions/upload-artifact' in line:
            ind = len(line) - len(line.lstrip())
            j = i + 1
            # 找同一个 step 里的 with: (步骤内缩进 > ind)
            with_at = None
            while j < len(lines):
                cur = lines[j]
                s = cur.strip()
                if not s:
                    j += 1
                    continue
                cind = len(cur) - len(cur.lstrip())
                if cind < ind or (s.startswith('- ') and cind <= ind):
                    break  # 下一个 step
                if s == 'with:':
                    with_at = j
                    break
                j += 1
            if with_at is not None:
                with_ind = len(lines[with_at]) - len(lines[with_at].lstrip())
                child_ind = with_ind + 2
                # 跟随 with 块第一个子项的缩进 (2/4 空格风格都兼容)
                m = with_at + 1
                while m < len(lines) and not lines[m].strip():
                    m += 1
                if m < len(lines):
                    mind = len(lines[m]) - len(lines[m].lstrip())
                    if mind > with_ind:
                        child_ind = mind
                # 该 with 块里已有 retention-days?
                k = with_at + 1
                has = False
                while k < len(lines):
                    c = lines[k]
                    cs = c.strip()
                    if not cs:
                        k += 1
                        continue
                    if (len(c) - len(c.lstrip())) <= ind:
                        break  # with: 块结束
                    if cs.startswith('retention-days'):
                        has = True
                        break
                    k += 1
                if not has:
                    lines.insert(with_at + 1, ' ' * child_ind + 'retention-days: %d' % days)
                    n += 1
        i += 1
    return '\n'.join(lines), n


def main():
    args = sys.argv[1:]
    repo = args[0] if args and not args[0].startswith('--') else None
    def opt(name, dflt):
        return args[args.index(name) + 1] if name in args else dflt
    days = int(opt('--days', 3))
    dry = '--dry-run' in args
    if not repo:
        print(__doc__ or 'usage: gh-workflow-retention.py owner/repo [--days 3] [--dry-run]')
        return
    backup = opt('--backup', os.path.normpath(os.path.join(ROOT, '..', '..', 'ci-out', 'wf-backup', repo.split('/')[-1])))
    listing = api('/repos/%s/contents/.github/workflows' % repo)
    total = 0
    for f in listing:
        if not f['name'].endswith(('.yml', '.yaml')):
            continue
        raw = api('/repos/%s/contents/.github/workflows/%s' % (repo, f['name']), raw=True).decode('utf-8')
        new, n = patch(raw, days)
        if n == 0:
            print('  %-22s 无需改动' % f['name'])
            continue
        total += n
        if dry:
            print('  %-22s 将插入 %d 处 retention-days: %d' % (f['name'], n, days))
            continue
        os.makedirs(backup, exist_ok=True)
        with open(os.path.join(backup, f['name']), 'w', encoding='utf-8', newline='') as fh:
            fh.write(raw)
        api('/repos/%s/contents/.github/workflows/%s' % (repo, f['name']), method='PUT', data={
            'message': 'ci: artifact retention-days=%d (0.5GB Actions 存储配额)' % days,
            'content': base64.b64encode(new.encode('utf-8')).decode('ascii'),
            'sha': f['sha'],
        })
        print('  %-22s 已提交 (%d 处)' % (f['name'], n))
    print('%s 共 %d 处%s' % ('[dry]' if dry else '[ok]', total, '' if not dry else ' 待插入'))


if __name__ == '__main__':
    main()
