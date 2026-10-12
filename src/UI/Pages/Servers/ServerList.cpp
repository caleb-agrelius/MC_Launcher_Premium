#include "UI/Pages/Servers/ServerList.h"

#include "Services/Library/PackLibrary.h"
#include "Services/Servers/ServerService.h"
#include "Services/Settings/SettingsStore.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

using Theme::S;

void ServerList::Draw(const std::map<std::string, const Modpack*>& packs, std::string& selected)
{
	const float available = ImGui::GetContentRegionAvail().x / S(1.0f);
	float width = m_settings.PaneWidth("servers", 320.0f, available - 460.0f);
	Widgets::BeginScrollArea("server_list", ImVec2(S(width), 0.0f), ImGuiChildFlags_Borders);
	Widgets::SectionLabel("YOUR PACKS");
	for (const auto& [packId, packPtr] : packs) {
		const Modpack& pack = *packPtr;
		const bool inLibrary = m_library.Contains(packId);
		const ServerState& server = m_servers.State(packId);
		const bool busy = m_servers.Setup(packId).busy;
		const char* tag = server.running ? "Running" : busy ? "Setting up" : server.ready ? "Ready" : nullptr;
		const ImVec4 tagColor = server.running ? Theme::C().accent : busy ? Theme::C().info : Theme::C().textDim;
		std::string meta = !inLibrary ? "Pack uninstalled"
			: m_servers.CanHost(pack)
			? (server.ready ? server.info.loader + " " + server.info.minecraftVersion : "No server yet")
			: "Pack files are missing";
		if (inLibrary && !m_library.ContentChangesOf(pack).empty())
			meta += "  \xC2\xB7  modified pack";
		ImGui::PushID(packId.c_str());
		if (Widgets::PackRow(pack, m_images, packId == selected, meta, tag, tagColor))
			selected = packId;
		ImGui::PopID();
	}
	Widgets::EndScrollArea();
	ImGui::SameLine(0.0f, 0.0f);
	if (Widgets::PaneSplitter("##servers_split", &width, 220.0f, available - 460.0f))
		m_settings.SetPaneWidth("servers", width);
	ImGui::SameLine(0.0f, 0.0f);
}
