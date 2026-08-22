// Starts gg.exe from the directory above this one. Only this directory goes on PATH: the application's own
// directory holds the Qt DLLs, and a PATH entry for it would offer them to every process's DLL search.

#include <Windows.h>

#include <string>

namespace {

constexpr const wchar_t* ApplicationFileName = L"gg.exe";

std::wstring ownExecutablePath()
{
	std::wstring path(MAX_PATH, L'\0');
	for (;;)
	{
		const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
		if (length == 0)
			return {};

		if (length < path.size()) // truncation returns the buffer size, never less
		{
			path.resize(length);
			return path;
		}

		path.resize(path.size() * 2);
	}
}

bool removeLastPathComponent(std::wstring& path)
{
	const size_t separator = path.find_last_of(L'\\');
	if (separator == std::wstring::npos)
		return false;

	path.resize(separator);
	return true;
}

// Skips the program name: quoted it ends at the closing quote, unquoted at the first whitespace. Escapes
// are not processed within it.
const wchar_t* argumentsOf(const wchar_t* commandLine)
{
	if (*commandLine == L'"')
	{
		++commandLine;
		while (*commandLine != L'\0' && *commandLine != L'"')
			++commandLine;

		if (*commandLine == L'"')
			++commandLine;
	}
	else
	{
		while (*commandLine != L'\0' && *commandLine != L' ' && *commandLine != L'\t')
			++commandLine;
	}

	while (*commandLine == L' ' || *commandLine == L'\t')
		++commandLine;

	return commandLine;
}

int reportFailure(const std::wstring& message)
{
	::MessageBoxW(nullptr, message.c_str(), L"GoodGit", MB_ICONERROR | MB_OK);
	return 1;
}

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
	// <install dir>\launcher\gg.exe -> <install dir>\gg.exe
	std::wstring application = ownExecutablePath();
	if (application.empty() || !removeLastPathComponent(application) || !removeLastPathComponent(application))
		return reportFailure(L"Could not locate the GoodGit executable.");

	application += L'\\';
	application += ApplicationFileName;

	// CreateProcessW takes the exact image from the first argument and the child's argv[0] from the command
	// line, and writes to the latter's buffer.
	std::wstring commandLine = L'"' + application + L'"';
	const wchar_t* const arguments = argumentsOf(::GetCommandLineW());
	if (*arguments != L'\0')
	{
		commandLine += L' ';
		commandLine += arguments;
	}

	STARTUPINFOW startupInfo{};
	startupInfo.cb = sizeof(startupInfo);
	PROCESS_INFORMATION process{};

	// The inherited working directory is the point of the launcher: gg opens the repository the terminal is in
	if (!::CreateProcessW(application.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startupInfo, &process))
		return reportFailure(L"Could not start " + application);

	::CloseHandle(process.hThread);
	::CloseHandle(process.hProcess);
	return 0;
}
