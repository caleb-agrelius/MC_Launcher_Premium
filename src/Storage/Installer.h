#pragma once

#include <atomic>
#include <string>
#include <functional>

// InstallZip extracts a zip file at zipPath into targetDir.
// progress callback receives (totalEntries, extractedEntries).
// Entries whose paths would land outside targetDir are rejected and fail the install.
// If cancel is provided and becomes true, extraction stops and returns false.
// Returns true on success; on failure err (if provided) will be set.
bool InstallZip(const std::string& zipPath,
				const std::string& targetDir,
				std::function<void(int,int)> progress = nullptr,
				std::string* err = nullptr,
				const std::atomic<bool>* cancel = nullptr);
