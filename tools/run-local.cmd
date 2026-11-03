@echo off
setlocal
if exist V:\ (
    echo V: is already in use. Choose a free drive letter in this script.
    exit /b 1
)
subst V: "%~dp0.."
if errorlevel 1 exit /b %errorlevel%
call :run %*
set "RUN_RESULT=%errorlevel%"
cd /d "%~dp0"
subst V: /d
exit /b %RUN_RESULT%

:run
set "PROJECT_ROOT=V:"
if not defined FIFA07_SHARE_CUBE_TEXTURES set "FIFA07_SHARE_CUBE_TEXTURES=1"
set "PATH=%PROJECT_ROOT%\tools\rexglue\win-amd64\bin;%PATH%"
cd /d "%PROJECT_ROOT%\port\out\build\win-release"
fifa07.exe --gpu_plugin=xenos --game_data_root="%PROJECT_ROOT%\game" --user_data_root="%PROJECT_ROOT%\port\userdata" --cache_root="%PROJECT_ROOT%\port\cache" %*
exit /b %errorlevel%
