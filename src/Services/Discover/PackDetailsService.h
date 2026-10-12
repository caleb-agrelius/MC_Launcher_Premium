#pragma once

#include "Core/Runtime.h"
#include "Core/TaskScope.h"
#include "Model/Modpack.h"
#include "Network/PackSources.h"
#include "Services/Discover/DiscoverModel.h"

#include <map>
#include <set>
#include <string>
#include <vector>

// Every version of a pack, for the version dropdowns.
struct VersionList {
	bool loading = false;
	bool loaded = false;
	std::string error;
	std::vector<PackVersionInfo> versions;
};

// The lookups behind Discover's details pane and the version dropdowns: a pack's latest version,
// its banner, description and mod list, and all its versions. Each is fetched on a worker the
// first time it's needed and cached by pack. UI thread only; results arrive through the UI queue.
class PackDetailsService {
public:
	explicit PackDetailsService(const Runtime& runtime);

	// ---- Latest version of an online result (view.packs entries are filled in when it arrives) ----
	bool FilesLoading(const std::string& packId) const { return m_filesLoading.count(packId) > 0; }
	// The last lookup's error for the pack, or null.
	const std::string* FilesError(const std::string& packId) const;
	void FetchFiles(SourceView& view, const Modpack& pack);

	// ---- Full-size banner (Modrinth) ----
	bool BannerLoading(const std::string& packId) const { return m_bannersLoading.count(packId) > 0; }
	void FetchBanner(SourceView& view, const std::string& packId);

	// ---- Description, as plain text ----
	const std::string* Description(const std::string& packId) const;
	bool DescriptionLoading(const std::string& packId) const { return m_descriptionsLoading.count(packId) > 0; }
	const std::string* DescriptionError(const std::string& packId) const;
	void FetchDescription(const Modpack& pack);

	// ---- Mod list, keyed by ModsKey (pack id and version) ----
	static std::string ModsKey(const Modpack& pack) { return pack.id + "@" + pack.sourceVersionId; }
	const PackModList* Mods(const std::string& key) const;
	bool ModsLoading(const std::string& key) const { return m_modsLoading.count(key) > 0; }
	const std::string* ModsError(const std::string& key) const;
	void FetchMods(const Modpack& pack);

	// ---- All versions of a pack ----
	// The pack's list (created empty if it has none yet).
	VersionList& Versions(const std::string& packId) { return m_versionLists[packId]; }
	void ListVersions(const Modpack& pack);

private:
	Runtime m_rt;
	std::set<std::string> m_filesLoading;
	std::map<std::string, std::string> m_filesErrors;
	std::set<std::string> m_bannersLoading;
	std::map<std::string, PackModList> m_packMods;
	std::set<std::string> m_modsLoading;
	std::map<std::string, std::string> m_modsErrors;
	std::map<std::string, std::string> m_packDescriptions;
	std::set<std::string> m_descriptionsLoading;
	std::map<std::string, std::string> m_descriptionErrors;
	std::map<std::string, VersionList> m_versionLists;
	TaskScope m_scope; // last, so its tasks finish before the members above go away
};
