#include "Paths.h"

#include <string>

namespace fs = std::filesystem;

// Leave room for names appended later, e.g. ".part".
static constexpr std::size_t kLongPathThreshold = 240;

fs::path LongPath(const fs::path& path)
{
#ifdef _WIN32
	const std::wstring& native = path.native();
	if (native.rfind(L"\\\\?\\", 0) == 0)
		return path;
	std::error_code ec;
	const fs::path absolute = fs::absolute(path, ec).lexically_normal();
	if (ec || absolute.native().size() < kLongPathThreshold)
		return path;
	const std::wstring& full = absolute.native();
	if (full.rfind(L"\\\\", 0) == 0)
		return fs::path(L"\\\\?\\UNC\\" + full.substr(2)); // \\server\share\...
	return fs::path(L"\\\\?\\" + full);
#else
	return path;
#endif
}

FILE* OpenFile(const fs::path& path, const char* mode)
{
#ifdef _WIN32
	const std::wstring wideMode(mode, mode + std::char_traits<char>::length(mode));
	FILE* file = nullptr;
	if (_wfopen_s(&file, LongPath(path).c_str(), wideMode.c_str()) != 0)
		return nullptr;
	return file;
#else
	return std::fopen(path.c_str(), mode);
#endif
}
