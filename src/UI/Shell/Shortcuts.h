#pragma once

#include "UI/Navigation.h"

class DiscoverPage;
class SettingsStore;

// Keyboard shortcuts that work on every page: Ctrl + / Ctrl - / Ctrl 0 change the UI scale
// (applied next frame), Ctrl F opens Discover with the search box focused.
class Shortcuts {
public:
	Shortcuts(SettingsStore& settings, INavigator& navigator, DiscoverPage& discover)
		: m_settings(settings), m_navigator(navigator), m_discover(discover) {}

	// Call once a frame, after ImGui::NewFrame().
	void Process();

private:
	void SetUiScale(float scale);

	SettingsStore& m_settings;
	INavigator& m_navigator;
	DiscoverPage& m_discover;
};
