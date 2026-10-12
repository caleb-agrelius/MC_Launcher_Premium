#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

// Starts a program without a console window. args[0] is the executable.
// stdout and stderr are appended to logFile. If wait is true, blocks until the
// program exits and fills exitCode. When cancel becomes true while waiting, the
// program is killed if killOnCancel is set; otherwise it keeps running and
// RunProcess returns false with err = "Cancelled".
// Windows only; elsewhere it fails with an error.
bool RunProcess(const std::vector<std::string>& args,
				const std::filesystem::path& workDir,
				const std::filesystem::path& logFile,
				bool wait,
				int* exitCode = nullptr,
				std::string* err = nullptr,
				const std::atomic<bool>* cancel = nullptr,
				bool killOnCancel = true);
