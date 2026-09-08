@echo off
setlocal

:: Runs the built tests under cdb, non-interactively: the commands given run at the initial breakpoint, so
:: they say what to do (set breakpoints, g) and end with q. The stack of the first crash, for example:
::   debug_tests.bat "g; kv; q"
:: Further arguments go to the tests binary (a test name, a tag). The debug build is used where one exists,
:: as run_tests.bat debug leaves it, else the release one. QT_NATVIS, when set, names a qt6.natvis to load
:: first, so Qt strings and containers print readably.

set "CDB=%ProgramFiles(x86)%\Windows Kits\10\Debuggers\x64\cdb.exe"
if not exist "%CDB%" (
	echo cdb.exe not found: install the Debugging Tools for Windows feature of the Windows SDK.
	exit /b 2
)

call "%~dp0qt_kit.bat" || exit /b 2

set "TESTS_EXE=%~dp0..\tests\bin\debug\tests.exe"
if not exist "%TESTS_EXE%" set "TESTS_EXE=%~dp0..\tests\bin\release\tests.exe"
if not exist "%TESTS_EXE%" (
	echo The tests are not built: run run_tests.bat first.
	exit /b 2
)

set "COMMANDS=%~1"
if defined QT_NATVIS set "COMMANDS=.nvload %QT_NATVIS%; %COMMANDS%"
"%CDB%" -G -lines -c "%COMMANDS%" "%TESTS_EXE%" %2 %3 %4 %5 %6 %7 %8 %9
