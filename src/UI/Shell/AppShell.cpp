#include "UI/Shell/AppShell.h"

#include "UI/Dialogs/DialogHost.h"
#include "UI/Pages/IPage.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

using Theme::S;

AppShell::AppShell(Sidebar& sidebar, DialogHost& dialogs, Shortcuts& shortcuts, Pages pages, INotifier& notify)
	: m_sidebar(sidebar), m_dialogs(dialogs), m_shortcuts(shortcuts), m_pages(pages), m_notify(notify)
{
}

void AppShell::ShowManage(const std::string& packId, bool browse)
{
	m_pages.manage.SelectPack(packId, browse);
	m_page = Page::Manage;
}

void AppShell::ShowServer(const std::string& packId)
{
	m_pages.servers.SelectPack(packId, false);
	m_page = Page::Servers;
}

IPage& AppShell::PageFor(Page page) const
{
	switch (page) {
	case Page::Library: return m_pages.library;
	case Page::Downloads: return m_pages.downloads;
	case Page::Servers: return m_pages.servers;
	case Page::Settings: return m_pages.settings;
	case Page::Manage: return m_pages.manage;
	default: return m_pages.discover;
	}
}

void AppShell::Draw(const std::vector<std::filesystem::path>& droppedPaths)
{
	// Dropped files are added to the pack open on the Manage page.
	if (!droppedPaths.empty() && m_page != Page::Manage)
		m_notify.Notify(NoticeKind::Info, "Open a pack to add files",
			"Drop mods, resource packs and shader packs on a pack's Manage page.");
	m_shortcuts.Process();

	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
	ImGui::Begin("MC_Launcher_Premium", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

	m_sidebar.Draw(m_page);

	ImGui::SameLine();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24.0f), S(18.0f)));
	Widgets::BeginScrollArea("main_content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::PopStyleVar();
	if (!PageFor(m_page).Available())
		m_page = Page::Library; // e.g. the pack on screen was uninstalled
	const int pageKey = static_cast<int>(m_page);
	IPage& page = PageFor(m_page);
	if (pageKey != m_shownPageKey) {
		m_shownPageKey = pageKey;
		m_shownAt = ImGui::GetTime();
		page.OnShown();
	}
	Widgets::BeginEntrance(m_shownAt);
	page.DrawHeader();
	ImGui::Spacing();
	PageFrame frame;
	frame.shownAt = m_shownAt;
	frame.droppedPaths = &droppedPaths;
	page.Draw(frame);
	m_dialogs.Draw();
	Widgets::EndEntrance();
	Widgets::EndScrollArea();
	ImGui::End();
}
