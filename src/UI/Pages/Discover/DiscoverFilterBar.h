#pragma once

#include "Services/Discover/DiscoverService.h"
#include "Services/Settings/SettingsStore.h"

// Discover's query controls: the search box, the source chips, and the Minecraft version, loader
// and sort pickers. Edits go to the DiscoverService's state, which sends the search once typing pauses.
class DiscoverFilterBar {
public:
	DiscoverFilterBar(DiscoverService& discover, SettingsStore& settings) : m_discover(discover), m_settings(settings) {}

	// Puts the keyboard in the search box (Ctrl+F) on the next frame.
	void FocusSearch() { m_focusSearch = true; }

	// plan says which sources the search will go to (see DiscoverService::Plan).
	void Draw(const DiscoverPlan& plan);

private:
	DiscoverService& m_discover;
	SettingsStore& m_settings;
	bool m_focusSearch = false;
};
