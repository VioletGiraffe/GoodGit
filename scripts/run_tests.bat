@echo off
setlocal

:: Builds tests\tests.pro and runs it. Exit code: the test run's, or 2 when the environment is incomplete.
:: The Qt kit comes from qt_kit.bat (QT_ROOT_DIR); the MSVC environment is set up through vswhere unless cl
:: is already on PATH, as it is on CI. An argument of "debug" builds and runs the debug configuration, for
:: debug_tests.bat.
:: Any further arguments go to the test executable, e.g. a Catch2 test spec.

:: shift renumbers %0 along with the arguments, so the script's own directory has to be taken before the first one
set "SCRIPT_DIR=%~dp0"

set "CONFIG=release"
if /i "%~1"=="debug" (
	set "CONFIG=debug"
	shift
)

call "%SCRIPT_DIR%qt_kit.bat" || exit /b 2

set "VSINSTALLER=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
where cl >nul 2>nul || call :vcvars || exit /b 2

pushd "%SCRIPT_DIR%..\tests" || exit /b 2
"%QT_ROOT_DIR%\bin\qmake.exe" -tp vc tests.pro || goto :fail
:: msbuild, not nmake: it also rebuilds what the compiler command line changed for, such as the Qt include paths
msbuild tests.vcxproj /nologo /m /v:minimal /p:Configuration=%CONFIG% /p:Platform=x64 || goto :fail
popd
"%SCRIPT_DIR%..\tests\bin\%CONFIG%\tests.exe" %1 %2 %3 %4 %5
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
