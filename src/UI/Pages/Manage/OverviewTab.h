#pragma once

#include "UI/Pages/Manage/ContentModel.h"
#include "UI/Pages/Manage/ManageTypes.h"
#include "UI/Pages/Manage/OverviewCards.h"

#include <string>

class ScreenshotsTab;

// Overview: the banner, the pack at a glance, installed versions, the player's changes and
// the pack's tools.
class OverviewTab final : public IManageTab {
public:
	OverviewTab(const ContentModel& content, const ScreenshotsTab& screenshots)
		: m_content(content), m_screenshots(screenshots) {}

	void Reload(const ManagePageContext& context) override;
	void Draw(const ManagePageContext& context, ManageFrame& frame) override;

private:
	const ContentModel& m_content;
	const ScreenshotsTab& m_screenshots;
	InstalledVersionsCard m_versions;
	PlayerChangesCard m_changes;
	PackToolsCard m_tools;
	int m_worlds = 0;
	std::string m_lastPlayed;
};
