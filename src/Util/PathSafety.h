#pragma once

#include <string>

// True when id can name one folder under installs/, instances/ or servers/: not empty, not
// "." or "..", and no path separators. Pack ids come from outside (library.json, pack sources),
// and some folders named after them get deleted, so they are checked first.
inline bool IsPlainFolderName(const std::string& id)
{
	return !id.empty() && id != "." && id != ".." && id.find_first_of("/\\:") == std::string::npos;
}
