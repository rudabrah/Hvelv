@echo off
rem Bygger Hvelv med MSVC 2022 + Qt 6.8.1. Bruk: build.bat [Debug^|Release] [ekstra cmake-argumenter]
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release
set QTDIR=C:\Qt\6.8.1\msvc2022_64

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%QTDIR%\bin;%PATH%

cmake -S "%~dp0." -B "%~dp0build\%CONFIG%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_PREFIX_PATH=%QTDIR% %2 %3 %4 || exit /b 1
cmake --build "%~dp0build\%CONFIG%" || exit /b 1
ctest --test-dir "%~dp0build\%CONFIG%" --output-on-failure || exit /b 1
