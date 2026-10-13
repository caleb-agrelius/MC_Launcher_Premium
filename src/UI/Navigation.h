#pragma once

#include <string>

// The pages the sidebar leads to.
enum class Page {
	Discover,
	Library,
	Downloads,
	Servers,
	Settings,
	Manage // one My Library pack
};

// How a page sends the player somewhere else. Pages hold one of these instead of the shell,
// so they don't know what else is on screen or how it's laid out.
class INavigator {
public:
	virtual ~INavigator() = default;
	virtual void ShowPage(Page page) = 0;
	// The Manage page of a library pack; with browse, opened on Content > Browse.
	virtual void ShowManage(const std::string& packId, bool browse = false) = 0;
	// The Servers page with this pack's server selected.
	virtual void ShowServer(const std::string& packId) = 0;
};
