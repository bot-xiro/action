@echo off
chcp 936 >nul
setlocal

rem ============================================================
rem  bilibilipan 一键扫码登录 (Windows)
rem
rem  流程: 浏览器打开二维码 -> 手机扫码 -> Cookie 自动写入
rem  之后: 运行 start-server.bat, 笔端 App 可拉取 Cookie
rem ============================================================

set "SCRIPT_DIR=%~dp0"

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

echo.
echo ============================================================
echo   bilibilipan 一键扫码登录
echo ============================================================
echo   1. 浏览器会自动打开 B 站登录二维码
echo   2. 用手机 B 站 App 扫码, 在手机上点确认登录
echo   3. 这里会等待, 成功后 Cookie 自动保存
echo ============================================================
echo.

%PY% "%SCRIPT_DIR%qr-login.py"

echo.
pause
