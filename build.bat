@echo off
rem ===========================================================================
rem  Build PDF / Word Page Exporter (native Win32, MSVC)
rem
rem  Usage:  build.bat          -> 64-bit, output to bin\
rem          build.bat x86      -> 32-bit, output to bin\x86\
rem
rem  ASCII only: cmd.exe reads .bat files in the OEM codepage.
rem  Requires: Visual Studio 2019/2022 with the C++ workload + Windows SDK.
rem  Optional: set VCVARS to your vcvars64.bat / vcvars32.bat if auto-detect fails.
rem ===========================================================================
setlocal enabledelayedexpansion
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"

set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=x64"
if /i "%ARCH%"=="x86" goto SET_X86
set "VCVARS_NAME=vcvars64.bat"
set "OUTDIR=%ROOT%\bin"
goto FIND_VCVARS

:SET_X86
set "VCVARS_NAME=vcvars32.bat"
set "OUTDIR=%ROOT%\bin\x86"
goto FIND_VCVARS

:FIND_VCVARS
if defined VCVARS goto HAVE_VCVARS
for %%E in (Community Professional Enterprise BuildTools) do (
  if not defined VCVARS (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\!VCVARS_NAME!" (
      set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\!VCVARS_NAME!"
    )
  )
  if not defined VCVARS (
    if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\%%E\VC\Auxiliary\Build\!VCVARS_NAME!" (
      set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\%%E\VC\Auxiliary\Build\!VCVARS_NAME!"
    )
  )
)

:HAVE_VCVARS
if not defined VCVARS (
  echo [ERROR] !VCVARS_NAME! not found. Install Visual Studio with the C++ workload,
  echo         or set VCVARS to its full path and run again.
  exit /b 1
)

echo Arch: !ARCH!  ^|  using: !VCVARS!
call "!VCVARS!" >nul
if errorlevel 1 ( echo [ERROR] vcvars failed & exit /b 1 )

cd /d "%ROOT%"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

rem Compile the version resource explicitly; cl does not always route .rc to rc.exe.
where rc.exe >nul 2>nul
if errorlevel 1 ( echo [ERROR] rc.exe not found in PATH ^(run from a VS developer prompt^) & exit /b 1 )
rc /nologo /fo "%ROOT%\src\version.res" "%ROOT%\src\version.rc"
if errorlevel 1 ( echo [ERROR] resource compile failed & exit /b 1 )

rem /MT = static CRT, so the exe needs no VC++ redistributable.
cl /nologo /EHsc /O2 /MT /W3 /utf-8 /DUNICODE /D_UNICODE ^
   /I"%ROOT%\include" ^
   "%ROOT%\src\core.cpp" "%ROOT%\src\main.cpp" "%ROOT%\src\version.res" ^
   /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /OPT:REF /OPT:ICF ^
   user32.lib gdi32.lib gdiplus.lib comdlg32.lib shell32.lib ole32.lib comctl32.lib ^
   /OUT:"%OUTDIR%\PDFPageExporter.exe"
if errorlevel 1 ( echo [ERROR] build failed & exit /b 1 )

del /q "%ROOT%\*.obj" 2>nul
del /q "%ROOT%\src\*.obj" 2>nul
del /q "%ROOT%\*.res" 2>nul
del /q "%ROOT%\src\*.res" 2>nul
echo.
echo Build OK: %OUTDIR%\PDFPageExporter.exe
for %%F in ("%OUTDIR%\PDFPageExporter.exe") do echo Size: %%~zF bytes
echo Note: pdfium.dll must sit next to the exe. Make sure it matches the architecture.
exit /b 0
