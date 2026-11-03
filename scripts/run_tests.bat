@echo off
setlocal

:: Runs the tests build_tests.bat built. Exit code: the test run's, or 2 when the kit or the build is missing.
:: An argument of "debug" runs the debug configuration.
:: Any further arguments go to the test executable, e.g. a Catch2 test spec.

:: shift renumbers %0 along with the arguments, so the script's own directory has to be taken before the first one
set "SCRIPT_DIR=%~dp0"

set "CONFIG=release"
if /i "%~1"=="debug" (
	set "CONFIG=debug"
	shift
)

:: qt_kit.bat puts the Qt DLLs on PATH
call "%SCRIPT_DIR%qt_kit.bat" || exit /b 2

set "TESTS_EXE=%SCRIPT_DIR%..\tests\bin\%CONFIG%\tests.exe"
if not exist "%TESTS_EXE%" (
	echo The %CONFIG% tests are not built: run build_tests.bat first.
	exit /b 2
)
:: The backend tests run these; call: hg may be a batch wrapper, which would otherwise end this script
git --version
call hg --version -q
:: --warn NoTests: a filter that matches nothing exits 0 otherwise, so a broken one would pass silently
"%TESTS_EXE%" %1 %2 %3 %4 %5 --warn NoTests
exit /b %errorlevel%
