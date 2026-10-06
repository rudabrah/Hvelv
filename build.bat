@echo off
rem Bygger Hvelv. Bruk: build.bat [Debug^|Release] [ekstra cmake-argumenter]
rem Bruker MSVC 2022 + Qt 6.8.1 hvis de finnes, ellers Qt 6.10.2 med MinGW.
rem Overstyr Qt-mappen med HVELV_QT (f.eks. C:\Qt\6.10.2\mingw_64).
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release
set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat

set QTDIR=%HVELV_QT%
if not "%QTDIR%"=="" goto :qtfound
set QTDIR=C:\Qt\6.10.2\mingw_64
if not exist "%VCVARS%" goto :qtfound
if exist "C:\Qt\6.8.1\msvc2022_64" set QTDIR=C:\Qt\6.8.1\msvc2022_64
:qtfound
if not exist "%QTDIR%\bin\qmake.exe" (
    echo Fant ikke Qt i %QTDIR%. Sett HVELV_QT til Qt-mappen.
    exit /b 1
)

rem Ingen ( )-blokker rundt PATH: "(x86)" i PATH avslutter blokken.
echo %QTDIR% | findstr /i mingw >nul
if errorlevel 1 goto :msvc
set "PATH=C:\Qt\Tools\mingw1310_64\bin;%PATH%"
goto :tools
:msvc
rem vcvars64 virker bare inne i en .bat-fil.
call "%VCVARS%" >nul || exit /b 1
:tools
set PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%QTDIR%\bin;%PATH%

cmake -S "%~dp0." -B "%~dp0build\%CONFIG%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_PREFIX_PATH=%QTDIR% %2 %3 %4 || exit /b 1
cmake --build "%~dp0build\%CONFIG%" || exit /b 1
ctest --test-dir "%~dp0build\%CONFIG%" --output-on-failure || exit /b 1
