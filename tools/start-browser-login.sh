#!/usr/bin/env bash
# bilibilipan 浏览器登录 —— 一键启动 (Linux / macOS)
#
# 默认打开一个独立的浏览器实例(临时用户目录), 在里面登录 B 站,
# 登录成功后自动取回 Cookie 并写入 bilibili-cookie.json。
# 加 --import 参数改为读取已有浏览器的登录态。
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY_SCRIPT="$SCRIPT_DIR/browser-login.py"
PORT="${1:-9527}"

if command -v python3 >/dev/null 2>&1; then PY=python3
elif command -v python >/dev/null 2>&1; then PY=python
else echo "未找到 Python, 请先安装 Python 3" >&2; exit 1; fi

if [ ! -f "$PY_SCRIPT" ]; then
  echo "找不到 $PY_SCRIPT" >&2; exit 1
fi

exec "$PY" "$PY_SCRIPT" "$PORT"
