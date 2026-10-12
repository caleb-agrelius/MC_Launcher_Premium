#pragma once

#include <string>

// User preferences, saved as JSON in the data folder.
struct Settings {
	// Multiplies the monitor's DPI scale. 1.0 = the size Windows display scaling asks for.
	float uiScale = 1.0f;
	// Azure app (client) ID used for Microsoft sign-in. Mojang must approve it
	// for the Minecraft API. Empty disables Microsoft sign-in.
	std::string microsoftClientId;
	// Maximum Java heap for Minecraft, in MB.
	int maxMemoryMb = 4096;
};

static constexpr float kMinUiScale = 0.75f;
static constexpr float kMaxUiScale = 2.0f;
static constexpr int kMinMemoryMb = 1024;
static constexpr int kMaxMemoryMb = 32768;

// Missing or unreadable files give the defaults.
Settings LoadSettings(const std::string& path);
bool SaveSettings(const std::string& path, const Settings& settings);
