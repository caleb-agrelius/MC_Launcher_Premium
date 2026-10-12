#pragma once

#include "UI/Pages/Manage/ManageTypes.h"

#include <map>
#include <set>
#include <string>

// Changelog: what's new in the pack's installed version, from its source.
class ChangelogTab final : public IManageTab {
public:
	void Reload(const ManagePageContext&) override {} // changelogs are kept per version
	void Draw(const ManagePageContext& context, ManageFrame& frame) override;

private:
	void Load(const ManagePageContext& context);

	// By pack id + version id.
	std::map<std::string, std::string> m_changelogs;
	std::map<std::string, std::string> m_changelogErrors;
	std::set<std::string> m_changelogsLoading;
};
