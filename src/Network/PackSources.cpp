#include "PackSources.h"

#include "AtLauncher.h"
#include "CurseForge.h"
#include "Ftb.h"
#include "Technic.h"
#include "HttpClient.h"
#include "Modrinth.h"
#include "Util/Text.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <utility>

namespace {

const char* kAllLoaders[] = { "forge", "neoforge", "fabric", "quilt" };

// Labels and the matching Modrinth "index" values. "Best match" keeps the default
// (downloads for an empty query, relevance otherwise).
const char* kModrinthSorts[] = { "Best match", "Downloads", "Follows", "Newest", "Recently updated" };
const char* kModrinthIndexes[] = { "", "downloads", "follows", "newest", "updated" };

// CurseForge.cpp maps these to its sortField numbers, in this order.
const char* kCurseForgeSorts[] = { "Best match", "Popularity", "Total downloads", "Last updated", "Name" };

// Ftb.cpp sorts locally in this order. "Featured" puts featured packs first, then most installed.
const char* kFtbSorts[] = { "Featured", "Most played", "Recently updated", "Name" };

// Technic.cpp: index 0 keeps the API's order (trending, or its search ranking).
const char* kTechnicSorts[] = { "Trending", "Most installed" };

// AtLauncher.cpp: "Featured" = featured packs first, then ATLauncher's own ordering.
const char* kAtLauncherSorts[] = { "Featured", "Name" };

// One modpack source. Each source overrides what it supports; the base class answers for
// sources that don't (the local index and packs the player made), with an error naming the
// source. The public functions below pick the client and do the work every source shares.
class PackSourceClient {
public:
	explicit PackSourceClient(PackSource source) : m_source(source) {}
	virtual ~PackSourceClient() = default;

