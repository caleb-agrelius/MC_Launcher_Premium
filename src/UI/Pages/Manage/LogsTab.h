#pragma once

#include "UI/LogView.h"
#include "UI/Pages/Manage/ManageTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Logs: the game's logs and crash reports, selectable and copyable, with a line filter.
class LogsTab final : public IManageTab {
public:
	std::size_t Count() const { return m_logs.size(); }

	void Reload(const ManagePageContext& context) override;
	void OnShown(const ManagePageContext& context) override;
	void Draw(const ManagePageContext& context, ManageFrame& frame) override;

private:
	struct LogFile {
		std::string label;            // e.g. "latest.log", "crash-reports/crash-....txt"
		std::filesystem::path path;
		std::uint64_t size = 0;
	};

	void Load(const ManagePageContext& context, std::size_t index);

	int m_generation = 0; // bumped on Reload, so late background results are ignored
	std::vector<LogFile> m_logs;
	std::size_t m_logIndex = 0;
	std::vector<std::string> m_logLines;
	std::vector<std::string> m_shownLines; // m_logLines matching the filter
	std::string m_logFilter;
	std::string m_shownFilter;
	bool m_refilter = false; // m_shownLines needs rebuilding (new lines loaded)
	bool m_logLoading = false;
	std::string m_logError;
	LogView m_logView;
	float m_logListWidth = 250.0f; // unscaled; the splitter beside the list changes it
};
