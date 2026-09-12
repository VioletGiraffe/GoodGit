// Loads gg.dll from the lib directory beside this executable and runs it. Only this executable's own
// directory goes on PATH: lib holds the Qt DLLs, and a PATH entry for it would offer them to every process's
// DLL search.

#include <Windows.h>

#include <stdlib.h>   // __argc, __argv
#include <string>

namespace {

constexpr const wchar_t* LibraryDirectoryName = L"lib";
constexpr const wchar_t* ApplicationFileName = L"gg.dll";
constexpr const char* EntryPointName = "ggMain";

using EntryPoint = int (*)(int argc, char* argv[], const wchar_t* libraryDirectory);

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

int reportFailure(const std::wstring& message)
{
	::MessageBoxW(nullptr, message.c_str(), L"GoodGit", MB_ICONERROR | MB_OK);
	return 1;
}

}

// WinMain, not wWinMain: the wide CRT startup leaves __argv null
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	// <install dir>\gg.exe -> <install dir>\lib
	std::wstring libraryDirectory = ownExecutablePath();
	if (libraryDirectory.empty() || !removeLastPathComponent(libraryDirectory))
		return reportFailure(L"Could not locate the GoodGit installation directory.");

	libraryDirectory += L'\\';
	libraryDirectory += LibraryDirectoryName;

	// Resolves the Qt DLLs that gg.dll and the Qt plugins import. Not PATH: no child process inherits it.
	::SetDllDirectoryW(libraryDirectory.c_str());

	const std::wstring application = libraryDirectory + L'\\' + ApplicationFileName;
	const HMODULE module = ::LoadLibraryW(application.c_str());
	if (!module)
		return reportFailure(L"Could not load " + application);

	const EntryPoint entryPoint = reinterpret_cast<EntryPoint>(::GetProcAddress(module, EntryPointName));
	if (!entryPoint)
		return reportFailure(L"Could not find the entry point in " + application);

	// Not unloaded: the module's static destructors then run at process exit, as an exe's do
	return entryPoint(__argc, __argv, libraryDirectory.c_str());
}
