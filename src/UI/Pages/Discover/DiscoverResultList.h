#pragma once

#include "Model/Modpack.h"
#include "Services/Discover/DiscoverService.h"
#include "Services/Settings/SettingsStore.h"

#include <string>
#include <vector>

class ImageCache;

// Discover's results: the status line above them and the scrolling list of packs (with its
// resizable edge and lazy loading) on the left of the page.
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
	// What one result shows, whatever view draws it.
	struct ResultInfo {
		const Modpack* pack = nullptr;
		std::string meta;         // game version and downloads, or author
		const char* tag = nullptr; // source name on a combined search, "Offline" for the local index
	};
	ResultInfo Describe(const Modpack& item, bool singleResult) const;

	// The views of the merged results. Each draws inside the scroll area and returns nothing; picking
	// a result sets State().selectedId. DrawGrid is the hook for the grid view and shows the list for now.
	void DrawRows(const Modpack* selected, double rowsShownAt, bool singleResult);
	void DrawGrid(const Modpack* selected, double rowsShownAt, bool singleResult);
	// Lazy loading of the next page and its status, under whichever view is showing.
	void DrawLoadMore();

	DiscoverService& m_discover;
	SettingsStore& m_settings;
	ImageCache& m_images;
};
