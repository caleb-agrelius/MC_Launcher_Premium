#pragma once

#include <string>
#include <functional>

// InstallZip extracts a zip file at zipPath into targetDir.
// progress callback receives (totalEntries, extractedEntries).
// Returns true on success; on failure err (if provided) will be set.
bool InstallZip(const std::string& zipPath,
				const std::string& targetDir,
				std::function<void(int,int)> progress = nullptr,
				std::string* err = nullptr);
