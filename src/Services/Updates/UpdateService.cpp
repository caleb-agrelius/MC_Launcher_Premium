#include "Services/Updates/UpdateService.h"

#include "Game/PackInstance.h"
#include "Network/PackSources.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace {

// The file an update downloads for per-file sources (Modrinth): the .mrpack.
const PackFile* UpdateFile(const Modpack& target)
{
	for (const PackFile& file : target.files) {
		if (file.name.size() > 7 && file.name.compare(file.name.size() - 7, 7, ".mrpack") == 0)
			return &file;
	}
	return target.files.empty() ? nullptr : &target.files.front();
}

}

UpdateService::UpdateService(const Runtime& runtime, PackLibrary& library, DownloadService& downloads, PlayService& plays,
	UninstallService& uninstaller)
	: m_rt(runtime), m_library(library), m_downloads(downloads), m_plays(plays), m_uninstaller(uninstaller),
	  m_scope(runtime.tasks)
{
	m_downloads.SetInstallObserver([this](const Modpack& pack, bool ok) { OnVersionInstalled(pack, ok); });
}

const PackUpdate* UpdateService::Running(const std::string& packId) const
{
	const auto it = m_updates.find(packId);
	return it != m_updates.end() ? &it->second : nullptr;
}

void UpdateService::StartCheck(const Modpack& pack)
{
	m_check = UpdateCheck{};
	m_check.packId = pack.id;
	m_check.loading = true;
	const int generation = ++m_checkGeneration;
	m_scope.Run([this, pack, generation]() {
		PackVersionInfo newest;
		std::string error;
		const bool ok = FetchPackVersion(m_rt.http, pack, newest, &error, &m_rt.shuttingDown);
		m_rt.ui.Post([this, pack, generation, ok, newest, error]() {
			if (generation != m_checkGeneration || m_check.packId != pack.id)
				return; // the dialog was closed or opened for another pack
			m_check.loading = false;
			if (!ok) {
				m_check.error = error;
				m_rt.log << "Update check for " << pack.id << " failed: " << error << "\n";
				return;
			}
			// The library pack, moved to the newest version. ATLauncher keeps its optional mod choices.
			Modpack target = pack;
			target.installedVersions.clear();
			target.activeVersion.clear();
			ApplyPackVersion(target, newest);
			m_check.target = target;
			m_rt.log << "Newest version of " << pack.id << ": " << target.version << "\n";
		});
	});
}

void UpdateService::Start(const Modpack& libraryPack, const Modpack& target, bool removeOld)
{
	const bool wholePack = GetSourceCapabilities(target.source).installsPacks;
	const PackFile* file = wholePack ? nullptr : UpdateFile(target);
	if (!wholePack && !file) {
		m_rt.notify.Notify(NoticeKind::Error, "Couldn't update " + libraryPack.name, "The new version has no file to install.");
		return;
	}
	m_updates[libraryPack.id] = { libraryPack.activeVersion, VersionKey(target), target.version, removeOld, false };
	m_rt.log << "Update " << libraryPack.id << " from " << libraryPack.activeVersion << " to " << VersionKey(target) << "\n";
	if (wholePack)
		m_downloads.StartPlanInstall(target);
	else
		m_downloads.StartDownloadInstall(target, *file);
	m_rt.notify.Notify(NoticeKind::Info, "Updating " + libraryPack.name + " to " + target.version,
		"Progress is on the Downloads page.");
}

void UpdateService::OnVersionInstalled(const Modpack& pack, bool ok)
{
	const auto running = m_updates.find(pack.id);
	if (running == m_updates.end() || running->second.copying || running->second.toKey != VersionKey(pack))
		return;
	if (!ok) {
		m_updates.erase(running); // the install already reported its error
		return;
	}
	const Modpack* entry = m_library.Find(pack.id);
	const InstalledVersion* from = entry ? FindInstalledVersion(*entry, running->second.fromKey) : nullptr;
	const InstalledVersion* to = entry ? FindInstalledVersion(*entry, running->second.toKey) : nullptr;
	if (!from || !to || from->key == to->key) {
		m_updates.erase(running);
		return;
	}
	running->second.copying = true;
	const fs::path fromDir = fs::absolute(fs::u8path(from->gameDir));
	const fs::path toDir = fs::absolute(fs::u8path(to->gameDir));
	m_scope.Run([this, packId = pack.id, fromDir, toDir]() {
		std::size_t copied = 0;
		std::string error;
		const bool copiedOk = CarryOverPlayerData(fromDir, toDir, copied, &error, &m_rt.shuttingDown);
		m_rt.ui.Post([this, packId, copiedOk, copied, error]() {
			const auto running = m_updates.find(packId);
			if (running == m_updates.end())
				return;
			const PackUpdate update = running->second;
			m_updates.erase(running);
			const Modpack* entry = m_library.Find(packId);
			if (!entry)
				return;
			const std::string name = entry->name;
			m_rt.log << "Update " << packId << ": copied " << copied << " file(s) of player data"
				<< (copiedOk ? std::string() : ", then failed: " + error) << "\n";
			if (!copiedOk) {
				m_rt.notify.Notify(NoticeKind::Error, "Couldn't copy your worlds and settings",
					name + " " + update.toVersion + " is installed, but " + error + ". The old version was kept.");
				return;
			}
			const bool playing = m_plays.VersionBusy(packId, update.fromKey);
			if (update.removeOld && !playing && FindInstalledVersion(*entry, update.fromKey)) {
				m_uninstaller.Uninstall(*entry, update.fromKey);
				m_rt.notify.Notify(NoticeKind::Success, "Updated " + name + " to " + update.toVersion,
					"Your worlds and settings came along; the old version is being removed.");
			} else {
				m_rt.notify.Notify(NoticeKind::Success, "Updated " + name + " to " + update.toVersion,
					update.removeOld ? "Your worlds and settings came along. The old version was kept because the game was running."
					: "Your worlds and settings came along. The old version is still installed.");
			}
		});
	});
}
