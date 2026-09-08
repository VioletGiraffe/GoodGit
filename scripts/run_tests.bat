@echo off
setlocal

:: Builds tests\tests.pro and runs it. Exit code: the test run's, or 2 when the environment is incomplete.
:: The Qt kit comes from qt_kit.bat (QT_ROOT_DIR); the MSVC environment is set up through vswhere unless cl
:: is already on PATH, as it is on CI. An argument of "debug" builds and runs the debug configuration, for
:: debug_tests.bat.

set "CONFIG=release"
if /i "%~1"=="debug" set "CONFIG=debug"

call "%~dp0qt_kit.bat" || exit /b 2

set "VSINSTALLER=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
where cl >nul 2>nul || call :vcvars || exit /b 2

pushd "%~dp0..\tests" || exit /b 2
"%QT_ROOT_DIR%\bin\qmake.exe" tests.pro CONFIG+=%CONFIG% || goto :fail
nmake /nologo || goto :fail
popd
"%~dp0..\tests\bin\%CONFIG%\tests.exe"
exit /b %errorlevel%

:fail
popd
exit /b 1

:vcvars
for /f "usebackq delims=" %%p in (`"%VSINSTALLER%\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%p"
if not defined VSPATH (
	echo No Visual Studio with the C++ toolset was found.
	exit /b 1
)
:: vcvars64.bat itself calls a bare vswhere.exe, which it expects on PATH
set "PATH=%PATH%;%VSINSTALLER%"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b %errorlevel%
