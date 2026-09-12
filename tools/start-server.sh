#!/usr/bin/env bash
# bilibilipan 一键启动 Cookie 同步服务 (Linux / macOS)
#
# 用法:
#   ./tools/start-server.sh         # 默认端口 9527
#   ./tools/start-server.sh 9000    # 自定义端口
#
# 启动后会打印本机局域网 IP, 浏览器打开 http://127.0.0.1:<端口> 粘贴 B 站 Cookie 即可.
# Ctrl+C 退出.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY_SCRIPT="$SCRIPT_DIR/pc-cookie-server.py"
PORT="${1:-9527}"

# 1) 找 python3
if command -v python3 >/dev/null 2>&1; then
  PY=python3
elif command -v python >/dev/null 2>&1; then
  PY=python
else
  echo "未找到 Python, 请先安装 Python 3 (https://www.python.org/)" >&2
  exit 1
fi

# 2) 校验目标脚本存在
if [ ! -f "$PY_SCRIPT" ]; then
  echo "找不到 $PY_SCRIPT" >&2
  exit 1
fi

# 3) 提示并启动
echo "==> 使用 Python: $($PY --version)"
echo "==> 启动 Cookie 同步服务, 端口 $PORT"
echo "==> 笔端获取地址: http://<本机局域网 IP>:$PORT/bilibilipan/cookie"
echo "==> Ctrl+C 退出"
echo ""
exec "$PY" "$PY_SCRIPT" "$PORT"