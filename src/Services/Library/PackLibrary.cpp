#include "Services/Library/PackLibrary.h"

#include "Network/PackSources.h"
#include "Services/AppPaths.h"

#include <utility>
#include <vector>

namespace fs = std::filesystem;

PackLibrary::PackLibrary(std::string file, std::ostream& log)
	: m_file(std::move(file)), m_log(log), m_packs(LoadPackLibrary(m_file))
{
	DropMissingVersions();
	m_log << "Loaded " << m_packs.size() << " library pack(s)\n";
}

void PackLibrary::DropMissingVersions()
{
	for (auto it = m_packs.begin(); it != m_packs.end();) {
		Modpack& pack = it->second;
		std::vector<std::string> missing;
		for (const InstalledVersion& v : pack.installedVersions) {
			std::error_code existsError;
			if (!fs::is_directory(v.installDir, existsError))
				missing.push_back(v.key);
		}
		bool keep = !pack.installedVersions.empty();
		for (const std::string& key : missing)
			keep = RemoveInstalledVersion(pack, key);
		if (keep)
			++it;
		else
			it = m_packs.erase(it);
	}
}

Modpack* PackLibrary::Find(const std::string& packId)
{
	const auto it = m_packs.find(packId);
	return it != m_packs.end() ? &it->second : nullptr;
}

const Modpack* PackLibrary::Find(const std::string& packId) const
{
	const auto it = m_packs.find(packId);
	return it != m_packs.end() ? &it->second : nullptr;
}

void PackLibrary::Save()
{
	if (!SavePackLibrary(m_file, m_packs))
		m_log << "Could not save " << m_file << "\n";
}

const InstalledVersion* PackLibrary::ActiveVersionOf(const Modpack& pack) const
{
	const Modpack* entry = Find(pack.id);
	return entry ? ActiveInstalledVersion(*entry) : nullptr;
}

fs::path PackLibrary::InstallDir(const Modpack& pack) const
{
	const InstalledVersion* v = ActiveVersionOf(pack);
	return v ? fs::u8path(v->installDir) : fs::path(AppPaths::kInstallsRoot) / pack.id;
}

fs::path PackLibrary::GameDir(const Modpack& pack) const
{
	if (const InstalledVersion* v = ActiveVersionOf(pack))
		return fs::u8path(v->gameDir);
	return GetSourceCapabilities(pack.source).installsPacks ? fs::path(AppPaths::kInstallsRoot) / pack.id / "minecraft"
		: fs::path(AppPaths::kInstancesRoot) / pack.id;
}

ContentChanges PackLibrary::ContentChangesOf(const Modpack& pack) const
{
	const InstalledVersion* v = ActiveVersionOf(pack);
	return v ? v->contentChanges : ContentChanges();
}
