#include "UI/Dialogs/UninstallDialog.h"

#include "Services/AppPaths.h"
#include "Services/Downloads/DownloadService.h"
#include "Services/Library/PackLibrary.h"
#include "Services/Library/UninstallService.h"
#include "Services/Play/PlayService.h"
#include "Services/Servers/ServerService.h"
#include "Services/Settings/SettingsStore.h"
#include "Services/Updates/UpdateService.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"

#include <filesystem>

using Theme::S;
namespace fs = std::filesystem;

void UninstallDialog::Open(const std::string& packId, const std::string& versionKey)
{
	m_packId = packId;
	m_versionKey = versionKey;
	m_deleteServer = false;
}

void UninstallDialog::Draw()
{
	if (m_packId.empty())
		return;
	const Modpack* found = m_library.Find(m_packId);
	if (!found) {
		m_packId.clear();
		return;
	}
	ImGui::OpenPopup("Uninstall pack?");
	ImGui::SetNextWindowSize(ImVec2(S(460.0f), 0.0f));
	if (!ImGui::BeginPopupModal("Uninstall pack?", nullptr, ImGuiWindowFlags_NoResize))
		return;
	const Modpack pack = *found;
	const InstalledVersion* one = m_versionKey.empty() ? nullptr : FindInstalledVersion(pack, m_versionKey);
	if (one) {
		ImGui::TextWrapped("Version %s of %s will be removed and its files deleted, including worlds in "
			"its saves folder. Other versions stay. This can't be undone.", one->version.c_str(), pack.name.c_str());
		ImGui::TextDisabled("%s", one->gameDir.c_str());
	} else {
		ImGui::TextWrapped("%s will be removed from My Library and the files of %s deleted, including "
			"worlds in their saves folders. This can't be undone.", pack.name.c_str(),
			pack.installedVersions.size() > 1 ? "all its versions" : "the pack");
		for (const InstalledVersion& v : pack.installedVersions)
			ImGui::TextDisabled("%s", v.gameDir.c_str());
	}
	ImGui::Spacing();
	bool busy = m_updates.Running(pack.id) != nullptr || m_plays.VersionBusy(pack.id, one ? one->key : std::string()) ||
		m_downloads.IsBusyFor(pack.id);
	const fs::path packServerDir = fs::path(AppPaths::kServersRoot) / pack.id;
	std::error_code serverDirError;
	// One server per pack: offered only when the whole pack goes.
	const bool removesPack = !one || pack.installedVersions.size() <= 1;
	if (removesPack && fs::is_directory(packServerDir, serverDirError)) {
		Widgets::ToggleRow("Also delete its server", &m_deleteServer);
		ImGui::SetItemTooltip("Deletes %s, including the server's world. Otherwise the server is kept "
			"and still listed on the Servers page.", packServerDir.string().c_str());
		if (m_deleteServer)
			busy = busy || m_servers.IsRunningOrBusy(pack.id);
	} else {
		m_deleteServer = false;
	}
	if (busy)
		ImGui::TextColored(Theme::C().error, m_deleteServer
			? "Close the game, stop the server, and wait for downloads, installs and server setup to finish first."
			: "Close the game and wait for downloads or installs of this pack to finish first.");
	ImGui::BeginDisabled(busy);
	if (Widgets::DangerButton("Uninstall")) {
		const bool lastVersion = !one || pack.installedVersions.size() <= 1;
		if (lastVersion && m_settings.Get().packLaunch.erase(pack.id) > 0)
			m_settings.MarkDirty();
		m_uninstaller.Uninstall(pack, one ? m_versionKey : std::string());
		// The server goes only with the pack's last version.
		if (m_deleteServer && lastVersion)
			m_servers.StartDelete(pack.id, pack.name);
		m_packId.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Keep it")) {
		m_packId.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}
