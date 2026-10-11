#pragma once

#include <string>

// User preferences, saved as JSON in the data folder.
struct Settings {
	// Multiplies the monitor's DPI scale. 1.0 = the size Windows display scaling asks for.
	float uiScale = 1.0f;
};

static constexpr float kMinUiScale = 0.75f;
static constexpr float kMaxUiScale = 2.0f;

// Missing or unreadable files give the defaults.
Settings LoadSettings(const std::string& path);
bool SaveSettings(const std::string& path, const Settings& settings);
