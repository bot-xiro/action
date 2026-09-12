@echo off
rem bilibilipan 一键启动 Cookie 同步服务 (Windows)
rem
rem 用法:
rem   tools\start-server.bat         默认端口 9527
rem   tools\start-server.bat 9000    自定义端口
rem
rem 启动后会打印本机局域网 IP, 浏览器打开 http://127.0.0.1:<端口> 粘贴 B 站 Cookie 即可.
rem Ctrl+C 退出.

setlocal

set "SCRIPT_DIR=%~dp0"
set "PY_SCRIPT=%SCRIPT_DIR%pc-cookie-server.py"
set "PORT=%1"
if "%PORT%"=="" set "PORT=9527"

rem 1) 找 Python
set "PY="
where python  >nul 2>&1 && set "PY=python"
if "%PY%"=="" (
  where py >nul 2>&1 && set "PY=py -3"
)
if "%PY%"=="" (
  echo 未找到 Python, 请先安装 Python 3 (https://www.python.org/) 1>&2
  exit /b 1
)

rem 2) 校验目标脚本存在
if not exist "%PY_SCRIPT%" (
  echo 找不到 %PY_SCRIPT% 1>&2
  exit /b 1
)

echo ==^> 启动 Cookie 同步服务, 端口 %PORT%
echo ==^> 笔端获取地址: http://^<本机局域网 IP^>:%PORT%/bilibilipan/cookie
echo ==^> Ctrl+C 退出
echo.

rem 3) 启动 (用 call 让 ^C 能干净退出)
%PY% "%PY_SCRIPT%" %PORT%