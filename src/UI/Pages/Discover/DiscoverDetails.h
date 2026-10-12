#pragma once

#include "Model/Modpack.h"
#include "Services/Discover/DiscoverService.h"
#include "Services/Discover/PackDetailsService.h"
#include "Services/Settings/SettingsStore.h"
#include "UI/Pages/Discover/PackInfoTabs.h"
#include "UI/Pages/Discover/PackInstallSection.h"
#include "UI/Pages/Discover/PackPlayButton.h"
#include "UI/Pages/PageFrame.h"

#include <string>

class ImageCache;

// Discover's details pane for the selected pack: banner, links, description, the install section,
// PLAY, and the Description / Mods tabs.
class DiscoverDetails {
public:
	DiscoverDetails(PackDetailsService& details, SettingsStore& settings, ImageCache& images, PackInstallSection& install,
		PackPlayButton& play, PackInfoTabs& tabs)
		: m_details(details), m_settings(settings), m_images(images), m_install(install), m_play(play), m_tabs(tabs) {}

	// selected may be null (nothing found yet). view is the source it came from.
	// compact (the grid view's modal): the banner shares a row with the version picker, install and
	// PLAY, and the short description is left to the Description tab, so the tabs show on open.
	void Draw(SourceView* view, Modpack* selected, bool searching, const PageFrame& frame, bool compact = false);

private:
	static void DrawStatsRow(const Modpack& pack, bool trimScheme);
	void DrawCompactTop(SourceView& view, Modpack& pack);

	PackDetailsService& m_details;
	SettingsStore& m_settings;
	ImageCache& m_images;
	PackInstallSection& m_install;
	PackPlayButton& m_play;
	PackInfoTabs& m_tabs;
	std::string m_shownPackId;   // the pack the entrance animation last ran for
	double m_shownAt = 0.0;
	// Switching tabs keeps the pane at least as tall as the view was, so a shorter tab doesn't
	// throw the scroll back up.
	float m_keepBottom = 0.0f;
};
