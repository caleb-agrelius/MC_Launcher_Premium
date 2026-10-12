// Linux and other POSIX definitions of the Platform functions (see Platform.h).
// What isn't implemented yet reports "unsupported" the same way the callers already handle:
// no file dialog, no network adapters, an unchanged title bar.
#include "Platform/Platform.h"

#include <cstdlib>
#include <filesystem>
#include <unistd.h>

namespace Platform {

namespace {

// Hands path to the desktop's opener (xdg-open) without blocking on it.
bool OpenWithDesktop(const std::filesystem::path& absolute)
{
	const std::string command = "xdg-open \"" + absolute.string() + "\" &";
	return std::system(command.c_str()) == 0;
}

}

bool OpenFolder(const std::string& path)
{
	std::error_code ec;
	std::filesystem::create_directories(path, ec);
	return OpenWithDesktop(std::filesystem::absolute(path, ec));
}

bool OpenFile(const std::string& path)
{
	std::error_code ec;
	if (!std::filesystem::is_regular_file(path, ec))
		return false;
	return OpenWithDesktop(std::filesystem::absolute(path, ec));
}

std::vector<std::string> PickFiles(const std::string&, const std::string&, const std::string&)
{
	return {};
}

std::uint64_t SystemMemoryMb()
{
	const long pages = sysconf(_SC_PHYS_PAGES);
	const long pageSize = sysconf(_SC_PAGE_SIZE);
	if (pages <= 0 || pageSize <= 0)
		return 0;
	return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize) / (1024 * 1024);
}

std::string LocalNetworkAddress()
{
	return {};
}

std::vector<NetworkAddress> LocalNetworkAddresses()
{
	return {};
}

std::filesystem::path ExecutableDir()
{
	std::error_code ec;
	const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
	return ec ? std::filesystem::path() : self.parent_path();
}

void ApplyTitleBarStyle(GLFWwindow*, const TitleBarStyle&)
{
}

}
