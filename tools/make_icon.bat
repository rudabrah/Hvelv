@echo off
rem Lager assets\hvelv.ico og assets\icon-*.png på nytt fra tools\make_icon.cpp.
setlocal
set QTDIR=C:\Qt\6.8.1\msvc2022_64
set ROOT=%~dp0..

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%QTDIR%\bin;%PATH%

cmake -S "%ROOT%" -B "%ROOT%\build\tools" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%QTDIR% -DHVELV_BUILD_APP=OFF >nul || exit /b 1
cmake --build "%ROOT%\build\tools" --target hvelv_make_icon || exit /b 1
"%ROOT%\build\tools\hvelv_make_icon.exe" "%ROOT%\assets" || exit /b 1
echo Ikon generert i %ROOT%\assets