	virtual SourceCapabilities Capabilities() const
	{
		SourceCapabilities caps;
		caps.sorts = { "Default" };
		return caps;
	}
	virtual bool Available(std::string* /*reason*/) const { return true; }
	virtual bool Search(HttpClient&, const std::string& /*query*/, const SearchFilters&, int /*limit*/, SourceSearchResult&,
						std::string* err, const std::atomic<bool>*) const
	{
		return Unsupported(err, " search isn't supported");
	}
	virtual bool LatestVersion(HttpClient&, const Modpack&, PackVersionInfo&, std::string* err, const std::atomic<bool>*) const
	{
		return Unsupported(err, " versions aren't supported");
	}
	virtual bool ListVersions(HttpClient&, const Modpack&, std::vector<PackVersionInfo>&, std::string* err,
							  const std::atomic<bool>*) const
	{
		return Unsupported(err, " packs have only one version");
	}
	virtual bool OptionalMods(HttpClient&, const Modpack&, std::vector<OptionalMod>&, std::string*, const std::atomic<bool>*) const
	{
		return true; // none
	}
	// The full description as Markdown or HTML; empty means the pack's summary is all there is.
	virtual bool DescriptionMarkup(HttpClient&, const Modpack&, std::string&, std::string*, const std::atomic<bool>*) const
	{
		return true;
	}
	virtual bool ChangelogMarkup(HttpClient&, const Modpack&, std::string&, std::string* err, const std::atomic<bool>*) const
	{
		return Unsupported(err, " doesn't publish changelogs");
	}
	virtual bool Mods(HttpClient&, const Modpack&, const std::string& /*workDir*/, PackModList&, std::string* err,
					  const std::atomic<bool>*) const
	{
		return Unsupported(err, " packs don't list their mods");
	}
	virtual bool Plan(HttpClient&, const Modpack&, const std::string& /*workDir*/, InstallPlan&,
					  std::function<void(const std::string&)>, std::string* err, const std::atomic<bool>*) const
	{
		return Unsupported(err, " packs can't be installed this way");
	}

protected:
	bool Unsupported(std::string* err, const char* what) const
	{
		if (err) *err = std::string(PackSourceName(m_source)) + what;
		return false;
	}
	// Capabilities every online source has: version filtering and its sorts and labels.
	static SourceCapabilities OnlineCapabilities(const char* const* sorts, std::size_t sortCount, const char* searchHint,
												 const char* browseLabel)
	{
		SourceCapabilities caps;
		caps.gameVersionFilter = true;
		caps.sorts.assign(sorts, sorts + sortCount);
		caps.searchHint = searchHint;
		caps.browseLabel = browseLabel;
		return caps;
	}
	static void AllLoaders(SourceCapabilities& caps)
	{
		caps.loaderFilter = true;
		caps.loaders.assign(std::begin(kAllLoaders), std::end(kAllLoaders));
	}

private:
	PackSource m_source;
};

PackVersionInfo FromModrinth(ModrinthVersion&& version)
{
	PackVersionInfo info;
	info.versionNumber = version.versionNumber;
	info.sourceVersionId = version.id;
	info.versionType = version.versionType;
	info.published = version.published;
	info.gameVersion = version.gameVersion;
	info.loaders = version.loaders;
	info.files = std::move(version.files);
	return info;
}

std::string ModrinthProject(const Modpack& pack)
{
	return pack.sourceId.empty() ? pack.id : pack.sourceId;
}

class ModrinthSource : public PackSourceClient {
public:
	ModrinthSource() : PackSourceClient(PackSource::Modrinth) {}
	SourceCapabilities Capabilities() const override
	{
		SourceCapabilities caps = OnlineCapabilities(kModrinthSorts, std::size(kModrinthSorts), "Search Modrinth modpacks...",
			"MOST DOWNLOADED");
		AllLoaders(caps);
		caps.changelogs = true;
		caps.categoryFilter = true;
		return caps;
	}
	bool Search(HttpClient& http, const std::string& query, const SearchFilters& filters, int limit, SourceSearchResult& out,
				std::string* err, const std::atomic<bool>* cancel) const override
	{
		ModrinthSearchOptions options;
		options.gameVersion = filters.gameVersion;
		options.loader = filters.loader;
		options.offset = filters.offset;
		options.category = filters.category;
		if (filters.sort > 0 && filters.sort < static_cast<int>(std::size(kModrinthIndexes)))
			options.index = kModrinthIndexes[filters.sort];
		ModrinthSearchResult result;
		if (!SearchModrinthModpacks(http, query, options, limit, result, err, cancel))
			return false;
		out.packs = std::move(result.packs);
		out.totalHits = result.totalHits;
		return true;
	}
	bool LatestVersion(HttpClient& http, const Modpack& pack, PackVersionInfo& out, std::string* err,
					   const std::atomic<bool>* cancel) const override
	{
		ModrinthVersion version;
		if (!FetchLatestModrinthVersion(http, ModrinthProject(pack), version, err, cancel))
			return false;
		out = FromModrinth(std::move(version));
		return true;
	}
	bool ListVersions(HttpClient& http, const Modpack& pack, std::vector<PackVersionInfo>& out, std::string* err,
					  const std::atomic<bool>* cancel) const override
	{
		std::vector<ModrinthVersion> versions;
		if (!ListModrinthVersions(http, ModrinthProject(pack), versions, err, cancel))
			return false;
		for (ModrinthVersion& version : versions)
			out.push_back(FromModrinth(std::move(version)));
		return true;
	}
	bool DescriptionMarkup(HttpClient& http, const Modpack& pack, std::string& out, std::string* err,
						   const std::atomic<bool>* cancel) const override
	{
		return FetchModrinthDescription(http, ModrinthProject(pack), out, err, cancel);
	}
	bool ChangelogMarkup(HttpClient& http, const Modpack& pack, std::string& out, std::string* err,
						 const std::atomic<bool>* cancel) const override
	{
		return FetchModrinthChangelog(http, ModrinthProject(pack), pack.sourceVersionId, pack.version, out, err, cancel);
	}
	bool Mods(HttpClient& http, const Modpack& pack, const std::string&, PackModList& out, std::string* err,
			  const std::atomic<bool>* cancel) const override
	{
		if (!FetchModrinthPackMods(http, pack.sourceVersionId, out.mods, err, cancel))
			return false;
		out.note = "Only mods hosted on Modrinth are listed.";
		return true;
	}
	// .mrpack packs install through the per-file download and extract flow, not a plan.
};

class CurseForgeSource : public PackSourceClient {
public:
	CurseForgeSource() : PackSourceClient(PackSource::CurseForge) {}
	SourceCapabilities Capabilities() const override
	{
		SourceCapabilities caps = OnlineCapabilities(kCurseForgeSorts, std::size(kCurseForgeSorts), "Search CurseForge modpacks...",
			"MOST DOWNLOADED");
		AllLoaders(caps);
		caps.installsPacks = true;
		caps.changelogs = true;
		return caps;
	}
	bool Available(std::string* reason) const override
	{
		if (!CurseForgeApiKey().empty())
			return true;
		if (reason)
			*reason = "CurseForge needs an API key. Add one in Settings > Downloads, or set the "
				"MCLP_CURSEFORGE_API_KEY environment variable (see README.md).";
		return false;
	}
	bool Search(HttpClient& http, const std::string& query, const SearchFilters& filters, int limit, SourceSearchResult& out,
				std::string* err, const std::atomic<bool>* cancel) const override
	{
		return SearchCurseForgeModpacks(http, query, filters, limit, out, err, cancel);
	}
	bool LatestVersion(HttpClient& http, const Modpack& pack, PackVersionInfo& out, std::string* err,
					   const std::atomic<bool>* cancel) const override
	{
		return FetchCurseForgeVersion(http, pack, out, err, cancel);
	}
	bool ListVersions(HttpClient& http, const Modpack& pack, std::vector<PackVersionInfo>& out, std::string* err,
					  const std::atomic<bool>* cancel) const override
	{
		return ListCurseForgeVersions(http, pack, out, err, cancel);
	}
	bool DescriptionMarkup(HttpClient& http, const Modpack& pack, std::string& out, std::string* err,
						   const std::atomic<bool>* cancel) const override
	{
		return FetchCurseForgeDescription(http, pack, out, err, cancel);
	}
	bool ChangelogMarkup(HttpClient& http, const Modpack& pack, std::string& out, std::string* err,
						 const std::atomic<bool>* cancel) const override
	{
		return FetchCurseForgeChangelog(http, pack, out, err, cancel);
	}
	bool Mods(HttpClient& http, const Modpack& pack, const std::string& workDir, PackModList& out, std::string* err,
			  const std::atomic<bool>* cancel) const override
	{
		return FetchCurseForgePackMods(http, pack, workDir, out.mods, err, cancel);
	}
	bool Plan(HttpClient& http, const Modpack& pack, const std::string& workDir, InstallPlan& plan,
			  std::function<void(const std::string&)> status, std::string* err, const std::atomic<bool>* cancel) const override
	{
		return BuildCurseForgePlan(http, pack, workDir, plan, std::move(status), err, cancel);
	}
};

class FtbSource : public PackSourceClient {
public:
	FtbSource() : PackSourceClient(PackSource::FTB) {}
	SourceCapabilities Capabilities() const override
	{
		SourceCapabilities caps = OnlineCapabilities(kFtbSorts, std::size(kFtbSorts), "Search FTB modpacks...", "FEATURED");
		AllLoaders(caps);
		caps.installsPacks = true;
		caps.changelogs = true;
		return caps;
	}
	bool Search(HttpClient& http, const std::string& query, const SearchFilters& filters, int limit, SourceSearchResult& out,
				std::string* err, const std::atomic<bool>* cancel) const override
	{
		return SearchFtbModpacks(http, query, filters, limit, out, err, cancel);
	}
	bool LatestVersion(HttpClient& http, const Modpack& pack, PackVersionInfo& out, std::string* err,
					   const std::atomic<bool>* cancel) const override
	{
		return FetchFtbVersion(http, pack, out, err, cancel);
	}
	bool ListVersions(HttpClient& http, const Modpack& pack, std::vector<PackVersionInfo>& out, std::string* err,
					  const std::atomic<bool>* cancel) const override
	{
		return ListFtbVersions(http, pack, out, err, cancel);
	}
	bool DescriptionMarkup(HttpClient& http, const Modpack& pack, std::string& out, std::string* err,
						   const std::atomic<bool>* cancel) const override
	{
		return FetchFtbDescription(http, pack, out, err, cancel);
	}
	bool ChangelogMarkup(HttpClient& http, const Modpack& pack, std::string& out, std::string* err,
						 const std::atomic<bool>* cancel) const override
	{
		return FetchFtbChangelog(http, pack, out, err, cancel);
	}
	bool Mods(HttpClient& http, const Modpack& pack, const std::string&, PackModList& out, std::string* err,
			  const std::atomic<bool>* cancel) const override
	{
		return FetchFtbPackMods(http, pack, out.mods, err, cancel);
	}
	bool Plan(HttpClient& http, const Modpack& pack, const std::string&, InstallPlan& plan,
			  std::function<void(const std::string&)> status, std::string* err, const std::atomic<bool>* cancel) const override
	{
		return BuildFtbPlan(http, pack, plan, std::move(status), err, cancel);
	}
};

class TechnicSource : public PackSourceClient {
public:
	TechnicSource() : PackSourceClient(PackSource::Technic) {}
	SourceCapabilities Capabilities() const override
	{
		// The game version filter is checked against each result's details; there's no loader info.
		SourceCapabilities caps = OnlineCapabilities(kTechnicSorts, std::size(kTechnicSorts), "Search Technic modpacks...", "TRENDING");
		caps.installsPacks = true;
		return caps;
	}
	bool Search(HttpClient& http, const std::string& query, const SearchFilters& filters, int limit, SourceSearchResult& out,
				std::string* err, const std::atomic<bool>* cancel) const override
	{
		return SearchTechnicModpacks(http, query, filters, limit, out, err, cancel);
	}
	bool LatestVersion(HttpClient& http, const Modpack& pack, PackVersionInfo& out, std::string* err,
					   const std::atomic<bool>* cancel) const override
	{
		return FetchTechnicVersion(http, pack, out, err, cancel);
	}
	bool ListVersions(HttpClient& http, const Modpack& pack, std::vector<PackVersionInfo>& out, std::string* err,
					  const std::atomic<bool>* cancel) const override
	{
		return ListTechnicVersions(http, pack, out, err, cancel);
	}
	bool Mods(HttpClient& http, const Modpack& pack, const std::string&, PackModList& out, std::string* err,
			  const std::atomic<bool>* cancel) const override
	{
		if (!FetchTechnicPackMods(http, pack, out.mods, err, cancel))
			return false;
		if (!pack.sourceIsSolder)
			out.note = "This pack is a single archive, so its mods aren't known until it's installed.";
		return true;
	}
	bool Plan(HttpClient& http, const Modpack& pack, const std::string&, InstallPlan& plan,
			  std::function<void(const std::string&)> status, std::string* err, const std::atomic<bool>* cancel) const override
	{
		return BuildTechnicPlan(http, pack, plan, std::move(status), err, cancel);
	}
};

class AtLauncherSource : public PackSourceClient {
public:
	AtLauncherSource() : PackSourceClient(PackSource::ATLauncher) {}
	SourceCapabilities Capabilities() const override
	{
		// The pack list has no loader info.
		SourceCapabilities caps = OnlineCapabilities(kAtLauncherSorts, std::size(kAtLauncherSorts), "Search ATLauncher modpacks...",
			"FEATURED");
		caps.installsPacks = true;
		return caps;
	}
	bool Search(HttpClient& http, const std::string& query, const SearchFilters& filters, int limit, SourceSearchResult& out,
				std::string* err, const std::atomic<bool>* cancel) const override
	{
		return SearchAtLauncherModpacks(http, query, filters, limit, out, err, cancel);
	}
	bool LatestVersion(HttpClient& http, const Modpack& pack, PackVersionInfo& out, std::string* err,
					   const std::atomic<bool>* cancel) const override
	{
		return FetchAtLauncherVersion(http, pack, out, err, cancel);
	}
	bool ListVersions(HttpClient& http, const Modpack& pack, std::vector<PackVersionInfo>& out, std::string* err,
					  const std::atomic<bool>* cancel) const override
	{
		return ListAtLauncherVersions(http, pack, out, err, cancel);
	}
	bool OptionalMods(HttpClient& http, const Modpack& pack, std::vector<OptionalMod>& out, std::string* err,
					  const std::atomic<bool>* cancel) const override
	{
		return FetchAtLauncherOptionalMods(http, pack, out, err, cancel);
	}
	bool Mods(HttpClient& http, const Modpack& pack, const std::string&, PackModList& out, std::string* err,
			  const std::atomic<bool>* cancel) const override
	{
		return FetchAtLauncherPackMods(http, pack, out.mods, err, cancel);
	}
	bool Plan(HttpClient& http, const Modpack& pack, const std::string& workDir, InstallPlan& plan,
			  std::function<void(const std::string&)> status, std::string* err, const std::atomic<bool>* cancel) const override
	{
		return BuildAtLauncherPlan(http, pack, workDir, plan, std::move(status), err, cancel);
	}
};

// The client for a source; sources without one (the local index, packs the player made)
// get the base class.
const PackSourceClient& Client(PackSource source)
{
	static const ModrinthSource modrinth;
	static const CurseForgeSource curseForge;
	static const FtbSource ftb;
	static const TechnicSource technic;
	static const AtLauncherSource atLauncher;
	static const PackSourceClient local(PackSource::LocalIndex);
	static const PackSourceClient custom(PackSource::Custom);
	switch (source) {
	case PackSource::Modrinth: return modrinth;
	case PackSource::CurseForge: return curseForge;
	case PackSource::FTB: return ftb;
	case PackSource::Technic: return technic;
	case PackSource::ATLauncher: return atLauncher;
	case PackSource::Custom: return custom;
	case PackSource::LocalIndex: break;
	}
	return local;
}

}

