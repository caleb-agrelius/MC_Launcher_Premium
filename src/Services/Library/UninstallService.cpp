#include "Services/Library/UninstallService.h"

#include "Services/AppPaths.h"
#include "Util/Paths.h"
#include "Util/PathSafety.h"
#include "Util/SharedFiles.h"
#include "Util/TracedFs.h"

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

UninstallService::UninstallService(const Runtime& runtime, PackLibrary& library, DownloadService& downloads, PlayService& plays)
	: m_rt(runtime), m_library(library), m_downloads(downloads), m_plays(plays), m_scope(runtime.tasks)
{
}

void UninstallService::Uninstall(const Modpack& pack, const std::string& versionKey)
{
	const std::string packId = pack.id; // pack may be the library's own entry, which this erases from
	// The id names folders that get deleted, so it must be one plain path segment.
	if (!IsPlainFolderName(packId)) {
		m_rt.log << "Refusing to uninstall pack with id \"" << packId << "\"\n";
		m_rt.notify.Notify(NoticeKind::Error, "Couldn't uninstall " + pack.name, "Its id isn't a valid folder name.");
		return;
	}
	Library& packs = m_library.Packs();
	const auto entry = packs.find(packId);
	if (entry == packs.end())
		return;
	// Folders come from library.json: only delete ones inside installs/ or instances/
	// that belong to this pack.
	auto ownedFolder = [&](const std::string& dir) {
		const fs::path path = fs::u8path(dir).lexically_normal();
		if (path.empty() || path.is_absolute() || path.has_root_name())
			return false;
		auto part = path.begin();
		const std::string top = part->string();
		if (top != AppPaths::kInstallsRoot && top != AppPaths::kInstancesRoot)
			return false;
		if (++part == path.end())
			return false;
		const std::string name = part->string();
		return name == packId || name.rfind(packId + "@", 0) == 0;
	};
	std::vector<fs::path> folders;
	const bool wholePack = versionKey.empty();
	for (const InstalledVersion& v : entry->second.installedVersions) {
		if (!wholePack && v.key != versionKey)
			continue;
		for (const std::string& dir : { v.installDir, v.gameDir }) {
			if (ownedFolder(dir))
				folders.push_back(fs::u8path(dir));
			else
				m_rt.log << "Not deleting " << dir << ": outside this pack's folders\n";
		}
	}
	if (wholePack)
		folders.push_back(fs::path(AppPaths::kDownloadsRoot) / packId);

	std::string label = pack.name;
	if (wholePack) {
		packs.erase(entry);
	} else {
		if (const InstalledVersion* v = FindInstalledVersion(entry->second, versionKey))
			label += " " + v->version;
		if (!RemoveInstalledVersion(entry->second, versionKey))
			packs.erase(entry);
	}
	m_library.Save();
	m_downloads.Forget(packId, wholePack, versionKey);
	// Forget the removed versions' play state (all of them for the whole pack).
	m_plays.Forget(packId, wholePack, versionKey);
	m_rt.log << "Uninstall " << packId << (wholePack ? std::string(" (all versions)") : " " + versionKey) << "\n";
	m_scope.Run([this, folders, packId, label]() {
		std::string failed;
		for (const fs::path& dir : folders) {
			std::error_code ec;
			// Always the extended-length form: files deep inside can pass 260 characters even when dir doesn't.
			TracedFs::RemoveAll(ExtendedPath(fs::absolute(dir, ec)), ec);
			if (ec && failed.empty())
				failed = dir.string() + ": " + ec.message();
		}
		// Shared mod files only this pack used are now unused.
		SharedFiles::Prune();
		m_rt.ui.Post([this, packId, label, failed]() {
			if (failed.empty()) {
				m_rt.notify.Notify(NoticeKind::Success, "Uninstalled " + label, "Its files were deleted.");
			} else {
				m_rt.log << "Uninstall " << packId << ": couldn't delete " << failed << "\n";
				m_rt.notify.Notify(NoticeKind::Error, "Some files couldn't be deleted",
					failed + ". Close anything using them and delete the folder by hand.");
			}
		});
	});
}
