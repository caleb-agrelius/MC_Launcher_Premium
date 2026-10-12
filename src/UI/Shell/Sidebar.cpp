#include "UI/Shell/Sidebar.h"

#include "Services/Downloads/DownloadService.h"
#include "Services/Servers/ServerService.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include <cmath>

using Theme::S;

void Sidebar::Draw(Page current)
{
	ImGui::BeginChild("sidebar", ImVec2(S(230.0f), 0.0f), ImGuiChildFlags_Borders,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	// Transparent interactive mascot, then the launcher name.
	m_mascot.Draw(S(56.0f));
	ImGui::SameLine(0.0f, S(8.0f));
	ImGui::BeginGroup();
	ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
	Widgets::Wordmark("MC LAUNCHER", std::floor(S(2.0f)), Theme::Mix(Theme::C().text, Theme::C().accent, 0.1f),
		Theme::Mix(Theme::C().accentDeep, Theme::C().windowBg, 0.15f));
	Widgets::Text(Theme::GetFonts().bold, Theme::kFontSmall, Theme::C().accent, "PREMIUM  \xC2\xB7  JAVA");
	ImGui::EndGroup();

	ImGui::Dummy(ImVec2(0.0f, S(14.0f)));
	Widgets::SectionLabel("BROWSE");
	if (Widgets::NavItem("Discover", current == Page::Discover))
		m_navigator.ShowPage(Page::Discover);
	if (Widgets::NavItem("My Library", current == Page::Library || current == Page::Manage))
		m_navigator.ShowPage(Page::Library);
	if (Widgets::NavItem("Downloads", current == Page::Downloads, m_downloads.ActiveCount()))
		m_navigator.ShowPage(Page::Downloads);
	if (Widgets::NavItem("Servers", current == Page::Servers, m_servers.RunningCount()))
		m_navigator.ShowPage(Page::Servers);
	ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
	Widgets::SectionLabel("LAUNCHER");
	if (Widgets::NavItem("Settings", current == Page::Settings))
		m_navigator.ShowPage(Page::Settings);

	m_account.Draw();
	ImGui::EndChild();
	m_account.DrawOfflinePopup();
}
