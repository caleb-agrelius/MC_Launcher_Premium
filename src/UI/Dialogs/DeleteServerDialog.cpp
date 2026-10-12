#include "UI/Dialogs/DeleteServerDialog.h"

#include "Services/AppPaths.h"
#include "Services/Library/PackLibrary.h"
#include "Services/Servers/ServerService.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include <filesystem>

using Theme::S;

void DeleteServerDialog::Draw()
{
	if (m_packId.empty())
		return;
	ImGui::OpenPopup("Delete server?");
	ImGui::SetNextWindowSize(ImVec2(S(460.0f), 0.0f));
	if (!ImGui::BeginPopupModal("Delete server?", nullptr, ImGuiWindowFlags_NoResize))
		return;
	const Modpack* libraryPack = m_library.Find(m_packId);
	const std::string name = libraryPack ? libraryPack->name : m_packId;
	ImGui::TextWrapped("%s's server will be deleted: its world, configuration, whitelist and logs. "
		"The pack stays in My Library. This can't be undone.", name.c_str());
	ImGui::TextDisabled("%s", (std::filesystem::path(AppPaths::kServersRoot) / m_packId).string().c_str());
	ImGui::Spacing();
	const bool blocked = m_servers.IsRunningOrBusy(m_packId);
	if (blocked)
		ImGui::TextColored(Theme::C().error, "Stop the server and wait for setup to finish first.");
	ImGui::BeginDisabled(blocked);
	if (Widgets::DangerButton("Delete server")) {
		m_servers.StartDelete(m_packId, name);
		m_packId.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Keep it##server")) {
		m_packId.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}
