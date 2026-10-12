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

}

SourceCapabilities GetSourceCapabilities(PackSource source)
{
	SourceCapabilities caps;
	switch (source) {
	case PackSource::Modrinth:
		caps.gameVersionFilter = true;
		caps.loaderFilter = true;
		caps.loaders.assign(std::begin(kAllLoaders), std::end(kAllLoaders));
		caps.sorts.assign(std::begin(kModrinthSorts), std::end(kModrinthSorts));
		caps.searchHint = "Search Modrinth modpacks...";
		caps.browseLabel = "MOST DOWNLOADED";
		break;
	case PackSource::CurseForge:
		caps.gameVersionFilter = true;
		caps.loaderFilter = true;
		caps.loaders.assign(std::begin(kAllLoaders), std::end(kAllLoaders));
		caps.sorts.assign(std::begin(kCurseForgeSorts), std::end(kCurseForgeSorts));
		caps.installsPacks = true;
		caps.searchHint = "Search CurseForge modpacks...";
		caps.browseLabel = "MOST DOWNLOADED";
		break;
	case PackSource::FTB:
		caps.gameVersionFilter = true;
		caps.loaderFilter = true;
		caps.loaders.assign(std::begin(kAllLoaders), std::end(kAllLoaders));
		caps.sorts.assign(std::begin(kFtbSorts), std::end(kFtbSorts));
		caps.installsPacks = true;
		caps.searchHint = "Search FTB modpacks...";
		caps.browseLabel = "FEATURED";
		break;
	case PackSource::Technic:
		caps.gameVersionFilter = true; // checked against each result's details (no loader info)
		caps.sorts.assign(std::begin(kTechnicSorts), std::end(kTechnicSorts));
		caps.installsPacks = true;
		caps.searchHint = "Search Technic modpacks...";
		caps.browseLabel = "TRENDING";
		break;
	case PackSource::ATLauncher:
		caps.gameVersionFilter = true; // the pack list has no loader info
		caps.sorts.assign(std::begin(kAtLauncherSorts), std::end(kAtLauncherSorts));
		caps.installsPacks = true;
		caps.searchHint = "Search ATLauncher modpacks...";
		caps.browseLabel = "FEATURED";
		break;
	default:
		caps.sorts = { "Default" };
		break;
	}
	return caps;
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
	if (source == PackSource::CurseForge && CurseForgeApiKey().empty()) {
		if (reason)
			*reason = "CurseForge needs an API key. Add one in Settings > Downloads, or set the "
				"MCLP_CURSEFORGE_API_KEY environment variable (see README.md).";
		return false;
	}
	return true;
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
	switch (source) {
	case PackSource::Modrinth: {
		ModrinthSearchOptions options;
		options.gameVersion = filters.gameVersion;
		options.loader = filters.loader;
		if (filters.sort > 0 && filters.sort < static_cast<int>(std::size(kModrinthIndexes)))
			options.index = kModrinthIndexes[filters.sort];
		ModrinthSearchResult result;
		if (!SearchModrinthModpacks(http, query, options, limit, result, err, cancel))
			return false;
		out.packs = std::move(result.packs);
		out.totalHits = result.totalHits;
		return true;
	}
	case PackSource::CurseForge:
		return SearchCurseForgeModpacks(http, query, filters, limit, out, err, cancel);
	case PackSource::FTB:
		return SearchFtbModpacks(http, query, filters, limit, out, err, cancel);
	case PackSource::Technic:
		return SearchTechnicModpacks(http, query, filters, limit, out, err, cancel);
	case PackSource::ATLauncher:
		return SearchAtLauncherModpacks(http, query, filters, limit, out, err, cancel);
	default:
		break;
	}
	if (err) *err = std::string(PackSourceName(source)) + " search isn't supported";
	return false;
}

