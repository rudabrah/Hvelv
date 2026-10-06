@echo off
rem Lager installer\output\Hvelv-Setup-<versjon>.exe.
rem Forutsetter: build.bat (Release) er kjørt, installer\fetch_redist.ps1 er kjørt, og Inno Setup 6 er installert.
setlocal
set ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe
if not exist "%ISCC%" set ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe
if not exist "%ISCC%" (
    echo Fant ikke Inno Setup 6. Installer med: winget install JRSoftware.InnoSetup
    exit /b 1
)

rem Versjonen hentes fra project(... VERSION x.y.z) i CMakeLists.txt.
for /f "tokens=3" %%v in ('findstr /r /c:"^project(Hvelv VERSION" "%~dp0..\CMakeLists.txt"') do set VERSION=%%v

"%ISCC%" /Q /DAppVersion=%VERSION% "%~dp0hvelv.iss" || exit /b 1
echo Ferdig: %~dp0output\Hvelv-Setup-%VERSION%.exe
