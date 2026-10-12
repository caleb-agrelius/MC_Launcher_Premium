#include "Platform.h"

#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#else
#include <cstdlib>
#include <unistd.h>
#endif

namespace Platform {

bool OpenFolder(const std::string& path)
{
	std::error_code ec;
	std::filesystem::create_directories(path, ec);
	const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
#ifdef _WIN32
	const HINSTANCE result = ShellExecuteW(nullptr, L"open", absolute.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return reinterpret_cast<INT_PTR>(result) > 32;
#else
	const std::string command = "xdg-open \"" + absolute.string() + "\" &";
	return std::system(command.c_str()) == 0;
#endif
}

std::uint64_t SystemMemoryMb()
{
#ifdef _WIN32
	MEMORYSTATUSEX status{};
	status.dwLength = sizeof(status);
	if (GlobalMemoryStatusEx(&status))
		return status.ullTotalPhys / (1024 * 1024);
	return 0;
#else
	const long pages = sysconf(_SC_PHYS_PAGES);
	const long pageSize = sysconf(_SC_PAGE_SIZE);
	if (pages <= 0 || pageSize <= 0)
		return 0;
	return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize) / (1024 * 1024);
#endif
}

}
