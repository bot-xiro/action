# -*- coding: utf-8 -*-
# 真机验证辅助 (词典笔 9E11700007500215)
#   dev.py shot [name]          唤醒屏幕 + 截图并拉回 ci-out/
#   dev.py tap <dx> <dy>        按「显示坐标」点击 (自动换算触控坐标)
#   dev.py tapraw <x> <y>       直接按触控坐标点击
#   dev.py swipe <dx1> <dy1> <dx2> <dy2> [ms]   显示坐标滑动 (左右滑切 tab / 下拉刷新用)
#   dev.py key <name>           返回键等: back / home
#   dev.py log [n]              最近 n 行运行日志 (默认 60)
#   dev.py start [page]         启动应用 (可指定页面)
#   dev.py install <amr>        安装 amr
# 坐标换算 (HANDOVER §3): touchX = displayY + 107, touchY = 959 - displayX
import sys, os, subprocess, time

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
ADB = os.path.join(ROOT, 'platform-tools', 'adb.exe')
OUT = os.path.join(ROOT, 'ci-out')
REMOTE = '/userdata/local/tmp'


def run(args, timeout=60):
    env = dict(os.environ, MSYS_NO_PATHCONV='1')
    p = subprocess.run([ADB] + args, capture_output=True, text=True, timeout=timeout, env=env)
    return (p.stdout or '') + (p.stderr or '')


def sh(cmd, timeout=60):
    return run(['shell', cmd], timeout)


def to_touch(dx, dy):
    """显示坐标 (960x266) -> 触控坐标 (480x960): touchX = displayY + 107, touchY = 959 - displayX"""
    return int(dy) + 107, 959 - int(dx)


def wake():
    # 用 slip 唤醒: 无点击副作用 (press/release 会点到屏幕上的按钮,
    # HANDOVER 里的 (143,121) 正好落在「我的」tab 上, 会误切 tab)
    sh('send_event touch slip 240 479')
    time.sleep(0.7)


def shot(name='dev', tapx=None, tapy=None):
    """截图. 屏幕睡着时 capture 返回的是最后一帧旧画面 (不是黑帧!),
    所以先用 press/release 唤醒并强制重绘 —— 坐标必须选当前页面的无害位置
    (如当前激活 tab / 标题栏空白), 否则会误触发按钮."""
    os.makedirs(OUT, exist_ok=True)
    if tapx is not None and tapy is not None:
        tap(int(tapx), int(tapy))
    else:
        sh('send_event touch slip 240 479')
    time.sleep(1.2)
    sh('miniapp_cli capture %s/%s.png' % (REMOTE, name))
    dst = os.path.join(OUT, name + '.png')
    run(['pull', '%s/%s.png' % (REMOTE, name), dst])
    size = os.path.getsize(dst) if os.path.exists(dst) else 0
    print('shot %s %d bytes%s' % (dst, size, '  (纯黑=息屏)' if size < 3000 else ''))
    return dst


def tap(dx, dy):
    x, y = to_touch(dx, dy)
    sh('send_event touch press %d %d' % (x, y))
    time.sleep(0.12)
    sh('send_event touch release %d %d' % (x, y))
    print('tap display(%s,%s) -> touch(%d,%d)' % (dx, dy, x, y))


