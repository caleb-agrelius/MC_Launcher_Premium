#pragma once

#include "UI/Pages/Manage/ContentBrowser.h"
#include "UI/Pages/Manage/ContentModel.h"
#include "UI/Pages/Manage/ManageTypes.h"

#include <filesystem>
#include <string>
#include <vector>

// Content: the pack's mods, resource packs and shader packs, switched on and off natively,
// removed, added from disk, or found on Modrinth (ContentBrowser).
class ContentTab final : public IManageTab {
public:
	explicit ContentTab(ContentModel& model) : m_model(model), m_browser(model) {}

	// Opens Mods with the Modrinth browser, e.g. for a pack that was just created.
	void ShowBrowse() { m_kind = 0; m_browsing = true; }
	// Adds dropped or picked files, then shows where they went. True when any were added.
	bool AddFiles(const ManagePageContext& context, const std::vector<std::filesystem::path>& paths);

	void Reload(const ManagePageContext& context) override;
	void Draw(const ManagePageContext& context, ManageFrame& frame) override;

private:
	ContentModel& m_model;
	ContentBrowser m_browser;
	int m_kind = 0;          // which of mods, resource packs, shader packs is shown
	std::string m_filter;
	std::string m_removeName; // content whose "Remove" confirmation is open
	bool m_browsing = false;
};
