@echo off
chcp 936 >nul
setlocal

rem ============================================================
rem  bilibilipan 登录 / Cookie 同步服务 - 一键启动 (Windows)
rem
rem  用法:
rem    tools\start-server.bat          默认端口 9527
rem    tools\start-server.bat 9000     自定义端口
rem
rem  启动后会自动打开登录页面 http://127.0.0.1:<端口>
rem    方式一: 点「从 Chrome / Edge 读取登录态」自动获取(需先退出浏览器)
rem    方式二: 粘贴 B 站 Cookie(页面会自动检测剪贴板)
rem
rem  只想从浏览器自动读取, 可以用 start-browser-login.bat
rem ============================================================

set "SCRIPT_DIR=%~dp0"
set "PY_SCRIPT=%SCRIPT_DIR%pc-cookie-server.py"
set "PORT=%1"
if "%PORT%"=="" set "PORT=9527"

rem ---- 1) 查找 Python ----
set "PY="
where python >nul 2>&1
if not errorlevel 1 set "PY=python"

if "%PY%"=="" (
    where py >nul 2>&1
    if not errorlevel 1 set "PY=py -3"
)

if "%PY%"=="" (
    echo.
    echo [错误] 未找到 Python, 请先安装 Python 3
    echo       下载地址: https://www.python.org/downloads/
    echo.
    pause
    exit /b 1
)

rem ---- 2) 校验脚本文件 ----
if not exist "%PY_SCRIPT%" (
    echo.
    echo [错误] 找不到脚本: %PY_SCRIPT%
    echo.
    pause
    exit /b 1
)

rem ---- 3) 启动服务 ----
echo.
echo ============================================================
echo   bilibilipan 登录 / Cookie 同步服务
echo ============================================================
echo   端口             : %PORT%
echo   登录页面         : http://127.0.0.1:%PORT%
echo   (会自动打开浏览器, 按 Ctrl+C 退出)
echo ============================================================
echo.

%PY% "%PY_SCRIPT%" %PORT%

echo.
echo 服务已退出.
pause
