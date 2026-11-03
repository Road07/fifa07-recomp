@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /O2 port\tests\memory_policy_test.cpp /Fologs\memory_policy_test.obj /Felogs\memory_policy_test.exe
if errorlevel 1 (
    popd
    exit /b 1
)
logs\memory_policy_test.exe
set "TEST_RESULT=%errorlevel%"
popd
exit /b %TEST_RESULT%
