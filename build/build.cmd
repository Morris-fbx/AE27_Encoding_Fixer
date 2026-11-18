@echo off
rem ===========================================================================
rem  Build !00_AE27CharsetFixer.aex  (x64)
rem
rem  Requirements:
rem    AE_SDK        After Effects SDK       (Headers\, Util\, Resources\)
rem    MINHOOK_DIR   MinHook source tree      (include\MinHook.h, src\*.c)
rem    MSVC          Visual Studio 2019+ (x64 toolchain)
rem
rem  Optional environment overrides: VCVARS (full path to vcvars64.bat),
rem  BUILD (scratch dir; must be space free - PiPLtool cannot handle spaces).
rem
rem  Usage:  build.cmd [output-dir]
rem ===========================================================================
setlocal EnableExtensions
set "ROOT=%~dp0.."
cd /d "%ROOT%"

if not defined AE_SDK      set "AE_SDK=%ROOT%\third_party\ae_sdk"
if not defined MINHOOK_DIR set "MINHOOK_DIR=%ROOT%\third_party\minhook"
set "OUTDIR=%~1"
if not defined OUTDIR set "OUTDIR=%ROOT%\dist"
if not defined BUILD set "BUILD=%TEMP%\ae27_charset_fixer"

if not exist "%AE_SDK%\Headers\AE_Effect.h" (
    echo [error] AE_SDK not found: "%AE_SDK%"
    echo         Install the After Effects SDK and set AE_SDK, or extract it to third_party\ae_sdk
    goto :fail
)
if not exist "%MINHOOK_DIR%\include\MinHook.h" (
    echo [error] MinHook not found: "%MINHOOK_DIR%"
    echo         git clone https://github.com/TsudaKageyu/minhook third_party\minhook
    goto :fail
)

rem ---- MSVC environment -------------------------------------------------
if not defined VCVARS (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "%VSWHERE%" (
        for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
    )
    if defined VSINSTALL set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS (
    echo [error] vcvars64.bat not found. Set VCVARS to its full path.
    goto :fail
)
call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
    echo [error] failed to initialise the MSVC environment
    goto :fail
)

if not exist "%BUILD%" mkdir "%BUILD%"
set "INCLUDES=/I "%AE_SDK%\Headers" /I "%AE_SDK%\Headers\SP" /I "%AE_SDK%\Util" /I "%AE_SDK%\Resources" /I "%MINHOOK_DIR%\include""

rem ---- 1) MinHook built with the static CRT (/MT) ----------------------
set "MINHOOK_LIB=%BUILD%\minhook_mt.lib"
if not exist "%MINHOOK_LIB%" (
    echo [1/5] MinHook ^(/MT^)
    if not exist "%BUILD%\mh" mkdir "%BUILD%\mh"
    pushd "%MINHOOK_DIR%\src"
    cl /nologo /c /O2 /MT /D_CRT_SECURE_NO_WARNINGS *.c hde\*.c /Fo"%BUILD%\mh\\" >nul || (popd & goto :fail)
    popd
    lib /nologo /OUT:"%MINHOOK_LIB%" "%BUILD%\mh\*.obj" >nul || goto :fail
    del /q "%BUILD%\mh\*.obj" >nul 2>&1
)

rem ---- 2) PiPL resource (PiPLtool needs a space-free working directory) -
echo [2/5] PiPL
copy /y "%ROOT%\src\FixerPiPL.r" "%BUILD%\pipl_in.r" >nul || goto :fail
pushd "%BUILD%"
cl /nologo /utf-8 /EP /D MSWindows %INCLUDES% pipl_in.r > pipl.rr || (popd & goto :fail)
"%AE_SDK%\Resources\PiPLtool.exe" pipl.rr pipl.rrc || (popd & goto :fail)
cl /nologo /utf-8 /EP /D MSWindows pipl.rrc > pipl.rc || (popd & goto :fail)
rc /nologo /fo pipl.res pipl.rc || (popd & goto :fail)
popd

rem ---- 3) compile ------------------------------------------------------
echo [3/5] compile
cl /nologo /c /std:c++20 /EHsc /O2 /MT /W4 /permissive- ^
   /D_CRT_SECURE_NO_WARNINGS /DMSWindows /DWIN32 /D_WINDOWS ^
   %INCLUDES% "%ROOT%\src\FixerANSI.cpp" /Fo"%BUILD%\fixer.obj" || goto :fail

rem ---- 4) link ---------------------------------------------------------
echo [4/5] link
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
link /nologo /DLL /MACHINE:X64 /OPT:REF /OPT:ICF ^
     /OUT:"%OUTDIR%\!00_AE27CharsetFixer.aex" /EXPORT:EntryPointFunc ^
     "%BUILD%\fixer.obj" "%BUILD%\pipl.res" "%MINHOOK_LIB%" ^
     user32.lib kernel32.lib || goto :fail

echo [5/5] built:
dir /b "%OUTDIR%\!00_AE27CharsetFixer.aex"
endlocal
exit /b 0

:fail
echo BUILD FAILED
endlocal
exit /b 1


