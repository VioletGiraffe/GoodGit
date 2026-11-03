@echo off

:: Prepares the caller's environment for a qmake build, for build_tests.bat and build_app.bat. Exit code 2 when it is incomplete.
:: The Qt kit comes from qt_kit.bat (QT_ROOT_DIR); the MSVC environment is set up through vswhere unless cl
:: is already on PATH, as it is on CI.
:: No setlocal: the caller keeps the result.

call "%~dp0qt_kit.bat" || exit /b 2

:: qmake probes cl for the MSVC version, so the environment is needed although MSBuild locates the toolset itself.
:: .qmake.stash caches that probe: an existing build tree hides a missing environment, a fresh checkout fails.
set "VSINSTALLER=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
where cl >nul 2>nul || call :vcvars || exit /b 2

:: A kept .qmake.stash pins the toolchain probed when it was written, so every build probes the current one instead.
:: One is written per subproject build directory, and qmake also searches upward, so the sweep covers the repository.
for /f "usebackq delims=" %%s in (`dir /s /b /a-d "%~dp0..\.qmake.stash" 2^>nul`) do del /q "%%s"
exit /b 0

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
