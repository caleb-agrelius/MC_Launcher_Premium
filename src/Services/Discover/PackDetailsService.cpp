#include "Services/Discover/PackDetailsService.h"

#include "Network/Modrinth.h"
#include "Services/AppPaths.h"

#include <filesystem>

namespace fs = std::filesystem;

PackDetailsService::PackDetailsService(const Runtime& runtime)
	: m_rt(runtime), m_scope(runtime.tasks)
{
}

const std::string* PackDetailsService::FilesError(const std::string& packId) const
{
	const auto it = m_filesErrors.find(packId);
	return it != m_filesErrors.end() ? &it->second : nullptr;
}

const std::string* PackDetailsService::Description(const std::string& packId) const
{
	const auto it = m_packDescriptions.find(packId);
	return it != m_packDescriptions.end() ? &it->second : nullptr;
}

const std::string* PackDetailsService::DescriptionError(const std::string& packId) const
{
	const auto it = m_descriptionErrors.find(packId);
	return it != m_descriptionErrors.end() ? &it->second : nullptr;
}

const PackModList* PackDetailsService::Mods(const std::string& key) const
{
	const auto it = m_packMods.find(key);
	return it != m_packMods.end() ? &it->second : nullptr;
}

const std::string* PackDetailsService::ModsError(const std::string& key) const
{
	const auto it = m_modsErrors.find(key);
	return it != m_modsErrors.end() ? &it->second : nullptr;
}

void PackDetailsService::FetchFiles(SourceView& view, const Modpack& pack)
{
	const std::string packId = pack.id;
	m_filesLoading.insert(packId);
	m_filesErrors.erase(packId);
	m_scope.Run([this, &view, pack, packId]() {
		PackVersionInfo version;
		std::string error;
		const bool ok = FetchPackVersion(m_rt.http, pack, version, &error, &m_rt.shuttingDown);
		m_rt.ui.Post([this, &view, packId, ok, version, error]() {
			m_filesLoading.erase(packId);
			if (!ok) {
				m_rt.log << "Version lookup for " << packId << " failed: " << error << "\n";
				m_filesErrors[packId] = error;
				return;
			}
			m_rt.log << "Version lookup for " << packId << ": " << version.versionNumber << " ("
				<< (version.files.empty() ? "id " + version.sourceVersionId : version.files.front().name) << ")\n";
			for (Modpack& item : view.packs) {
				if (item.id == packId)
					ApplyPackVersion(item, version);
			}
		});
	});
}

void PackDetailsService::FetchBanner(SourceView& view, const std::string& packId)
{
	m_bannersLoading.insert(packId);
	m_scope.Run([this, &view, packId]() {
		std::string url;
		std::string error;
		const bool ok = FetchModrinthBanner(m_rt.http, packId, url, &error, &m_rt.shuttingDown);
		m_rt.ui.Post([this, &view, packId, ok, url, error]() {
			m_bannersLoading.erase(packId);
			if (!ok)
				m_rt.log << "Banner lookup for " << packId << " failed: " << error << "\n";
			for (Modpack& pack : view.packs) {
				if (pack.id != packId)
					continue;
				pack.bannerHiResUrl = url;
				pack.bannerLoaded = true; // also on failure: keep the thumbnail, don't retry
			}
		});
	});
}

void PackDetailsService::FetchDescription(const Modpack& pack)
{
	m_descriptionsLoading.insert(pack.id);
	m_descriptionErrors.erase(pack.id);
	m_scope.Run([this, pack]() {
		std::string text, error;
		const bool ok = FetchPackDescription(m_rt.http, pack, text, &error, &m_rt.shuttingDown);
		m_rt.ui.Post([this, packId = pack.id, ok, text, error]() {
			m_descriptionsLoading.erase(packId);
			if (ok) {
				m_packDescriptions[packId] = text;
			} else {
				m_rt.log << "Description for " << packId << " failed: " << error << "\n";
				m_descriptionErrors[packId] = error;
			}
		});
	});
}

void PackDetailsService::FetchMods(const Modpack& pack)
{
	const std::string key = ModsKey(pack);
	m_modsLoading.insert(key);
	m_modsErrors.erase(key);
	// CurseForge reads the pack zip's manifest; it lands where the install looks for it.
	const std::string workDir = (fs::path(AppPaths::kDownloadsRoot) / pack.id).string();
	m_scope.Run([this, pack, key, workDir]() {
		PackModList list;
		std::string error;
		const bool ok = FetchPackMods(m_rt.http, pack, workDir, list, &error, &m_rt.shuttingDown);
		m_rt.ui.Post([this, key, packId = pack.id, ok, list, error]() {
			m_modsLoading.erase(key);
			if (ok) {
				m_rt.log << "Mod list for " << packId << ": " << list.mods.size() << " mod(s)\n";
				m_packMods[key] = list;
			} else {
				m_rt.log << "Mod list for " << packId << " failed: " << error << "\n";
				m_modsErrors[key] = error;
			}
		});
	});
}

void PackDetailsService::ListVersions(const Modpack& pack)
{
	VersionList& list = m_versionLists[pack.id];
	list.loading = true;
	list.error.clear();
	m_scope.Run([this, pack]() {
		std::vector<PackVersionInfo> versions;
		std::string error;
		const bool ok = ListPackVersions(m_rt.http, pack, versions, &error, &m_rt.shuttingDown);
		m_rt.ui.Post([this, id = pack.id, ok, versions, error]() {
			VersionList& list = m_versionLists[id];
			list.loading = false;
			list.loaded = ok;
			list.error = ok ? std::string() : error;
			list.versions = versions;
			m_rt.log << "Versions of " << id << ": " << (ok ? std::to_string(versions.size()) : "failed: " + error) << "\n";
		});
	});
}