SourceCapabilities GetSourceCapabilities(PackSource source)
{
	return Client(source).Capabilities();
}

const std::vector<PackSource>& OnlineSources()
{
	static const std::vector<PackSource> sources = {
		PackSource::Modrinth,
		PackSource::CurseForge,
		PackSource::FTB,
		PackSource::Technic,
		PackSource::ATLauncher,
	};
	return sources;
}

bool IsSourceAvailable(PackSource source, std::string* reason)
{
	return Client(source).Available(reason);
}

bool SearchPacks(HttpClient& http,
				 PackSource source,
				 const std::string& query,
				 const SearchFilters& filters,
				 int limit,
				 SourceSearchResult& out,
				 std::string* err,
				 const std::atomic<bool>* cancel)
{
	out = {};
	return Client(source).Search(http, query, filters, limit, out, err, cancel);
}

bool FetchPackVersion(HttpClient& http,
					  const Modpack& pack,
					  PackVersionInfo& out,
					  std::string* err,
					  const std::atomic<bool>* cancel)
{
	out = {};
	return Client(pack.source).LatestVersion(http, pack, out, err, cancel);
}

bool ListPackVersions(HttpClient& http,
					  const Modpack& pack,
					  std::vector<PackVersionInfo>& out,
					  std::string* err,
					  const std::atomic<bool>* cancel)
{
	out.clear();
	return Client(pack.source).ListVersions(http, pack, out, err, cancel);
}

