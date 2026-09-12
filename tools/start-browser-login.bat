@echo off
chcp 936 >nul
setlocal
set "SCRIPT_DIR=%~dp0"
set "PY_SCRIPT=%SCRIPT_DIR%browser-login.py"
set "PORT=%1"
if "%PORT%"=="" set "PORT=9527"

echo ============================================================
echo   bilibilipan 浏览器登录态导入
echo ============================================================
echo.
echo   本工具会自动读取 Chrome / Edge 里已登录的 B 站账号,
echo   写入 bilibili-cookie.json, 然后启动同步服务供词典笔获取。
echo.
echo   注意: 读取前请完全退出 Chrome / Edge(含后台进程),
echo         否则浏览器会独占用户数据, 读不到登录态。
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
