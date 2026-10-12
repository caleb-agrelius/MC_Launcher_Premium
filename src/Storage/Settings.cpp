#include "Settings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>

using json = nlohmann::json;

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
		settings.theme = j.value("theme", settings.theme);
		settings.reduceMotion = j.value("reduceMotion", settings.reduceMotion);
		settings.showGalleryBanners = j.value("showGalleryBanners", settings.showGalleryBanners);
		settings.searchLimit = std::clamp(j.value("searchLimit", settings.searchLimit), kMinSearchLimit, kMaxSearchLimit);

		const json launch = j.value("launch", json::object());
		LaunchSettings& l = settings.launch;
		l.maxMemoryMb = std::max(kMinMemoryMb, launch.value("maxMemoryMb", l.maxMemoryMb));
		l.minMemoryMb = std::clamp(launch.value("minMemoryMb", l.minMemoryMb), kMinMemoryMb, l.maxMemoryMb);
		l.javaPath = launch.value("javaPath", l.javaPath);
		l.jvmArgs = launch.value("jvmArgs", l.jvmArgs);
		l.windowWidth = std::max(kMinWindowSide, launch.value("windowWidth", l.windowWidth));
		l.windowHeight = std::max(kMinWindowSide, launch.value("windowHeight", l.windowHeight));
		l.fullscreen = launch.value("fullscreen", l.fullscreen);
		l.afterLaunch = static_cast<AfterLaunch>(std::clamp(launch.value("afterLaunch", 0), 0, 2));
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
	j["theme"] = settings.theme;
	j["reduceMotion"] = settings.reduceMotion;
	j["showGalleryBanners"] = settings.showGalleryBanners;
	j["searchLimit"] = settings.searchLimit;
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
	ofs << j.dump(2) << "\n";
	return ofs.good();
}