void ApplyPackVersion(Modpack& pack, const PackVersionInfo& version)
{
	pack.version = version.versionNumber;
	if (!version.sourceVersionId.empty())
		pack.sourceVersionId = version.sourceVersionId;
	if (!version.gameVersion.empty())
		pack.gameVersion = version.gameVersion;
	if (!version.loaders.empty())
		pack.loaders = version.loaders;
	pack.files = version.files;
	if (!version.description.empty())
		pack.description = version.description;
	if (!version.author.empty())
		pack.author = version.author;
	if (version.downloads > 0)
		pack.downloads = version.downloads;
	if (!version.bannerUrl.empty())
		pack.bannerUrl = version.bannerUrl;
	if (!version.websiteUrl.empty())
		pack.websiteUrl = version.websiteUrl;
	if (!version.sourceUrl.empty()) {
		pack.sourceUrl = version.sourceUrl;
		pack.sourceIsSolder = version.sourceIsSolder;
	}
	pack.filesLoaded = true;
}

bool FetchOptionalMods(HttpClient& http,
					   const Modpack& pack,
					   std::vector<OptionalMod>& out,
					   std::string* err,
					   const std::atomic<bool>* cancel)
{
	out.clear();
	return Client(pack.source).OptionalMods(http, pack, out, err, cancel);
}

