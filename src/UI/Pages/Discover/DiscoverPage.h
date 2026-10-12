#pragma once

#include "Services/Discover/DiscoverService.h"
#include "UI/Pages/Discover/DiscoverDetails.h"
#include "UI/Pages/Discover/DiscoverFilterBar.h"
#include "UI/Pages/Discover/DiscoverResultList.h"
#include "UI/Pages/IPage.h"
#include "UI/Pages/PageFrame.h"

// The Discover page: search every source at once, browse the merged results, and install or
// play the selected pack. It only lays the pieces out; each owns its part of the screen.
class DiscoverPage final : public IPage {
public:
	DiscoverPage(DiscoverService& discover, DiscoverFilterBar& filters, DiscoverResultList& results, DiscoverDetails& details)
		: m_discover(discover), m_filters(filters), m_results(results), m_details(details) {}

	// Ctrl+F: jump to the search box.
	void FocusSearch() { m_filters.FocusSearch(); }

	void DrawHeader() override;
	void Draw(const PageFrame& frame) override;

private:
	void DrawDetailsModal(SourceView* view, Modpack* selected, bool searching, const PageFrame& frame);

	DiscoverService& m_discover;
	DiscoverFilterBar& m_filters;
	DiscoverResultList& m_results;
	DiscoverDetails& m_details;
};
