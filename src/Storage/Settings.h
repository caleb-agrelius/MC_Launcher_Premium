#pragma once

#include <map>
#include <string>
#include <vector>

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

// Vanilla game options (options.txt) applied to every pack's game folder at launch.
// Only options the player turned on in Settings are written; the rest stay as the
// pack or the player left them in-game.
struct GameOptionsSettings {
	bool everyLaunch = true; // false = only the first time each pack is launched
	std::map<std::string, std::string> values; // options.txt key -> value as written to the file
};

// A player-made color theme. Colors are RGB 0-1, in Theme::Palette field order.
struct CustomTheme {
	std::string id;   // "custom-<n>", saved as Settings::theme when selected
	std::string name;
	bool light = false;
	std::vector<float> colors; // 11 colors x 3 channels
};

// User preferences, saved as JSON in the data folder.
struct Settings {
	// Multiplies the monitor's DPI scale. 1.0 = the size Windows display scaling asks for.
	float uiScale = 1.0f;
	// Discover sub-page shown at startup (a PackSourceKey, e.g. "modrinth").
	std::string discoverSource = "modrinth";
	std::string theme = "grass"; // Theme::ThemeInfo::id
	bool reduceMotion = false;   // no image fade-ins or toast slide-ins
	bool showGalleryBanners = true;
	int searchLimit = 30;        // results per search (CurseForge caps it at 50)
	// Public Azure app client ID used for Microsoft sign-in. The app must still
	// be approved for the Minecraft API. Empty disables Microsoft sign-in.
	std::string microsoftClientId = "838edcea-b214-4030-b620-4e436525b305";
	// CurseForge API key. Empty = use the MCLP_CURSEFORGE_API_KEY environment variable.
	std::string curseForgeApiKey;
	LaunchSettings launch;
	GameOptionsSettings gameOptions;
	std::vector<CustomTheme> customThemes;
};

static constexpr float kMinUiScale = 0.75f;
static constexpr float kMaxUiScale = 2.0f;
static constexpr int kMinSearchLimit = 10;
static constexpr int kMaxSearchLimit = 100; // Modrinth's maximum
static constexpr int kMinMemoryMb = 1024;   // smallest -Xmx
static constexpr int kMaxMemoryMb = 32768;
static constexpr int kMinWindowSide = 320;
static constexpr std::size_t kCustomThemeFloats = 11 * 3; // 11 palette colors, RGB

// Missing or unreadable files give the defaults. Values are clamped to their valid ranges.
// The older top-level "maxMemoryMb" key is read into launch.maxMemoryMb.
Settings LoadSettings(const std::string& path);
bool SaveSettings(const std::string& path, const Settings& settings);