def swipe(dx1, dy1, dx2, dy2, ms=400):
    """用多个 slip 事件模拟滑动 (send_event 无手势合成, 只能插值发点).
    步长 ~20px, 总时长默认 400ms —— 太快的合成滑动可能被识别成点击或丢失."""
    x1, y1 = to_touch(dx1, dy1)
    x2, y2 = to_touch(dx2, dy2)
    dist = max(abs(x2 - x1), abs(y2 - y1))
    steps = max(6, min(24, dist // 20))
    sh('send_event touch press %d %d' % (x1, y1))
    for i in range(1, steps + 1):
        x = x1 + (x2 - x1) * i // steps
        y = y1 + (y2 - y1) * i // steps
        sh('send_event touch slip %d %d' % (x, y))
        time.sleep(ms / 1000.0 / steps)
    sh('send_event touch release %d %d' % (x2, y2))
    print('swipe display(%s,%s)->(%s,%s) steps=%d' % (dx1, dy1, dx2, dy2, steps))


def keepalive(seconds=600):
    """后台保活: 每 4s 注入 slip (无点击副作用), 让屏幕在验证期间不睡"""
    import time as _t
    end = _t.time() + seconds
    while _t.time() < end:
        sh('send_event touch slip 240 479')
        _t.sleep(4)
    print('keepalive done')


def tapshot(name, ax, ay, wx, wy, wait=4.0):
    """动作点击 + 保活等待 + 截图.
    息屏极快 (几秒), 点击后必须持续注入 slip 保持亮屏, 否则 capture 拿到旧帧."""
    tap(int(ax), int(ay))
    end = time.time() + float(wait)
    while time.time() < end:
        sh('send_event touch slip 240 479')
        time.sleep(1.0)
    # 用无害位置唤醒+强制重绘后截图
    tap(int(wx), int(wy))
    time.sleep(0.8)
    sh('miniapp_cli capture %s/%s.png' % (REMOTE, name))
    dst = os.path.join(OUT, name + '.png')
    run(['pull', '%s/%s.png' % (REMOTE, name), dst])
    print('tapshot %s %d bytes' % (dst, os.path.getsize(dst) if os.path.exists(dst) else 0))
    return dst


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'shot'
    a = sys.argv[2:]
    if cmd == 'keepalive':
        keepalive(int(a[0]) if a else 600)
    elif cmd == 'cap':
        # 纯截图 (不注入任何事件), 供精确时序验证用
        nm = a[0] if a else 'cap'
        os.makedirs(OUT, exist_ok=True)
        sh('miniapp_cli capture %s/%s.png' % (REMOTE, nm))
        dst = os.path.join(OUT, nm + '.png')
        run(['pull', '%s/%s.png' % (REMOTE, nm), dst])
        print('cap %s %d bytes' % (dst, os.path.getsize(dst) if os.path.exists(dst) else 0))
    elif cmd == 'tapshot':
        # tapshot <name> <ax> <ay> <wx> <wy> [wait]
        tapshot(a[0], a[1], a[2], a[3], a[4], a[5] if len(a) > 5 else 4.0)
    elif cmd == 'shot':
        # shot <name> [tapX tapY]  —— 可选: 截图前先点一下安全位置唤醒+重绘
        nm = a[0] if a else 'dev'
        tx = a[1] if len(a) > 1 else None
        ty = a[2] if len(a) > 2 else None
        shot(nm, tx, ty)
    elif cmd == 'tap':
        tap(a[0], a[1])
    elif cmd == 'tapraw':
        sh('send_event touch press %s %s' % (a[0], a[1]))
        time.sleep(0.12)
        sh('send_event touch release %s %s' % (a[0], a[1]))
    elif cmd == 'swipe':
        swipe(a[0], a[1], a[2], a[3], int(a[4]) if len(a) > 4 else 250)
    elif cmd == 'key':
        if a and a[0] == 'back':
            sh('send_event menu press')
            time.sleep(0.1)
            sh('send_event menu release')
        elif a and a[0] == 'home':
            sh('send_event camera press')
            time.sleep(0.1)
            sh('send_event camera release')
        else:
            print('key: back | home')
    elif cmd == 'log':
        n = int(a[0]) if a else 60
        print(sh('tail -n %d /userdisk/xiro/bilibili.log' % n))
    elif cmd == 'start':
        print(sh('miniapp_cli start 8001812345678901' + ((' --' + a[0]) if a else '')))
    elif cmd == 'install':
        print(sh('miniapp_cli install ' + a[0]))
    else:
        print(__doc__)
