#include "Settings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>

using json = nlohmann::json;

// Window sizes: 0 (Minecraft's default) or at least kMinWindowSide.
static int ClampWindowSide(int value)
{
	return value <= 0 ? 0 : std::max(kMinWindowSide, value);
}

// Reads launch options over the values already in l (so defaults stay for missing keys).
static void LaunchFromJson(const json& launch, LaunchSettings& l)
{
	l.maxMemoryMb = std::clamp(launch.value("maxMemoryMb", l.maxMemoryMb), kMinMemoryMb, kMaxMemoryMb);
	l.minMemoryMb = std::clamp(launch.value("minMemoryMb", l.minMemoryMb), 0, l.maxMemoryMb);
	l.javaPath = launch.value("javaPath", l.javaPath);
	l.jvmArgs = launch.value("jvmArgs", l.jvmArgs);
	l.windowWidth = ClampWindowSide(launch.value("windowWidth", l.windowWidth));
	l.windowHeight = ClampWindowSide(launch.value("windowHeight", l.windowHeight));
	l.fullscreen = launch.value("fullscreen", l.fullscreen);
	l.afterLaunch = static_cast<AfterLaunch>(std::clamp(launch.value("afterLaunch", static_cast<int>(l.afterLaunch)), 0, 2));
}

static json LaunchToJson(const LaunchSettings& l)
{
	return {
		{ "minMemoryMb", l.minMemoryMb },
		{ "maxMemoryMb", l.maxMemoryMb },
		{ "javaPath", l.javaPath },
		{ "jvmArgs", l.jvmArgs },
		{ "windowWidth", l.windowWidth },
		{ "windowHeight", l.windowHeight },
		{ "fullscreen", l.fullscreen },
		{ "afterLaunch", static_cast<int>(l.afterLaunch) },
	};
}

LaunchSettings LaunchSettingsFor(const Settings& settings, const std::string& packId)
{
	const auto it = settings.packLaunch.find(packId);
	if (it == settings.packLaunch.end())
		return settings.launch;
	LaunchSettings launch = it->second;
	launch.afterLaunch = settings.launch.afterLaunch; // a launcher-wide choice
	return launch;
}

Settings LoadSettings(const std::string& path)
{
	Settings settings;
	std::ifstream ifs(path);
	if (!ifs.is_open())
		return settings;
	try {
		json j;
		ifs >> j;
		settings.uiScale = std::clamp(j.value("uiScale", settings.uiScale), kMinUiScale, kMaxUiScale);
		settings.discoverSources = j.value("discoverSources", settings.discoverSources);
		settings.theme = j.value("theme", settings.theme);
		settings.reduceMotion = j.value("reduceMotion", settings.reduceMotion);
		settings.showGalleryBanners = j.value("showGalleryBanners", settings.showGalleryBanners);
		settings.searchLimit = std::clamp(j.value("searchLimit", settings.searchLimit), kMinSearchLimit, kMaxSearchLimit);
		settings.microsoftClientId = j.value("microsoftClientId", settings.microsoftClientId);
		settings.curseForgeApiKey = j.value("curseForgeApiKey", settings.curseForgeApiKey);
		const json paneWidths = j.value("paneWidths", json::object()); // items() must not outlive it
		for (const auto& [pane, width] : paneWidths.items()) {
			if (width.is_number())
				settings.paneWidths[pane] = std::clamp(width.get<float>(), kMinPaneWidth, kMaxPaneWidth);
		}

		LaunchSettings& l = settings.launch;
		// Before the Settings page, memory was a top-level key.
		l.maxMemoryMb = j.value("maxMemoryMb", l.maxMemoryMb);
		LaunchFromJson(j.value("launch", json::object()), l);
		const json packLaunch = j.value("packLaunch", json::object());
		for (const auto& [packId, launch] : packLaunch.items()) {
			if (packId.empty() || !launch.is_object())
				continue;
			LaunchSettings pack = l; // missing keys fall back to the global options
			LaunchFromJson(launch, pack);
			settings.packLaunch[packId] = pack;
		}

		const json gameOptions = j.value("gameOptions", json::object());
		settings.gameOptions.everyLaunch = gameOptions.value("everyLaunch", settings.gameOptions.everyLaunch);
		// Named, not a temporary: items() only refers to the object it's called on.
		const json optionValues = gameOptions.value("values", json::object());
		for (const auto& [key, value] : optionValues.items()) {
			if (value.is_string() && !key.empty() && key.find(':') == std::string::npos)
				settings.gameOptions.values[key] = value.get<std::string>();
		}

		const json window = j.value("window", json::object());
		WindowPlacement& w = settings.window;
		w.width = window.value("width", 0);
		w.height = window.value("height", 0);
		w.x = window.value("x", 0);
		w.y = window.value("y", 0);
		w.maximized = window.value("maximized", false);
		w.saved = w.width >= 200 && w.height >= 150;

		for (const auto& item : j.value("customThemes", json::array())) {
			CustomTheme theme;
			theme.id = item.value("id", "");
			theme.name = item.value("name", "Custom theme");
			theme.light = item.value("light", false);
			theme.colors = item.value("colors", std::vector<float>());
			if (theme.id.rfind("custom-", 0) == 0 && theme.colors.size() == kCustomThemeFloats)
				settings.customThemes.push_back(std::move(theme));
		}
	}
	catch (...) {
		// corrupt file: keep whatever was read before the error, defaults for the rest
	}
	return settings;
}

bool SaveSettings(const std::string& path, const Settings& settings)
{
	std::ofstream ofs(path, std::ios::trunc);
	if (!ofs.is_open())
		return false;
	json j;
	j["uiScale"] = std::round(settings.uiScale * 100.0) / 100.0; // 1.1, not 1.100000023841858
	j["discoverSources"] = settings.discoverSources;
	j["theme"] = settings.theme;
	j["reduceMotion"] = settings.reduceMotion;
	j["showGalleryBanners"] = settings.showGalleryBanners;
	j["searchLimit"] = settings.searchLimit;
	j["microsoftClientId"] = settings.microsoftClientId;
	j["curseForgeApiKey"] = settings.curseForgeApiKey;
	json paneWidths = json::object();
	for (const auto& [pane, width] : settings.paneWidths)
		paneWidths[pane] = std::round(width);
	j["paneWidths"] = paneWidths;
	j["launch"] = LaunchToJson(settings.launch);
	json packLaunch = json::object();
	for (const auto& [packId, launch] : settings.packLaunch) {
		json entry = LaunchToJson(launch);
		entry.erase("afterLaunch"); // always the global choice
		packLaunch[packId] = entry;
	}
	j["packLaunch"] = packLaunch;
	j["gameOptions"] = {
		{ "everyLaunch", settings.gameOptions.everyLaunch },
		{ "values", settings.gameOptions.values },
	};
	json themes = json::array();
	for (const CustomTheme& theme : settings.customThemes) {
		std::vector<float> colors;
		for (float c : theme.colors)
			colors.push_back(std::round(c * 1000.0f) / 1000.0f);
		themes.push_back({ { "id", theme.id }, { "name", theme.name }, { "light", theme.light }, { "colors", colors } });
	}
	j["customThemes"] = themes;
	if (settings.window.saved) {
		const WindowPlacement& w = settings.window;
		j["window"] = { { "x", w.x }, { "y", w.y }, { "width", w.width }, { "height", w.height }, { "maximized", w.maximized } };
	}
	ofs << j.dump(2) << "\n";
	return ofs.good();
}
