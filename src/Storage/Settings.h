#pragma once

#include <string>

// What the launcher does once Minecraft starts.
enum class AfterLaunch {
	KeepOpen = 0,
	Minimize = 1, // minimized while the game runs, restored when it exits
	Close = 2,    // the launcher exits once the game has started
};

// Default Minecraft launch options, used for every pack.
struct LaunchSettings {
	int minMemoryMb = 0;     // Java -Xms; 0 = let Java decide
	int maxMemoryMb = 4096;  // Java -Xmx
	std::string javaPath;    // java/javaw executable; empty = Mojang's runtime for the version
	std::string jvmArgs;     // extra JVM arguments, space separated ("quotes" group words)
	int windowWidth = 0;     // game window size; 0 = Minecraft's default (854x480)
	int windowHeight = 0;
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
	// Public Azure app client ID used for Microsoft sign-in. The app must still
	// be approved for the Minecraft API. Empty disables Microsoft sign-in.
	std::string microsoftClientId = "838edcea-b214-4030-b620-4e436525b305";
	LaunchSettings launch;
};

static constexpr float kMinUiScale = 0.75f;
static constexpr float kMaxUiScale = 2.0f;
static constexpr int kMinSearchLimit = 10;
static constexpr int kMaxSearchLimit = 100; // Modrinth's maximum
static constexpr int kMinMemoryMb = 1024;   // smallest -Xmx
static constexpr int kMaxMemoryMb = 32768;
static constexpr int kMinWindowSide = 320;

// Missing or unreadable files give the defaults. Values are clamped to their valid ranges.
// The older top-level "maxMemoryMb" key is read into launch.maxMemoryMb.
Settings LoadSettings(const std::string& path);
bool SaveSettings(const std::string& path, const Settings& settings);
