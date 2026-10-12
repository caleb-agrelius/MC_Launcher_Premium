#include "UI/Pages/Manage/LogsTab.h"

#include "Platform/Platform.h"
#include "UI/Common/Background.h"
#include "UI/Common/UiHelpers.h"
#include "UI/Pages/Manage/LogReader.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Text.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;
using Theme::S;

void LogsTab::Reload(const ManagePageContext& context)
{
	++m_generation;
	std::error_code ec;
	// Logs: latest first, then the launcher's own output, older logs and crash reports (newest first).
	m_logs.clear();
	auto addLogs = [&](const fs::path& folder, const std::string& prefix, std::initializer_list<const char*> extensions) {
		std::vector<std::pair<fs::file_time_type, LogFile>> found;
		for (const auto& entry : fs::directory_iterator(folder, ec)) {
			if (!entry.is_regular_file(ec))
				continue;
			const std::string name = entry.path().filename().u8string();
			const std::string lower = ToLower(name);
			bool match = false;
			for (const char* extension : extensions)
				match = match || (lower.size() > std::strlen(extension) && lower.compare(lower.size() - std::strlen(extension), std::string::npos, extension) == 0);
			if (!match)
				continue;
			found.push_back({ entry.last_write_time(ec), LogFile{ prefix + name, entry.path(), entry.file_size(ec) } });
		}
		std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
			if ((a.second.label.find("latest.log") != std::string::npos) != (b.second.label.find("latest.log") != std::string::npos))
				return a.second.label.find("latest.log") != std::string::npos;
			return a.first > b.first;
		});
		for (auto& [time, log] : found)
			m_logs.push_back(std::move(log));
	};
	addLogs(context.gameDir / "logs", "", { ".log", ".log.gz" });
	addLogs(context.gameDir / "crash-reports", "crash-reports/", { ".txt" });
	m_logIndex = 0;
	m_logLines.clear();
	m_shownLines.clear();
	m_shownFilter.clear();
	m_logError.clear();
	m_logLoading = false;
	m_logView.ClearSelection();
}

void LogsTab::OnShown(const ManagePageContext& context)
{
	if (m_logLines.empty() && !m_logLoading && !m_logs.empty())
		Load(context, m_logIndex);
}

void LogsTab::Load(const ManagePageContext& context, std::size_t index)
{
	if (index >= m_logs.size())
		return;
	m_logIndex = index;
	m_logLoading = true;
	m_logError.clear();
	m_logLines.clear();
	m_shownLines.clear();
	m_refilter = true;
	m_logView.ClearSelection();
	m_logView.ScrollToBottom();
	const fs::path path = m_logs[index].path;
	const int generation = m_generation;
	struct Read {
		std::vector<std::string> lines;
		std::string error;
		bool ok = false;
	};
	RunInBackground(context.tasks, [path]() {
		Read read;
		std::string text;
		read.ok = LogReader::ReadText(path, text, &read.error);
		read.lines = LogReader::SplitLines(text);
		return read;
	}, [this, index, generation](Read read) {
		if (generation != m_generation || index != m_logIndex)
			return;
		m_logLoading = false;
		m_logError = read.ok ? std::string() : read.error;
		m_logLines = std::move(read.lines);
		m_refilter = true; // filter the new lines on the next frame
	});
}

void LogsTab::Draw(const ManagePageContext& context, ManageFrame& frame)
{
	if (m_logs.empty()) {
		UiHelpers::EmptyWithRefresh("No logs yet. They appear here once the pack has been played.", "Refresh##logs", frame.reload);
		return;
	}
	const float height = std::max(S(320.0f), ImGui::GetContentRegionAvail().y);
	// The log files.
	const float maxListWidth = ImGui::GetContentRegionAvail().x / S(1.0f) - 360.0f;
	m_logListWidth = std::clamp(m_logListWidth, 160.0f, std::max(160.0f, maxListWidth));
	Widgets::BeginScrollArea("log_files", ImVec2(S(m_logListWidth), height), ImGuiChildFlags_Borders);
	for (std::size_t i = 0; i < m_logs.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		if (Widgets::ListItem(m_logs[i].label.c_str(), i == m_logIndex) && i != m_logIndex)
			Load(context, i);
		ImGui::SetItemTooltip("%s  \xC2\xB7  %s", m_logs[i].label.c_str(), FormatFileSize(m_logs[i].size).c_str());
		ImGui::PopID();
	}
	Widgets::EndScrollArea();
	ImGui::SameLine(0.0f, 0.0f);
	Widgets::PaneSplitter("##logs_split", &m_logListWidth, 160.0f, maxListWidth);
	ImGui::SameLine(0.0f, 0.0f);

	// The selected log.
	ImGui::BeginGroup();
	const LogFile& log = m_logs[std::min(m_logIndex, m_logs.size() - 1)];
	const float buttons = Widgets::ButtonWidth("Open file") + Widgets::ButtonWidth("Open folder") + ImGui::GetStyle().ItemSpacing.x * 2.0f;
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttons);
	ImGui::InputTextWithHint("##log_filter", "Show only lines containing...", &m_logFilter);
	ImGui::SameLine();
	if (ImGui::Button("Open file"))
		Platform::OpenFile(log.path.string());
	ImGui::SameLine();
	if (ImGui::Button("Open folder##logs"))
		Platform::OpenFolder(log.path.parent_path().string());
	if (m_refilter || m_logFilter != m_shownFilter) {
		m_refilter = false;
		m_shownFilter = m_logFilter;
		m_shownLines.clear();
		const std::string filter = ToLower(m_logFilter);
		for (const std::string& line : m_logLines) {
			if (filter.empty() || ToLower(line).find(filter) != std::string::npos)
				m_shownLines.push_back(line);
		}
		m_logView.ClearSelection();
	}
	const float viewHeight = height - ImGui::GetFrameHeightWithSpacing();
	if (m_logLoading) {
		// The log view's size and look, so it doesn't jump when the lines arrive.
		ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Mix(Theme::C().windowBg, Theme::C().cardBg, 0.4f));
		if (ImGui::BeginChild("log_loading", ImVec2(0.0f, viewHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding))
			UiHelpers::BusyLabel(("Reading " + log.label + "...").c_str());
		ImGui::EndChild();
		ImGui::PopStyleColor();
	} else if (!m_logError.empty()) {
		Widgets::WrappedText(Theme::C().error, m_logError.c_str());
	} else {
		ImGui::PushFont(nullptr, Theme::kFontSmall + 0.5f);
		m_logView.Draw("log_text", m_shownLines, ImVec2(0.0f, viewHeight));
		ImGui::PopFont();
	}
	ImGui::EndGroup();
}
