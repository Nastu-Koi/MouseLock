@echo off
setlocal
cd /d "%~dp0"

rem Reuse an initialized developer prompt; otherwise let vswhere find the installed
rem Visual Studio edition, falling back to the default 2022 Build Tools path.
where cl >nul 2>nul
if not errorlevel 1 goto :build
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" goto :novs
call "%VCVARS%" >nul
where cl >nul 2>nul
if errorlevel 1 goto :novs

:build
rc /nologo /fo resources.res resources.rc
if errorlevel 1 goto :err

cl /nologo /O2 /utf-8 /EHsc /MT /std:c++17 /DUNICODE /D_UNICODE /DNOMINMAX ^
  config.cpp locker.cpp theme.cpp notifications.cpp settings.cpp main.cpp ^
  /Fe:MouseLockGame.exe ^
  /link /SUBSYSTEM:WINDOWS /MANIFEST:NO resources.res ^
  user32.lib shell32.lib advapi32.lib comctl32.lib gdi32.lib gdiplus.lib dwmapi.lib ole32.lib uuid.lib
if errorlevel 1 goto :err

cl /nologo /O2 /utf-8 /EHsc /MT /std:c++17 /DUNICODE /D_UNICODE /DNOMINMAX ^
  tests\config_test.cpp config.cpp /Fe:config-test.exe /link user32.lib
if errorlevel 1 goto :err

echo Build OK: MouseLockGame.exe, config-test.exe
exit /b 0

:novs
echo Visual Studio C++ build tools not found. Install them or run this from a developer prompt.
:err
echo Build FAILED
exit /b 1
