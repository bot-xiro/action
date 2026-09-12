#!/usr/bin/env bash
# bilibilipan 浏览器登录态导入 —— 一键启动 (Linux / macOS)
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
