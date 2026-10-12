#include "UI/Dialogs/OptionalModsDialog.h"

#include "Services/Discover/OptionalModsService.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include <algorithm>

using Theme::S;

void OptionalModsDialog::Draw()
{
	if (!m_optional.IsOpen())
		return;
	const OptionalPicker& picker = m_optional.Picker();
	ImGui::OpenPopup("Optional mods");
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowSize(ImVec2(std::min(S(640.0f), display.x * 0.9f), std::min(S(600.0f), display.y * 0.85f)));
	if (!ImGui::BeginPopupModal("Optional mods", nullptr, ImGuiWindowFlags_NoResize))
		return;
	Widgets::Text(Theme::GetFonts().bold, Theme::kFontHeading, Theme::C().text, picker.pack.name.c_str());
	if (picker.loading) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("Loading optional mods...");
	} else if (!picker.error.empty()) {
		ImGui::TextColored(Theme::C().error, "Couldn't load optional mods: %s", picker.error.c_str());
	} else {
		ImGui::TextDisabled("Choose extra mods to install. Mods they need are added too.");
	}
	const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
	Widgets::BeginScrollArea("optional_list", ImVec2(0.0f, -footer), ImGuiChildFlags_Borders);
	for (std::size_t i = 0; i < picker.mods.size(); ++i) {
		const OptionalMod& mod = picker.mods[i];
		ImGui::PushID(static_cast<int>(i));
		bool on = picker.chosen[i] != 0;
		if (Widgets::ToggleRow(mod.name.c_str(), &on))
			m_optional.SetOptional(i, on);
		if (!mod.group.empty()) {
			ImGui::SameLine();
			ImGui::TextDisabled("(pick one: %s)", mod.group.c_str());
		}
		if (!mod.description.empty()) {
			ImGui::Indent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::C().textDim);
			ImGui::TextWrapped("%s", mod.description.c_str());
			ImGui::PopStyleColor();
			ImGui::Unindent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
		}
		ImGui::PopID();
	}
	Widgets::EndScrollArea();
	// Confirm and Cancel close the picker, which empties the state read above, so decide first.
	bool confirm = false;
	bool cancel = false;
	ImGui::BeginDisabled(picker.loading);
	if (ImGui::Button(picker.error.empty() ? "Install" : "Install without optional mods"))
		confirm = true;
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel##optional"))
		cancel = true;
	if (confirm)
		m_optional.Confirm();
	else if (cancel)
		m_optional.Cancel();
	if (confirm || cancel)
		ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}
