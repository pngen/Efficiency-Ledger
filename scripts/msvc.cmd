@echo off
rem Discover a Visual Studio / Build Tools installation that has the C++ x64
rem tools via Microsoft's standard vswhere discovery, then invoke the x64
rem developer environment. No machine-specific absolute toolchain paths are
rem stored in this repository.
set "EL_VC_FOUND="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :novswhere
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
  call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
  set "EL_VC_FOUND=1"
)
if not defined EL_VC_FOUND goto :noinstall
%*
goto :eof
:novswhere
echo Error: vswhere.exe not found; install Visual Studio Build Tools. 1>&2
exit /b 1
:noinstall
echo Error: no Visual Studio installation with the C++ x64 build tools was found. 1>&2
exit /b 1
