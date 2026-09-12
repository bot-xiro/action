@echo off
chcp 936 >nul
setlocal
set "SCRIPT_DIR=%~dp0"
set "PY_SCRIPT=%SCRIPT_DIR%browser-login.py"
set "PORT=%1"
if "%PORT%"=="" set "PORT=9527"

echo ============================================================
echo   bilibilipan 浏览器登录
echo ============================================================
echo.
echo   会打开一个独立的浏览器窗口(临时用户目录), 请在里面登录 B 站。
echo   登录成功后自动取回 Cookie 并保存, 然后关闭这个临时窗口。
echo.
echo   不会影响你正在用的浏览器, 也不需要提前关闭它。
echo.
echo   登录完成后会自动启动同步服务, 供词典笔获取。
echo.
pause

rem ---- 1) 查找 Python ----
set "PY="
where python >nul 2>&1
if not errorlevel 1 set "PY=python"
if "%PY%"=="" (
    where py >nul 2>&1
    if not errorlevel 1 set "PY=py -3"
)
if "%PY%"=="" (
    echo [错误] 未找到 Python, 请先安装 Python 3 并勾选 Add to PATH
    pause
    exit /b 1
)

rem ---- 2) 校验脚本文件 ----
if not exist "%PY_SCRIPT%" (
    echo [错误] 找不到 browser-login.py
    echo       路径: %PY_SCRIPT%
    pause
    exit /b 1
)

rem ---- 3) 运行 ----
%PY% "%PY_SCRIPT%" %PORT%
echo.
echo 服务已退出.
pause
