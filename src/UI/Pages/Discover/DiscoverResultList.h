#pragma once

#include "Model/Modpack.h"
#include "Services/Discover/DiscoverService.h"
#include "Services/Settings/SettingsStore.h"

#include <string>
#include <vector>

class ImageCache;

// Discover's results: the status line above them and the scrolling list of packs (with its
// resizable edge and "Load more") on the left of the page.
class DiscoverResultList {
public:
	DiscoverResultList(DiscoverService& discover, SettingsStore& settings, ImageCache& images)
		: m_discover(discover), m_settings(settings), m_images(images) {}

	// "Searching X..." while sources are working, else the result count and each source's total.
	// Returns the names of the sources still searching.
	std::vector<std::string> DrawStatus(const DiscoverPlan& plan);

	// The list pane, then its splitter. Leaves the cursor so the details pane can follow on the same line.
	// selected is the pack highlighted (and shown in the details pane); picking another changes it.
	void DrawList(const Modpack* selected, bool searching, double pageShownAt);

private:
	DiscoverService& m_discover;
	SettingsStore& m_settings;
	ImageCache& m_images;
};
