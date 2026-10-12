#include "UI/Pages/Servers/ServerModsTab.h"

#include "Services/Servers/ServerService.h"
#include "UI/Common/UiHelpers.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Text.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cfloat>
#include <string>
#include <vector>

using Theme::S;

void ServerModsTab::Draw(const ServerTabContext& ctx)
{
	ServerState& server = ctx.server;
	ServerView& view = ctx.view;
	if (server.mods.empty()) {
		ImGui::TextDisabled("This server has no mods folder, or it is empty.");
		return;
	}
	Widgets::BeginCard("server_mods");
	Widgets::SectionLabel("MODS");
	const auto disabledCount = std::count_if(server.mods.begin(), server.mods.end(),
		[](const ServerMod& mod) { return !mod.enabled; });
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("%zu mods, %lld turned off. Mods you turn off stay off when the server files are updated.",
		server.mods.size(), static_cast<long long>(disabledCount));
	UiHelpers::AlignRightForButton("Refresh");
	if (ImGui::Button("Refresh##mods")) {
		server.modsLoaded = false;
		view.modError.clear();
	}
	ImGui::SetItemTooltip("Re-read the server's mods folder");
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::InputTextWithHint("##mod_filter", "Filter mods...", &view.modFilter);
	const std::string filter = ToLower(view.modFilter);
	std::vector<std::size_t> shown;
	for (std::size_t i = 0; i < server.mods.size(); ++i) {
		if (filter.empty() || ContainsIgnoreCase(server.mods[i].fileName, filter))
			shown.push_back(i);
	}
	const float rowHeight = ImGui::GetFrameHeightWithSpacing();
	const float listHeight = std::min(S(280.0f),
		rowHeight * static_cast<float>(std::max<std::size_t>(shown.size(), 1)) + ImGui::GetStyle().WindowPadding.y * 2.0f);
	Widgets::BeginScrollArea("server_mod_list", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders);
	ImGui::BeginDisabled(server.running || ctx.setup.busy);
	ImGuiListClipper clipper;
	clipper.Begin(static_cast<int>(shown.size()), rowHeight);
	while (clipper.Step()) {
		for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
			ServerMod& mod = server.mods[shown[static_cast<std::size_t>(row)]];
			ImGui::PushID(mod.fileName.c_str());
			bool on = mod.enabled;
			if (Widgets::Toggle("##mod_on", &on)) {
				std::string error;
				if (m_servers.SetModEnabled(ctx.packId, server, mod.fileName, on, &error))
					view.modError.clear();
				else
					view.modError = error;
			}
			ImGui::SameLine(0.0f, S(10.0f));
			ImGui::AlignTextToFramePadding();
			if (mod.enabled)
				ImGui::TextUnformatted(mod.fileName.c_str());
			else
				ImGui::TextDisabled("%s", mod.fileName.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s", FormatFileSize(mod.size).c_str());
			ImGui::PopID();
		}
	}
	if (shown.empty())
		ImGui::TextDisabled("No mods match the filter.");
	ImGui::EndDisabled();
	Widgets::EndScrollArea();
	if (server.running)
		ImGui::TextDisabled("Stop the server to turn mods on or off.");
	if (!view.modError.empty())
		Widgets::WrappedText(Theme::C().error, view.modError.c_str());
	Widgets::EndCard();
	ImGui::Spacing();
}
