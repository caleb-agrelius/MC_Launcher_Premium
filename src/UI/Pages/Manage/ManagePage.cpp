#include "UI/Pages/Manage/ManagePage.h"

#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "imgui.h"

#include <algorithm>

ManagePage::ManagePage()
	: m_overview(m_contentModel, m_screenshots), m_content(m_contentModel)
{
}

IManageTab& ManagePage::TabAt(int tab)
{
	switch (tab) {
	case kContent: return m_content;
	case kChangelog: return m_changelog;
	case kLogs: return m_logs;
	case kScreenshots: return m_screenshots;
	default: return m_overview;
	}
}

void ManagePage::Reload(const ManagePageContext& context)
{
	m_loadedFor = context.pack->id + "|" + context.gameDir.u8string();
	m_contentModel.Reload(context);
	for (int tab = 0; tab < kTabCount; ++tab)
		TabAt(tab).Reload(context);
	TabAt(m_tab).OnShown(context);
}

void ManagePage::ShowBrowse()
{
	m_tab = kContent;
	m_content.ShowBrowse();
	m_tabShownAt = ImGui::GetTime();
}

void ManagePage::ShowTab(int tab, const ManagePageContext& context)
{
	m_tab = tab;
	m_tabShownAt = ImGui::GetTime();
	TabAt(m_tab).OnShown(context);
}

ManageRequest ManagePage::Draw(const ManagePageContext& context)
{
	if (!context.pack)
		return {};
	if (m_loadedFor != context.pack->id + "|" + context.gameDir.u8string())
		Reload(context);
	if (!context.droppedPaths.empty() && m_content.AddFiles(context, context.droppedPaths))
		ShowTab(kContent, context);
	if (m_contentModel.reloadRequested) {
		m_contentModel.reloadRequested = false;
		m_contentModel.ReloadFolders(context);
	}

	ManageFrame frame;
	frame.request = m_header.Draw(context);
	ImGui::Spacing();

	const std::string contentLabel = "Content (" + std::to_string(m_contentModel.Count(0)) + ")";
	const std::string shotsLabel = "Screenshots (" + std::to_string(m_screenshots.Count()) + ")";
	const std::string logsLabel = "Logs (" + std::to_string(m_logs.Count()) + ")";
	const char* tabs[kTabCount] = { "Overview", contentLabel.c_str(), "Changelog", logsLabel.c_str(), shotsLabel.c_str() };
	if (Widgets::TabStrip("manage_tabs", tabs, kTabCount, &m_tab))
		ShowTab(m_tab, context);
	ImGui::Spacing();

	Widgets::BeginEntrance(std::max(context.shownAt, m_tabShownAt));
	TabAt(m_tab).Draw(context, frame);
	Widgets::EndEntrance();
	m_screenshots.DrawViewer(context);

	if (frame.showTab >= 0)
		ShowTab(frame.showTab, context);
	if (frame.reload)
		m_loadedFor.clear(); // reloads next frame
	return frame.request;
}
