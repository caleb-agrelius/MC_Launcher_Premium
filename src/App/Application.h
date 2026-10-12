#pragma once

#include <filesystem>
#include <ostream>

// The running launcher: opens the window, builds the app (AppRoot), loops over frames until
// the window closes, then shuts everything down in order.
class Application {
public:
	// dataRoot is the folder holding resources/ (the working directory), or empty if not found.
	Application(std::filesystem::path dataRoot, std::ostream& log) : m_dataRoot(std::move(dataRoot)), m_log(log) {}

	// Returns the process exit code.
	int Run();

private:
	std::filesystem::path m_dataRoot;
	std::ostream& m_log;
};
