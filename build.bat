@echo off
chcp 65001 >nul
echo ============================================
echo   FloatMon Build Script
echo ============================================
echo.

:: 优先使用项目自带的 MinGW
set "MINGW_DIR=%~dp0gcc_big\mingw64\bin"
if exist "%MINGW_DIR%\g++.exe" (
    set "PATH=%MINGW_DIR%;%PATH%"
    echo [信息] 使用项目自带 MinGW: %MINGW_DIR%
) else (
    echo [信息] 项目自带 MinGW 未找到，使用系统 PATH 中的 g++
)

where g++ >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [错误] 未找到 g++ 编译器!
    echo.
    echo 请下载 MinGW-w64（推荐 winlibs，解压到 gcc_big 目录）:
    echo   https://github.com/brechtsanders/winlibs_mingw/releases
    pause
    exit /b 1
)

echo [信息] 编译器版本:
g++ --version | findstr /C:"g++"
echo.

cd /d "%~dp0FloatMon"

echo [编译] 编译资源文件...
windres resource.rc resource.o
if %ERRORLEVEL% NEQ 0 (
    echo [失败] 资源编译出错
    pause
    exit /b 1
)

echo [编译] 编译 floatmon.cpp ...
g++ -o FloatMon.exe floatmon.cpp resource.o ^
    -lgdi32 -lgdiplus -luser32 -lshell32 -lpdh -liphlpapi -lole32 -ladvapi32 ^
    -mwindows -O2 -s -static -static-libgcc -static-libstdc++ -std=c++17

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [失败] 编译出错，请检查错误信息
    pause
    exit /b 1
)

echo.
echo ============================================
echo   [成功] FloatMon.exe 编译完成!
echo ============================================
echo.
echo 文件: %cd%\FloatMon.exe
for %%A in (FloatMon.exe) do echo 大小: %%~zA 字节
echo.
echo 双击 FloatMon.exe 即可运行（静态链接，无需依赖）
pause
