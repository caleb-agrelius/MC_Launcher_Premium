#include "UI/Dialogs/StopGameDialog.h"

#include "Services/Play/PlayService.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

using Theme::S;

void StopGameDialog::Draw()
{
	if (m_playKey.empty())
		return;
	ImGui::OpenPopup("Stop Minecraft?");
	ImGui::SetNextWindowSize(ImVec2(S(420.0f), 0.0f));
	if (!ImGui::BeginPopupModal("Stop Minecraft?", nullptr, ImGuiWindowFlags_NoResize))
		return;
	ImGui::TextWrapped("Minecraft will be closed right away. Anything not saved since the last autosave "
		"(usually under a minute ago) will be lost.");
	ImGui::Spacing();
	if (Widgets::DangerButton("Stop game")) {
		m_plays.Stop(m_playKey);
		m_playKey.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Keep playing")) {
		m_playKey.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}
