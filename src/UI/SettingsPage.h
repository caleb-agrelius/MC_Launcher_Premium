#pragma once

#include <cstdint>
#include <string>

struct Settings;

// Facts about the app and machine that the Settings page shows.
struct SettingsPageContext {
	std::string appVersion;
	std::string dataFolder;            // absolute path of the folder with settings.json, downloads/, installs/
	std::uint64_t systemMemoryMb = 0;  // 0 if unknown
	float dpiScale = 1.0f;
};

// Draws the Settings page into the current window. Edits settings in place and returns
// true if anything changed (the caller saves and applies theme/scale changes).
bool DrawSettingsPage(Settings& settings, const SettingsPageContext& context);
