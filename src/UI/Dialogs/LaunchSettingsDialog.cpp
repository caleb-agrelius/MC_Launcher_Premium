#include "UI/Dialogs/LaunchSettingsDialog.h"

#include "Services/Library/PackLibrary.h"
#include "Services/Settings/SettingsStore.h"
#include "UI/Pages/Settings/SettingsPageHost.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include <algorithm>

using Theme::S;

void LaunchSettingsDialog::Draw()
{
	if (m_packId.empty())
		return;
	const Modpack* pack = m_library.Find(m_packId);
	if (!pack) {
		m_packId.clear();
		return;
	}
	ImGui::OpenPopup("Launch settings");
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowSize(ImVec2(std::min(S(720.0f), display.x * 0.9f), std::min(S(640.0f), display.y * 0.85f)));
	if (!ImGui::BeginPopupModal("Launch settings", nullptr, ImGuiWindowFlags_NoResize))
		return;
	Widgets::Text(Theme::GetFonts().bold, Theme::kFontHeading, Theme::C().text, pack->name.c_str());
	ImGui::TextDisabled("Used the next time you press Play for this pack.");
	const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
	Widgets::BeginScrollArea("pack_launch_scroll", ImVec2(0.0f, -footer));
	if (m_settingsPage.DrawPackLaunch(m_settings.Get(), m_packId))
		m_settings.MarkDirty();
	Widgets::EndScrollArea();
	if (ImGui::Button("Done")) {
		m_packId.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}
