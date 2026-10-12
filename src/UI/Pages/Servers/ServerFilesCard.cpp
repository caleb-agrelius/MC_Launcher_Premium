#include "UI/Pages/Servers/ServerFilesCard.h"

#include "Platform/Platform.h"
#include "Services/Library/PackLibrary.h"
#include "Services/Servers/ServerService.h"
#include "UI/Common/UiHelpers.h"
#include "UI/Common/VersionPicker.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include <string>
#include <vector>

using Theme::S;

void ServerFilesCard::DrawVersionPicker(const ServerTabContext& ctx)
{
	const Modpack& pack = ctx.pack;
	const ServerState& server = ctx.server;
	// Which pack version to set up: the one installed in My Library unless another is picked.
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("Pack version");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(S(240.0f));
	PackVersionInfo chosen;
	const bool hasChosen = m_servers.ChosenVersion(ctx.packId, chosen);
	const std::string chosenId = hasChosen ? chosen.sourceVersionId : pack.sourceVersionId;
	const std::string preview = hasChosen ? chosen.versionNumber
		: pack.version + (m_library.ContentChangesOf(pack).empty() ? " (installed)" : " (installed, modified)");
	ImGui::BeginDisabled(server.running);
	PackVersionInfo picked;
	if (m_versions.Combo("##server_version", pack, preview, chosenId, picked))
		m_servers.ChooseVersion(ctx.packId, !VersionPicker::IsInstalled(pack, picked), picked);
	ImGui::EndDisabled();
	if (server.ready && !server.info.packVersion.empty()) {
		ImGui::SameLine();
		ImGui::TextDisabled("Server has %s", server.info.packVersion.c_str());
	}
}

void ServerFilesCard::Draw(const ServerTabContext& ctx)
{
	ServerState& server = ctx.server;
	const ServerSetupSnapshot& setup = ctx.setup;
	Widgets::BeginCard("server_files");
	Widgets::SectionLabel("SERVER FILES");
	if (setup.busy) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("%s", setup.step.c_str());
		if (!setup.deleting) {
			UiHelpers::AlignRightForButton("Cancel");
			if (ImGui::Button("Cancel##setup"))
				m_servers.CancelSetup(ctx.packId);
		}
		if (setup.progress >= 0.0)
			Widgets::ThinProgressBar(static_cast<float>(setup.progress));
	} else {
		if (ctx.pack.source != PackSource::LocalIndex)
			DrawVersionPicker(ctx);
		const char* label = server.ready ? "Update server files" : "Create server files";
		std::vector<const char*> buttons;
		if (ctx.hasServerDir) {
			buttons.push_back("Open folder");
			buttons.push_back("Delete...");
		}
		if (ctx.supported)
			buttons.push_back(label);
		// Wrap the description short of the buttons on its right.
		float buttonsWidth = 0.0f;
		for (const char* button : buttons)
			buttonsWidth += ImGui::CalcTextSize(button).x + ImGui::GetStyle().FramePadding.x * 2.0f +
				ImGui::GetStyle().ItemSpacing.x;
		ImGui::AlignTextToFramePadding();
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttonsWidth - S(16.0f));
		if (!ctx.inLibrary)
			ImGui::TextDisabled("Installed in servers/%s. Its pack was uninstalled, so it can't be updated.", ctx.packId.c_str());
		else
			ImGui::TextDisabled(server.ready ? "Installed in servers/%s" : "Downloads the pack's server mods, its loader's server and Java.",
				ctx.packId.c_str());
		ImGui::PopTextWrapPos();
		UiHelpers::AlignRightForButtons(buttons);
		if (ctx.hasServerDir) {
			if (ImGui::Button("Open folder##server"))
				Platform::OpenFolder(ctx.dir.string());
			ImGui::SameLine();
			ImGui::BeginDisabled(server.running);
			if (ImGui::Button("Delete..."))
				m_dialogs.ConfirmDeleteServer(ctx.packId);
			ImGui::EndDisabled();
			ImGui::SetItemTooltip(server.running ? "Stop the server first"
				: "Delete this server's files, world included");
			if (ctx.supported)
				ImGui::SameLine();
		}
		if (ctx.supported) {
			ImGui::BeginDisabled(server.running);
			if (ImGui::Button(label))
				m_servers.StartSetup(ctx.pack);
			ImGui::EndDisabled();
			if (server.running)
				ImGui::SetItemTooltip("Stop the server first");
		}
		if (!setup.error.empty())
			Widgets::WrappedText(Theme::C().error, setup.error.c_str());
		for (const std::string& note : server.info.notes)
			Widgets::WrappedText(Theme::C().textDim, ("Note: " + note).c_str());
	}
	Widgets::EndCard();
}
