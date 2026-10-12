#pragma once

#include <string>

// What the launcher does once Minecraft starts.
enum class AfterLaunch {
	KeepOpen = 0,
	Minimize = 1,
	Close = 2,
};

// Default Minecraft launch options. Launching isn't implemented yet; these are saved
// so they're ready when it is.
struct LaunchSettings {
	int minMemoryMb = 2048;
	int maxMemoryMb = 4096;
	std::string javaPath; // empty = detect automatically
	std::string jvmArgs;  // extra JVM arguments
	int windowWidth = 1280;
	int windowHeight = 720;
	bool fullscreen = false;
	AfterLaunch afterLaunch = AfterLaunch::KeepOpen;
};

// User preferences, saved as JSON in the data folder.
struct Settings {
	// Multiplies the monitor's DPI scale. 1.0 = the size Windows display scaling asks for.
	float uiScale = 1.0f;
	std::string theme = "grass"; // Theme::ThemeInfo::id
	bool reduceMotion = false;   // no image fade-ins or toast slide-ins
	bool showGalleryBanners = true;
	int searchLimit = 30;        // results per Modrinth search
	LaunchSettings launch;
};

static constexpr float kMinUiScale = 0.75f;
static constexpr float kMaxUiScale = 2.0f;
static constexpr int kMinSearchLimit = 10;
static constexpr int kMaxSearchLimit = 100; // Modrinth's maximum
static constexpr int kMinMemoryMb = 512;
static constexpr int kMinWindowSide = 320;

// Missing or unreadable files give the defaults. Values are clamped to their valid ranges.
Settings LoadSettings(const std::string& path);
bool SaveSettings(const std::string& path, const Settings& settings);
