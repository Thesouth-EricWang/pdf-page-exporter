@echo off
rem ===========================================================================
rem  Build PDF / Word Page Exporter (native Win32, MSVC)
rem  ASCII only: cmd.exe reads .bat files in the OEM codepage.
rem
rem  Requires: Visual Studio 2019/2022 with the C++ workload + Windows SDK.
rem  Optional: set VCVARS to your vcvars64.bat if auto-detection fails.
rem ===========================================================================
setlocal enabledelayedexpansion
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"

if not defined VCVARS (
  for %%E in (Community Professional Enterprise BuildTools) do (
    if not defined VCVARS (
      if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
        set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
      )
    )
    if not defined VCVARS (
      if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\%%E\VC\Auxiliary\Build\vcvars64.bat" (
        set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\%%E\VC\Auxiliary\Build\vcvars64.bat"
      )
    )
  )
)

if not defined VCVARS (
  echo [ERROR] vcvars64.bat not found. Install Visual Studio with the C++ workload,
  echo         or set VCVARS to its full path and run again.
  exit /b 1
)

echo Using: !VCVARS!
call "!VCVARS!" >nul
if errorlevel 1 ( echo [ERROR] vcvars64 failed & exit /b 1 )

cd /d "%ROOT%"
if not exist "%ROOT%\bin" mkdir "%ROOT%\bin"

rem /MT = static CRT, so the exe needs no VC++ redistributable.
cl /nologo /EHsc /O2 /MT /W3 /utf-8 /DUNICODE /D_UNICODE ^
   /I"%ROOT%\include" ^
   "%ROOT%\src\core.cpp" "%ROOT%\src\main.cpp" ^
   /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /OPT:REF /OPT:ICF ^
   user32.lib gdi32.lib gdiplus.lib comdlg32.lib shell32.lib ole32.lib comctl32.lib ^
   /OUT:"%ROOT%\bin\PDFPageExporter.exe"
if errorlevel 1 ( echo [ERROR] build failed & exit /b 1 )

del /q "%ROOT%\*.obj" 2>nul
echo.
echo Build OK: %ROOT%\bin\PDFPageExporter.exe
for %%F in ("%ROOT%\bin\PDFPageExporter.exe") do echo Size: %%~zF bytes
exit /b 0
