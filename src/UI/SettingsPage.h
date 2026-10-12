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
	std::string accountName;           // empty when signed out
	std::string accountKind;           // e.g. "Microsoft account", "Offline (singleplayer)"
	int accountCount = 0;              // saved accounts
	bool curseForgeEnvKeySet = false;  // MCLP_CURSEFORGE_API_KEY is set
	double shownAt = 0.0;              // ImGui::GetTime() when the page was opened (entrance animation)
};

// Draws the Settings page into the current window. Edits settings in place and returns
// true if anything changed (the caller saves and applies theme/scale changes).
bool DrawSettingsPage(Settings& settings, const SettingsPageContext& context);

// The per-pack launch options (memory, Java, JVM arguments, window) shown from My Library:
// a toggle to override Settings > Minecraft, then the same cards. Returns true when
// settings changed and should be saved.
bool DrawPackLaunchSettings(Settings& settings, const std::string& packId, const SettingsPageContext& context);
