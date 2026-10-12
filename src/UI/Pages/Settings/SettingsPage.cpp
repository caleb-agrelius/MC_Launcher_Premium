#include "UI/Pages/Settings/SettingsPage.h"

#include "UI/Widgets.h"
#include "imgui.h"

#include <iterator>

namespace {


const char* const kTabNames[] = { "Appearance", "Minecraft", "Game options", "Account", "Downloads", "About" };

}

// Pill-style tab strip.
void SettingsPage::DrawTabStrip()
{
	int current = static_cast<int>(m_tab);
	if (Widgets::TabStrip("settings_tabs", kTabNames, static_cast<int>(std::size(kTabNames)), &current)) {
		m_tab = static_cast<Tab>(current);
		m_layout.TabSwitched(ImGui::GetTime());
	}
}

ISettingsTab& SettingsPage::TabFor(Tab tab)
{
	switch (tab) {
	case Tab::Minecraft: return m_minecraft;
	case Tab::GameOptions: return m_gameOptions;
	case Tab::Account: return m_account;
	case Tab::Downloads: return m_downloads;
	case Tab::About: return m_about;
	default: return m_appearance;
	}
}

bool SettingsPage::Draw(Settings& settings, const SettingsPageContext& context)
{
	m_layout.BeginFrame(context.shownAt);
	DrawTabStrip();
	ImGui::Spacing();

	Widgets::BeginScrollArea("settings_scroll");
	m_layout.BeginFlow();
	const bool changed = TabFor(m_tab).Draw(settings, context, m_layout);
	m_layout.EndFlow();
	Widgets::EndScrollArea();
	return changed;
}

bool SettingsPage::DrawPackLaunch(Settings& settings, const std::string& packId, const SettingsPageContext& context)
{
	return m_packLaunch.Draw(settings, packId, context, m_layout);
}