bool FetchPackVersion(HttpClient& http,
					  const Modpack& pack,
					  PackVersionInfo& out,
					  std::string* err,
					  const std::atomic<bool>* cancel)
{
	out = {};
	switch (pack.source) {
	case PackSource::Modrinth: {
		ModrinthVersion version;
		if (!FetchLatestModrinthVersion(http, pack.sourceId.empty() ? pack.id : pack.sourceId, version, err, cancel))
			return false;
		out.versionNumber = version.versionNumber;
		out.sourceVersionId = version.id;
		out.gameVersion = version.gameVersion;
		out.loaders = version.loaders;
		out.files = std::move(version.files);
		return true;
	}
	case PackSource::CurseForge:
		return FetchCurseForgeVersion(http, pack, out, err, cancel);
	case PackSource::FTB:
		return FetchFtbVersion(http, pack, out, err, cancel);
	case PackSource::Technic:
		return FetchTechnicVersion(http, pack, out, err, cancel);
	case PackSource::ATLauncher:
		return FetchAtLauncherVersion(http, pack, out, err, cancel);
	default:
		break;
	}
	if (err) *err = std::string(PackSourceName(pack.source)) + " versions aren't supported";
	return false;
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
	if (pack.source == PackSource::ATLauncher)
		return FetchAtLauncherOptionalMods(http, pack, out, err, cancel);
	return true;
}

bool FetchPackDescription(HttpClient& http,
						  const Modpack& pack,
						  std::string& out,
						  std::string* err,
						  const std::atomic<bool>* cancel)
{
	out.clear();
	std::string markup;
	bool ok = true;
	switch (pack.source) {
	case PackSource::Modrinth:
		ok = FetchModrinthDescription(http, pack.sourceId.empty() ? pack.id : pack.sourceId, markup, err, cancel);
		break;
	case PackSource::CurseForge:
		ok = FetchCurseForgeDescription(http, pack, markup, err, cancel);
		break;
	case PackSource::FTB:
		ok = FetchFtbDescription(http, pack, markup, err, cancel);
		break;
	default:
		break;
	}
	if (!ok)
		return false;
	out = MarkupToPlainText(markup.empty() ? pack.description : markup);
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
	bool ok = false;
	switch (pack.source) {
	case PackSource::Modrinth:
		ok = FetchModrinthPackMods(http, pack.sourceVersionId, out.mods, err, cancel);
		if (ok)
			out.note = "Only mods hosted on Modrinth are listed.";
		break;
	case PackSource::CurseForge:
		ok = FetchCurseForgePackMods(http, pack, workDir, out.mods, err, cancel);
		break;
	case PackSource::FTB:
		ok = FetchFtbPackMods(http, pack, out.mods, err, cancel);
		break;
	case PackSource::Technic:
		ok = FetchTechnicPackMods(http, pack, out.mods, err, cancel);
		if (ok && !pack.sourceIsSolder)
			out.note = "This pack is a single archive, so its mods aren't known until it's installed.";
		break;
	case PackSource::ATLauncher:
		ok = FetchAtLauncherPackMods(http, pack, out.mods, err, cancel);
		break;
	default:
		if (err) *err = std::string(PackSourceName(pack.source)) + " packs don't list their mods";
		return false;
	}
	if (!ok)
		return false;
	auto lower = [](std::string text) {
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	};
	std::stable_sort(out.mods.begin(), out.mods.end(),
		[&](const PackMod& a, const PackMod& b) { return lower(a.name) < lower(b.name); });
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
	switch (pack.source) {
	case PackSource::CurseForge:
		return BuildCurseForgePlan(http, pack, workDir, plan, std::move(status), err, cancel);
	case PackSource::FTB:
		return BuildFtbPlan(http, pack, plan, std::move(status), err, cancel);
	case PackSource::Technic:
		return BuildTechnicPlan(http, pack, plan, std::move(status), err, cancel);
	case PackSource::ATLauncher:
		return BuildAtLauncherPlan(http, pack, workDir, plan, std::move(status), err, cancel);
	default:
		break;
	}
	if (err) *err = std::string(PackSourceName(pack.source)) + " packs can't be installed this way";
	return false;
}
