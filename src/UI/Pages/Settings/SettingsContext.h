#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct JavaInstall;

// Facts about the app and machine that the Settings page shows.
struct SettingsPageContext {
	std::string appVersion;
	std::string dataFolder;            // absolute path of the folder with settings.json, downloads/, installs/
	std::uint64_t systemMemoryMb = 0;  // 0 if unknown
	float dpiScale = 1.0f;
	std::string accountName;           // empty when signed out
	std::string accountKind;           // e.g. "Microsoft account", "Offline (singleplayer)"
	int accountCount = 0;              // saved accounts
	bool curseForgeEnvKeySet = false;  // MCLP_CURSEFORGE_API_KEY is set
	double shownAt = 0.0;              // ImGui::GetTime() when the page was opened (entrance animation)
	// Javas found on this PC (FindJavaInstalls), for the Java choice. The page asks for a
	// scan with scanJavas when none has run yet, and on Rescan.
	const std::vector<JavaInstall>* javas = nullptr;
	bool javasScanned = false;
	bool javasScanning = false;
	std::function<void()> scanJavas;
	// Shared mod files (Settings > Downloads): a summary of the store, whether a share or
	// stats job is running, the last result, and the jobs to start.
	std::string sharingStats;          // empty until counted
	bool sharingBusy = false;
	bool sharingBlocked = false;       // a game is running, so its mod files are in use
	std::string sharingStatus;         // progress or the last result
	std::function<void()> shareExisting;
	std::function<void()> countSharing;
};
