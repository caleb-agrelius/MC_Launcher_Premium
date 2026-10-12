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
		settings.discoverSource = j.value("discoverSource", settings.discoverSource);
		settings.theme = j.value("theme", settings.theme);
		settings.reduceMotion = j.value("reduceMotion", settings.reduceMotion);
		settings.showGalleryBanners = j.value("showGalleryBanners", settings.showGalleryBanners);
		settings.searchLimit = std::clamp(j.value("searchLimit", settings.searchLimit), kMinSearchLimit, kMaxSearchLimit);
		settings.microsoftClientId = j.value("microsoftClientId", settings.microsoftClientId);
		settings.curseForgeApiKey = j.value("curseForgeApiKey", settings.curseForgeApiKey);

		LaunchSettings& l = settings.launch;
		// Before the Settings page, memory was a top-level key.
		l.maxMemoryMb = j.value("maxMemoryMb", l.maxMemoryMb);
		const json launch = j.value("launch", json::object());
		l.maxMemoryMb = std::clamp(launch.value("maxMemoryMb", l.maxMemoryMb), kMinMemoryMb, kMaxMemoryMb);
		l.minMemoryMb = std::clamp(launch.value("minMemoryMb", l.minMemoryMb), 0, l.maxMemoryMb);
		l.javaPath = launch.value("javaPath", l.javaPath);
		l.jvmArgs = launch.value("jvmArgs", l.jvmArgs);
		l.windowWidth = ClampWindowSide(launch.value("windowWidth", l.windowWidth));
		l.windowHeight = ClampWindowSide(launch.value("windowHeight", l.windowHeight));
		l.fullscreen = launch.value("fullscreen", l.fullscreen);
		l.afterLaunch = static_cast<AfterLaunch>(std::clamp(launch.value("afterLaunch", 0), 0, 2));

		const json gameOptions = j.value("gameOptions", json::object());
		settings.gameOptions.everyLaunch = gameOptions.value("everyLaunch", settings.gameOptions.everyLaunch);
		// Named, not a temporary: items() only refers to the object it's called on.
		const json optionValues = gameOptions.value("values", json::object());
		for (const auto& [key, value] : optionValues.items()) {
			if (value.is_string() && !key.empty() && key.find(':') == std::string::npos)
				settings.gameOptions.values[key] = value.get<std::string>();
		}

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
	const LaunchSettings& l = settings.launch;
	json j;
	j["uiScale"] = std::round(settings.uiScale * 100.0) / 100.0; // 1.1, not 1.100000023841858
	j["discoverSource"] = settings.discoverSource;
	j["theme"] = settings.theme;
	j["reduceMotion"] = settings.reduceMotion;
	j["showGalleryBanners"] = settings.showGalleryBanners;
	j["searchLimit"] = settings.searchLimit;
	j["microsoftClientId"] = settings.microsoftClientId;
	j["curseForgeApiKey"] = settings.curseForgeApiKey;
	j["launch"] = {
		{ "minMemoryMb", l.minMemoryMb },
		{ "maxMemoryMb", l.maxMemoryMb },
		{ "javaPath", l.javaPath },
		{ "jvmArgs", l.jvmArgs },
		{ "windowWidth", l.windowWidth },
		{ "windowHeight", l.windowHeight },
		{ "fullscreen", l.fullscreen },
		{ "afterLaunch", static_cast<int>(l.afterLaunch) },
	};
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
	ofs << j.dump(2) << "\n";
	return ofs.good();
}
