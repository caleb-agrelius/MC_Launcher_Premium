#include "UI/Dialogs/UpdateDialog.h"

#include "Network/PackSources.h"
#include "Services/Downloads/DownloadService.h"
#include "Services/Library/PackLibrary.h"
#include "Services/Play/PlayService.h"
#include "Services/Updates/UpdateService.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

using Theme::S;

void UpdateDialog::Open(const Modpack& pack)
{
	m_updates.StartCheck(pack);
}

void UpdateDialog::Draw()
{
	UpdateCheck& check = m_updates.Check();
	if (check.packId.empty())
		return;
	const Modpack* found = m_library.Find(check.packId);
	if (!found) {
		m_updates.CloseCheck();
		return;
	}
	ImGui::OpenPopup("Update pack");
	ImGui::SetNextWindowSize(ImVec2(S(460.0f), 0.0f));
	if (!ImGui::BeginPopupModal("Update pack", nullptr, ImGuiWindowFlags_NoResize))
		return;
	const Modpack pack = *found;
	const Modpack target = check.target;
	const InstalledVersion* installed = target.id.empty() ? nullptr : FindInstalledVersion(pack, VersionKey(target));
	bool canUpdate = false;
	if (check.loading) {
		Widgets::Spinner(Theme::C().accent);
		ImGui::SameLine();
		ImGui::TextDisabled("Checking %s for the newest version...", PackSourceName(pack.source));
		// The answer is a couple of paragraphs and a checkbox; hold the room so the dialog doesn't grow.
		ImGui::Spacing();
		const float width = ImGui::GetContentRegionAvail().x;
		for (int line = 0; line < 2; ++line)
			Widgets::Skeleton(ImVec2(line == 1 ? width * 0.6f : width, ImGui::GetTextLineHeightWithSpacing()), 4.0f, S(10.0f));
		ImGui::Spacing();
		for (int line = 0; line < 4; ++line)
			Widgets::Skeleton(ImVec2(line == 3 ? width * 0.4f : width, ImGui::GetTextLineHeightWithSpacing()), 4.0f, S(10.0f));
		ImGui::Spacing();
		Widgets::Skeleton(ImVec2(width * 0.5f, ImGui::GetFrameHeight()), 4.0f, S(14.0f));
	} else if (!check.error.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::C().error);
		ImGui::TextWrapped("Couldn't check for updates: %s", check.error.c_str());
		ImGui::PopStyleColor();
	} else if (installed && installed->key == pack.activeVersion) {
		ImGui::TextWrapped("%s %s is the newest version.", pack.name.c_str(), pack.version.c_str());
	} else if (installed) {
		ImGui::TextWrapped("The newest version, %s, is already installed. Make it active (the card's version "
			"dropdown, or Installed versions on its Manage page) to play it.", target.version.c_str());
	} else {
		canUpdate = true;
		std::string newest = target.version;
		if (!target.gameVersion.empty())
			newest += " (Minecraft " + target.gameVersion + ")";
		ImGui::TextWrapped("Update %s from %s to %s?", pack.name.c_str(), pack.version.c_str(), newest.c_str());
		ImGui::Spacing();
		ImGui::TextWrapped("The new version is installed next to the current one. Your worlds, screenshots, "
			"resource and shader packs, schematics, server list and game options are copied over. "
			"Files the new version ships itself take their place.");
		if (!target.gameVersion.empty() && !pack.gameVersion.empty() && target.gameVersion != pack.gameVersion) {
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::C().error);
			ImGui::TextWrapped("The Minecraft version changes from %s to %s. Back up worlds you care about.",
				pack.gameVersion.c_str(), target.gameVersion.c_str());
			ImGui::PopStyleColor();
		}
		if (!pack.optionalMods.empty())
			ImGui::TextDisabled("Your optional mod choices are kept.");
		ImGui::Spacing();
		ImGui::Checkbox(("Remove " + pack.version + " afterwards").c_str(), &check.removeOld);
		ImGui::SetItemTooltip("Deletes the old version's folders once everything has been copied, "
			"including its saves folder. Leave it off to keep the old version installed.");
	}
	const bool busy = m_updates.Running(pack.id) != nullptr || m_plays.VersionBusy(pack.id, pack.activeVersion) ||
		m_downloads.IsBusyFor(pack.id);
	if (canUpdate && busy)
		ImGui::TextColored(Theme::C().error, "Close the game and wait for downloads or installs of this pack to finish first.");
	ImGui::Spacing();
	if (canUpdate) {
		ImGui::BeginDisabled(busy);
		if (ImGui::Button("Update")) {
			m_updates.Start(pack, target, check.removeOld);
			m_updates.CloseCheck();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
	} else if (!check.loading && !check.error.empty()) {
		if (ImGui::Button("Try again"))
			m_updates.StartCheck(pack);
		ImGui::SameLine();
	}
	if (ImGui::Button(canUpdate ? "Not now" : "Close")) {
		m_updates.CloseCheck();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}
