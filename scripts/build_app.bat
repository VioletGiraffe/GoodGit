@echo off
setlocal

:: Builds GoodGit.pro, the app and the libraries it links, into bin\<config>. Exit code 1 when the build fails, 2 when the environment is incomplete.
:: The environment comes from build_env.bat. An argument of "debug" builds the debug configuration.

set "SCRIPT_DIR=%~dp0"

set "CONFIG=release"
if /i "%~1"=="debug" set "CONFIG=debug"

call "%SCRIPT_DIR%build_env.bat" || exit /b 2

pushd "%SCRIPT_DIR%.." || exit /b 2
"%QT_ROOT_DIR%\bin\qmake.exe" -tp vc -r GoodGit.pro || goto :fail
:: msbuild, not nmake: it also rebuilds what the compiler command line changed for, such as the Qt include paths
msbuild GoodGit.sln /nologo /m /v:minimal /p:Configuration=%CONFIG% /p:Platform=x64 || goto :fail
popd
exit /b 0

:fail
popd
exit /b 1
