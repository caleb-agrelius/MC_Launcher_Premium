#pragma once

#include "Core/Notifier.h"
#include "UI/Navigation.h"
#include "UI/Pages/IPage.h"
#include "UI/Shell/Shortcuts.h"
#include "UI/Shell/Sidebar.h"

#include <filesystem>
#include <vector>

class DialogHost;
class IPage;
class IPackPage;

// The launcher's window contents: the sidebar, the current page under its header, and the
// dialogs. It knows each page only as an IPage and answers their requests to go elsewhere.
class AppShell final : public INavigator {
public:
	// The pages the sidebar leads to, by role. The shell doesn't create or own them.
	struct Pages {
		IPage& discover;
		IPage& library;
		IPage& downloads;
		IPackPage& servers;
		IPage& settings;
		IPackPage& manage;
	};
	AppShell(Sidebar& sidebar, DialogHost& dialogs, Shortcuts& shortcuts, Pages pages, INotifier& notify);

	Page Current() const { return m_page; }

	// One frame of the window. droppedPaths are the files dropped on it since the last frame.
	void Draw(const std::vector<std::filesystem::path>& droppedPaths);

	void ShowPage(Page page) override { m_page = page; }
	void ShowManage(const std::string& packId, bool browse) override;
	void ShowServer(const std::string& packId) override;

private:
	IPage& PageFor(Page page) const;

	Sidebar& m_sidebar;
	DialogHost& m_dialogs;
	Shortcuts& m_shortcuts;
	Pages m_pages;
	INotifier& m_notify;
	Page m_page = Page::Discover;
	int m_shownPageKey = -1;  // the page whose entrance animation last started
	double m_shownAt = 0.0;
};
