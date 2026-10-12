#pragma once

#include "UI/Pages/Manage/ManageTypes.h"

#include <filesystem>
#include <string>
#include <vector>

// Screenshots: thumbnails of the pack's screenshots, newest first, and a full-size viewer.
class ScreenshotsTab final : public IManageTab {
public:
	std::size_t Count() const { return m_screenshots.size(); }

	void Reload(const ManagePageContext& context) override;
	void Draw(const ManagePageContext& context, ManageFrame& frame) override;
	// The viewer, a modal that can open over any tab.
	void DrawViewer(const ManagePageContext& context);

private:
	struct Screenshot {
		std::filesystem::path path;
		std::string label; // file name
	};

	std::vector<Screenshot> m_screenshots;
	int m_viewing = -1; // screenshot open in the viewer
};
