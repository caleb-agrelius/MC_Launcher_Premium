#pragma once

#include "Model/Modpack.h"
#include "Storage/InstallPlan.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class IHttpClient;

// One entry point per operation for every online pack source. Each source's own
// client (Modrinth.h, CurseForge.h, ...) does the work; these functions dispatch on
// the source so the UI doesn't need to know which API it is talking to.

// Search filters. Which ones a source honors is described by its SourceCapabilities.
struct SearchFilters {
	std::string gameVersion; // e.g. "1.20.1"; empty = any
	std::string loader;      // "forge" | "neoforge" | "fabric" | "quilt"; empty = any
	int sort = 0;            // index into SourceCapabilities::sorts
	int offset = 0;          // results to skip, for loading the next page (infinite scroll)
	std::string category;    // a Modrinth category key, e.g. "adventure"; empty = any
};

struct SourceCapabilities {
	bool gameVersionFilter = false;
	bool loaderFilter = false;
	bool categoryFilter = false;            // honors SearchFilters::category (Modrinth)
	std::vector<const char*> loaders;        // loader filter values, e.g. "fabric"
	std::vector<const char*> sorts;          // sort option labels; index 0 is the default
	bool changelogs = false;                 // FetchPackChangelog works
	bool installsPacks = false;              // true: "Install pack" runs an InstallPlan
	                                         // false: the per-file download/extract flow (Modrinth)
	const char* searchHint = "Search modpacks...";
	const char* browseLabel = "POPULAR";       // pack list heading when there's no query or filter
};

SourceCapabilities GetSourceCapabilities(PackSource source);

// Sources in sidebar order.
const std::vector<PackSource>& OnlineSources();

// False (with a reason) when a source can't be used, e.g. CurseForge without an API key.
bool IsSourceAvailable(PackSource source, std::string* reason = nullptr);

struct SourceSearchResult {
	std::vector<Modpack> packs; // filesLoaded == false; use FetchPackVersion
	std::uint64_t totalHits = 0;
};

bool SearchPacks(IHttpClient& http,
				 PackSource source,
				 const std::string& query,
				 const SearchFilters& filters,
				 int limit,
				 SourceSearchResult& out,
				 std::string* err = nullptr,
				 const std::atomic<bool>* cancel = nullptr);

// The version a pack installs by default (the newest release), plus details the search
// results didn't include. Empty strings / zero mean "keep what the pack already has".
struct PackVersionInfo {
	std::string versionNumber;
	std::string sourceVersionId;
	std::string versionType; // release, beta, alpha, recommended, latest; empty if unknown (version lists only)
	std::string published;   // YYYY-MM-DD; empty if unknown (version lists only)
	std::string gameVersion;
	std::string loaders;
	std::vector<PackFile> files; // Modrinth: the .mrpack to download
	std::string description;
	std::string author;
	std::uint64_t downloads = 0;
	std::string bannerUrl;
	std::string websiteUrl;
	std::string sourceUrl;       // Technic
	bool sourceIsSolder = false; // Technic
};

bool FetchPackVersion(IHttpClient& http,
					  const Modpack& pack,
					  PackVersionInfo& out,
					  std::string* err = nullptr,
					  const std::atomic<bool>* cancel = nullptr);

// Every version a pack has, newest first, for the version picker. Each entry can be
// passed to ApplyPackVersion. Entries may lack details the source only gives per
// version (e.g. a Technic Solder build's Minecraft version).
bool ListPackVersions(IHttpClient& http,
					  const Modpack& pack,
					  std::vector<PackVersionInfo>& out,
					  std::string* err = nullptr,
					  const std::atomic<bool>* cancel = nullptr);

// Copies a FetchPackVersion result onto the pack and marks its files as loaded.
void ApplyPackVersion(Modpack& pack, const PackVersionInfo& version);

// An optional mod the player can choose when installing a pack (ATLauncher).
struct OptionalMod {
	std::string name;
	std::string description;
	std::string group;                // mods in the same group are either/or
	std::vector<std::string> depends; // names of other optional mods it needs
	bool selected = false;            // the pack author's default
};

// The optional mods of the pack's version (pack.sourceVersionId). Sources without
// optional mods succeed with an empty list.
bool FetchOptionalMods(IHttpClient& http,
					   const Modpack& pack,
					   std::vector<OptionalMod>& out,
					   std::string* err = nullptr,
					   const std::atomic<bool>* cancel = nullptr);

// The pack's full description as plain text (see MarkupToPlainText). Technic and
// ATLauncher search results already hold it, so they make no request. Falls back to
// pack.description when the source has nothing longer.
bool FetchPackDescription(IHttpClient& http,
						  const Modpack& pack,
						  std::string& out,
						  std::string* err = nullptr,
						  const std::atomic<bool>* cancel = nullptr);

// The changelog of the pack's version (pack.sourceVersionId) as plain text. Only for
// sources whose capabilities have changelogs (Modrinth, CurseForge, FTB).
bool FetchPackChangelog(IHttpClient& http,
						const Modpack& pack,
						std::string& out,
						std::string* err = nullptr,
						const std::atomic<bool>* cancel = nullptr);

// One mod (or resource pack, shader pack...) a pack version includes, for Discover's Mods tab.
struct PackMod {
	std::string name;       // display name, e.g. "Sodium"
	std::string summary;    // one-line description, when the source has one
	std::string version;    // version or file name, when known
	std::string url;        // the mod's page, when known
	bool clientOnly = false; // the source says servers don't need it
	bool serverOnly = false;
	bool optional = false;
};

struct PackModList {
	std::vector<PackMod> mods; // sorted by name, ignoring case
	std::string note;          // e.g. why the list is empty or incomplete
};

// The mods in the pack's version (pack.sourceVersionId, so the pack's version must be
// loaded first). CurseForge downloads the pack zip into workDir to read its manifest; the
// install reuses it. Technic single-zip packs succeed with an empty list and a note.
bool FetchPackMods(IHttpClient& http,
				   const Modpack& pack,
				   const std::string& workDir,
				   PackModList& out,
				   std::string* err = nullptr,
				   const std::atomic<bool>* cancel = nullptr);

// Builds the install plan for the pack's chosen version (pack.sourceVersionId). May
// download the pack's own archive (e.g. a CurseForge zip) into workDir first.
// status reports what it is doing, for the UI.
bool BuildInstallPlan(IHttpClient& http,
					  const Modpack& pack,
					  const std::string& workDir,
					  InstallPlan& plan,
					  std::function<void(const std::string&)> status,
					  std::string* err = nullptr,
					  const std::atomic<bool>* cancel = nullptr);