bool FetchPackDescription(HttpClient& http,
						  const Modpack& pack,
						  std::string& out,
						  std::string* err,
						  const std::atomic<bool>* cancel)
{
	out.clear();
	std::string markup;
	if (!Client(pack.source).DescriptionMarkup(http, pack, markup, err, cancel))
		return false;
	out = MarkupToPlainText(markup.empty() ? pack.description : markup);
	return true;
}

bool FetchPackChangelog(HttpClient& http,
						const Modpack& pack,
						std::string& out,
						std::string* err,
						const std::atomic<bool>* cancel)
{
	out.clear();
	std::string markup;
	if (!Client(pack.source).ChangelogMarkup(http, pack, markup, err, cancel))
		return false;
	out = MarkupToPlainText(markup);
	return true;
}

bool FetchPackMods(HttpClient& http,
				   const Modpack& pack,
				   const std::string& workDir,
				   PackModList& out,
				   std::string* err,
				   const std::atomic<bool>* cancel)
{
	out = {};
	if (pack.sourceVersionId.empty()) {
		if (err) *err = "The pack's version hasn't loaded yet";
		return false;
	}
	if (!Client(pack.source).Mods(http, pack, workDir, out, err, cancel))
		return false;
	std::stable_sort(out.mods.begin(), out.mods.end(),
		[](const PackMod& a, const PackMod& b) { return ToLower(a.name) < ToLower(b.name); });
	return true;
}

bool BuildInstallPlan(HttpClient& http,
					  const Modpack& pack,
					  const std::string& workDir,
					  InstallPlan& plan,
					  std::function<void(const std::string&)> status,
					  std::string* err,
					  const std::atomic<bool>* cancel)
{
	plan = {};
	return Client(pack.source).Plan(http, pack, workDir, plan, std::move(status), err, cancel);
}
