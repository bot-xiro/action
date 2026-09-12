#!/usr/bin/env bash
# bilibilipan 一键扫码登录 (Linux / macOS)
#
# 用法:
#   ./tools/start-login.sh
#
# 流程:
#   1. 自动打开浏览器显示 B 站登录二维码
#   2. 用手机 B 站 App 扫码确认
#   3. Cookie 自动写入 tools/bilibili-cookie.json
#   4. 之后启动 tools/start-server.sh, 笔端 App 可拉取

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 找 python3
if command -v python3 >/dev/null 2>&1; then
  PY=python3
elif command -v python >/dev/null 2>&1; then
  PY=python
else
  echo "未找到 Python, 请先安装 Python 3 (https://www.python.org/)" >&2
  exit 1
fi

echo "==> 使用 Python: $($PY --version)"
echo "==> 启动扫码登录"
echo
exec "$PY" "$SCRIPT_DIR/qr-login.py"