#pragma once

#include <filesystem>
#include <ostream>

// The running launcher: opens the window, builds the app (AppRoot), loops over frames until
// the window closes, then shuts everything down in order.
class Application {
public:
	// dataRoot is the folder holding resources/ (the working directory), or empty if not found.
	// offline: every network request fails at once (see Network/OfflineHttpClient.h).
	Application(std::filesystem::path dataRoot, std::ostream& log, bool offline = false)
		: m_dataRoot(std::move(dataRoot)), m_log(log), m_offline(offline) {}

	// Returns the process exit code.
	int Run();

private:
	std::filesystem::path m_dataRoot;
	std::ostream& m_log;
	bool m_offline;
};
