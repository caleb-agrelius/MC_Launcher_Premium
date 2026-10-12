#include "UI/Pages/Manage/OverviewCards.h"

#include "Network/PackSources.h"
#include "Platform/Platform.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "Util/Text.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>

namespace fs = std::filesystem;
using Theme::S;

void InstalledVersionsCard::Draw(const ManagePageContext& context, ManageRequest& request)
{
	const Modpack& pack = *context.pack;
	Widgets::BeginCard("manage_versions");
	Widgets::SectionLabel("INSTALLED VERSIONS");
	for (const InstalledVersion& v : pack.installedVersions) {
		ImGui::PushID(v.key.c_str());
		const bool active = v.key == pack.activeVersion;
		ImGui::AlignTextToFramePadding();
		ImGui::PushFont(Theme::GetFonts().bold, 0.0f);
		ImGui::TextUnformatted(v.version.empty() ? v.key.c_str() : v.version.c_str());
		ImGui::PopFont();
		std::string about = v.gameVersion.empty() ? std::string() : "Minecraft " + v.gameVersion;
		if (!v.loaders.empty())
			about += (about.empty() ? "" : "  \xC2\xB7  ") + v.loaders;
		ImGui::SameLine();
		ImGui::TextDisabled("%s", about.c_str());
		if (active) {
			ImGui::SameLine();
			Widgets::Chip("Active", Theme::C().accent);
		}
		if (!v.contentChanges.empty()) {
			ImGui::SameLine();
			Widgets::ModifiedChip(DescribeContentChanges(v.contentChanges));
		}
		const char* uninstall = "Uninstall...";
		const float buttons = Widgets::ButtonWidth(uninstall) + (active ? 0.0f : Widgets::ButtonWidth("Make active") + ImGui::GetStyle().ItemSpacing.x);
		ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - buttons));
		ImGui::BeginDisabled(context.playBusy || context.updating);
		if (!active) {
			if (ImGui::Button("Make active"))
				request = { ManageAction::SelectVersion, v.key };
			ImGui::SetItemTooltip("PLAY, Open folder and this page use the active version");
			ImGui::SameLine();
		}
		if (ImGui::Button(uninstall))
			request = { ManageAction::Uninstall, pack.installedVersions.size() > 1 ? v.key : std::string() };
		ImGui::EndDisabled();
		ImGui::PopID();
	}
	Widgets::EndCard();
	ImGui::Spacing();
}

void PlayerChangesCard::Draw(const ManagePageContext& context, ManageRequest& request)
{
	if (!context.changes || context.changes->empty())
		return;
	Widgets::BeginCard("manage_changes");
	Widgets::SectionLabel("YOUR CHANGES");
	const std::string summary = "You changed this version: " + DescribeContentChanges(*context.changes) +
		". Your changes are kept when you play or repair it. Updating installs a new version without them.";
	Widgets::WrappedText(Theme::C().text, summary.c_str());
	ImGui::Spacing();
	ImGui::PushFont(nullptr, Theme::kFontSmall);
	for (const auto& [path, change] : *context.changes) {
		const char* what = change == "added" ? "Added" : change == "removed" ? "Removed"
			: change == "disabled" ? "Turned off" : "Turned on";
		ImGui::TextColored(Widgets::ModifiedColor(), "%s", what);
		ImGui::SameLine(S(90.0f));
		ImGui::TextDisabled("%s", path.c_str());
	}
	ImGui::PopFont();
	ImGui::Spacing();
	ImGui::BeginDisabled(context.playBusy || context.updating);
	if (m_confirmUndo) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Delete the files you added and restore the pack's own?");
		ImGui::SameLine();
		if (Widgets::DangerButton("Undo changes")) {
			request.action = ManageAction::UndoChanges;
			m_confirmUndo = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("Keep"))
			m_confirmUndo = false;
	} else if (ImGui::Button("Undo my changes...")) {
		m_confirmUndo = true;
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Deletes added files, switches files back, and downloads removed ones again (Repair files)");
	Widgets::EndCard();
	ImGui::Spacing();
}

void PackToolsCard::Draw(const ManagePageContext& context, ManageRequest& request)
{
	const Modpack& pack = *context.pack;
	Widgets::BeginCard("manage_tools");
	Widgets::SectionLabel("MANAGE");
	// A read-only path (selectable, so it can be copied) with an Open button.
	auto folderRow = [](const char* label, const fs::path& path) {
		ImGui::PushID(label);
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", label);
		ImGui::SameLine(S(130.0f));
		std::string text = path.u8string();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - Widgets::ButtonWidth("Open") - ImGui::GetStyle().ItemSpacing.x);
		ImGui::InputText("##path", &text, ImGuiInputTextFlags_ReadOnly);
		ImGui::SameLine();
		if (ImGui::Button("Open"))
			Platform::OpenFolder(path.string());
		ImGui::PopID();
	};
	folderRow("Game folder", context.gameDir);
	folderRow("Pack files", context.installDir);
	ImGui::Spacing();
	if (ImGui::Button(context.hasLaunchSettings ? "Launch settings (custom)..." : "Launch settings..."))
		request.action = ManageAction::LaunchSettings;
	ImGui::SetItemTooltip("Memory, Java and window options for this pack only");
	if (context.canUpdate) {
		ImGui::SameLine();
		ImGui::BeginDisabled(context.updating);
		if (ImGui::Button("Update..."))
			request.action = ManageAction::Update;
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Install the newest version from %s and bring your worlds and settings along",
			PackSourceName(pack.source));
	}
	if (context.canRepair) {
		ImGui::SameLine();
		ImGui::BeginDisabled(context.playBusy || context.updating);
		if (ImGui::Button("Repair files"))
			request.action = ManageAction::Repair;
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Re-check every pack file and re-download damaged or missing ones");
	}
	if (context.canHostServer) {
		ImGui::SameLine();
		if (ImGui::Button("Host a server"))
			request.action = ManageAction::Server;
		ImGui::SetItemTooltip("Set up and run this pack as a server on the Servers page");
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(context.playBusy || context.updating);
	if (Widgets::DangerButton(pack.installedVersions.size() > 1 ? "Uninstall all versions..." : "Uninstall..."))
		request = { ManageAction::Uninstall, std::string() };
	ImGui::EndDisabled();
	Widgets::EndCard();
}
