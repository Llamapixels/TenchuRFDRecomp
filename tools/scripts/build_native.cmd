@echo off
setlocal
cd /d "%~dp0..\.."
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TENCHU_VS=%%i"
if not defined TENCHU_VS exit /b 1
call "%TENCHU_VS%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
set "TENCHU_LLVM=%CD%\tools\llvm\clang+llvm-23.1.4-x86_64-pc-windows-msvc\bin"
set "PATH=%TENCHU_LLVM%;%TENCHU_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
rem build logs: analysis\ when it is there, else build\
set "TENCHU_LOGS=analysis"
if not exist analysis set "TENCHU_LOGS=build"
if not exist "%TENCHU_LOGS%" mkdir "%TENCHU_LOGS%"
cmake -S . -B build/native -G Ninja "-DCMAKE_C_COMPILER=%TENCHU_LLVM%\clang.exe" "-DCMAKE_CXX_COMPILER=%TENCHU_LLVM%\clang++.exe" "-DCMAKE_AR=%TENCHU_LLVM%\llvm-ar.exe" "-DCMAKE_RANLIB=%TENCHU_LLVM%\llvm-ranlib.exe" -DCMAKE_BUILD_TYPE=Release -DTENCHU_BUILD_RUNTIME=ON > "%TENCHU_LOGS%\native_configure.log" 2>&1
if errorlevel 1 exit /b 1
if "%~1"=="configure" exit /b 0
cmake --build build/native --parallel 4 > "%TENCHU_LOGS%\native_build.log" 2>&1
exit /b %errorlevel%
