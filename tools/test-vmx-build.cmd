@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
set "PATH=V:\tools\llvm-20\bin;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
cmake -S V:/tools/vmx_harness -B V:/logs/vmx/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_PREFIX_PATH=V:/tools/rexglue/win-amd64
if errorlevel 1 exit /b %errorlevel%
cmake --build V:/logs/vmx/build --parallel 4
if errorlevel 1 exit /b %errorlevel%
V:\logs\vmx\build\vmx_regression.exe
exit /b %errorlevel%
