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
		settings.discoverSource = j.value("discoverSource", settings.discoverSource);
	}
	catch (...) {
		// corrupt file: keep defaults
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
	j["discoverSource"] = settings.discoverSource;
	ofs << j.dump(2) << "\n";
	return ofs.good();
}
