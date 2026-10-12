#pragma once

#include "UI/Pages/Manage/ChangelogTab.h"
#include "UI/Pages/Manage/ContentModel.h"
#include "UI/Pages/Manage/ContentTab.h"
#include "UI/Pages/Manage/LogsTab.h"
#include "UI/Pages/Manage/ManageHeader.h"
#include "UI/Pages/Manage/ManageTypes.h"
#include "UI/Pages/Manage/OverviewTab.h"
#include "UI/Pages/Manage/ScreenshotsTab.h"

#include <string>

// The full-page view of one My Library pack: Overview, Content (mods, resource packs and
// shader packs, switched on and off natively), Changelog, Logs (selectable and copyable)
// and Screenshots. Keep one for the app's lifetime; background work posts back to its tabs.
class ManagePage {
public:
	ManagePage();

	ManageRequest Draw(const ManagePageContext& context);
	// Forgets cached folder listings, e.g. after the active version changed or files were installed.
	void Refresh() { m_loadedFor.clear(); }
	// Opens Content > Mods with the Modrinth browser, e.g. for a pack that was just created.
	void ShowBrowse();

private:
	enum Tab { kOverview, kContent, kChangelog, kLogs, kScreenshots, kTabCount };

	IManageTab& TabAt(int tab);
	void Reload(const ManagePageContext& context);
	void ShowTab(int tab, const ManagePageContext& context);

	ManageHeader m_header;
	ContentModel m_contentModel;
	ScreenshotsTab m_screenshots;
	OverviewTab m_overview;
	ContentTab m_content;
	ChangelogTab m_changelog;
	LogsTab m_logs;

	std::string m_loadedFor; // pack id + game folder the tabs' data belongs to
	int m_tab = kOverview;
	double m_tabShownAt = 0.0;
};
