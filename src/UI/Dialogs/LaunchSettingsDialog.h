#pragma once

#include <string>

class PackLibrary;
class SettingsPageHost;
class SettingsStore;

// Per-pack launch settings (memory, Java, JVM arguments, window), opened from Manage.
class LaunchSettingsDialog {
public:
	LaunchSettingsDialog(PackLibrary& library, SettingsStore& settings, SettingsPageHost& settingsPage)
		: m_library(library), m_settings(settings), m_settingsPage(settingsPage) {}

	void Open(const std::string& packId) { m_packId = packId; }
	void Draw();

private:
	PackLibrary& m_library;
	SettingsStore& m_settings;
	SettingsPageHost& m_settingsPage;
	std::string m_packId; // empty = closed
};
