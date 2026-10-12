#pragma once

#include "UI/Pages/PageFrame.h"

// A screen the sidebar leads to. The shell draws the sidebar, then calls DrawHeader() and Draw()
// on the current page inside the scrolling content area, and knows nothing else about it.
class IPage {
public:
	virtual ~IPage() = default;

	// The breadcrumb and title above the page (and anything else that belongs beside them,
	// like a button).
	virtual void DrawHeader() = 0;

	// The page itself.
	virtual void Draw(const PageFrame& frame) = 0;

	// False when the page has nothing to show any more (its pack was uninstalled while open);
	// the shell then goes back to My Library.
	virtual bool Available() const { return true; }

	// Called on the frame the page becomes the current one.
	virtual void OnShown() {}
};

// A page about one pack at a time: the Servers page (that pack's server) and the Manage page.
// The shell tells it which pack the player picked before showing it.
class IPackPage : public IPage {
public:
	// With browse, a Manage page opens on Content > Browse Modrinth; others ignore it.
	virtual void SelectPack(const std::string& packId, bool browse) = 0;
};
