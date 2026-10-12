#include "Ftb.h"

#include "CurseForge.h"
#include "HttpClient.h"
#include "Util/Json.h"
#include "Util/Text.h"
#include "Storage/Installer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

using json = nlohmann::json;

static const char* kApiBase = "https://api.feed-the-beast.com/v1/modpacks/public";
static constexpr int kDetailThreads = 4;

static bool GetJson(IHttpClient& http, const std::string& path, json& out, std::string* err, const std::atomic<bool>* cancel)
{
	std::string body;
	if (!http.Get(std::string(kApiBase) + path, body, err, cancel))
		return false;
	try {
		out = json::parse(body);
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected FTB response: ") + ex.what();
		return false;
	}
	if (out.value("status", "success") != "success") {
		if (err) *err = "FTB API error: " + StringOrEmpty(out, "message");
		return false;
	}
	return true;
}

// A pack's default version: the newest release, else the newest that isn't archived, else the newest.
static const json* DefaultVersion(const json& versions)
{
	const json* chosen = nullptr;
	for (int pass = 0; pass < 3 && !chosen; ++pass) {
		for (const auto& version : versions) {
			const std::string type = version.value("type", "");
			if ((pass == 0 && type != "release") || (pass == 1 && type == "archived"))
				continue;
			if (!chosen || version.value("id", 0LL) > chosen->value("id", 0LL))
				chosen = &version;
		}
	}
	return chosen;
}

// Minecraft version and loader ("forge 47.1.3") from a version's targets.
static void ReadTargets(const json& version, std::string& minecraft, std::string& loader, std::string& loaderVersion)
{
	for (const auto& target : version.value("targets", json::array())) {
		const std::string type = target.value("type", "");
		if (type == "game" && target.value("name", "") == "minecraft")
			minecraft = StringOrEmpty(target, "version");
		else if (type == "modloader") {
			loader = ToLower(StringOrEmpty(target, "name"));
			loaderVersion = StringOrEmpty(target, "version");
		}
	}
}

struct FtbPack {
	Modpack pack;
	std::uint64_t plays = 0;
	std::int64_t updated = 0;
	bool featured = false;
	std::string searchText; // lowercase name, synopsis and tags
};

// Every pack's details, fetched once per run. Guarded by g_cacheMutex.
static std::mutex g_cacheMutex;
static std::vector<FtbPack> g_cache;
static bool g_cacheLoaded = false;

static bool LoadAllPacks(IHttpClient& http, std::vector<FtbPack>& out, std::string* err, const std::atomic<bool>* cancel)
{
	{
		std::lock_guard<std::mutex> lock(g_cacheMutex);
		if (g_cacheLoaded) {
			out = g_cache;
			return true;
		}
	}
	json all;
	if (!GetJson(http, "/modpack/all", all, err, cancel))
		return false;
	std::vector<std::int64_t> ids;
	for (const auto& id : all.value("packs", json::array()))
		if (id.is_number_integer())
			ids.push_back(id.get<std::int64_t>());

	std::vector<FtbPack> packs(ids.size());
	std::vector<bool> loaded(ids.size(), false);
	std::mutex mutex;
	std::size_t next = 0;
	std::string firstError;
	auto worker = [&]() {
		for (;;) {
			std::size_t index;
			{
				std::lock_guard<std::mutex> lock(mutex);
				if (next >= ids.size() || (cancel && cancel->load()))
					return;
				index = next++;
			}
			json detail;
			std::string error;
			if (!GetJson(http, "/modpack/" + std::to_string(ids[index]), detail, &error, cancel)) {
				std::lock_guard<std::mutex> lock(mutex);
				if (firstError.empty())
					firstError = error;
				continue;
			}
			try {
				if (detail.value("private", false))
					continue;
				FtbPack& entry = packs[index];
				Modpack& pack = entry.pack;
				pack.source = PackSource::FTB;
				pack.sourceId = std::to_string(ids[index]);
				pack.id = "ftb-" + pack.sourceId;
				pack.name = StringOrEmpty(detail, "name");
				pack.description = StringOrEmpty(detail, "synopsis");
				pack.downloads = detail.value("installs", 0ULL);
				const json authors = detail.value("authors", json::array());
				if (!authors.empty())
					pack.author = StringOrEmpty(authors.front(), "name");
				for (const auto& art : detail.value("art", json::array())) {
					const std::string type = art.value("type", "");
					if (type == "square" && pack.iconUrl.empty())
						pack.iconUrl = StringOrEmpty(art, "url");
					else if (type == "splash" && pack.bannerUrl.empty())
						pack.bannerUrl = StringOrEmpty(art, "url");
				}
				const std::string slug = StringOrEmpty(detail, "slug");
				pack.websiteUrl = "https://www.feed-the-beast.com/modpacks/" + pack.sourceId + (slug.empty() ? "" : "-" + slug);
				const json versions = detail.value("versions", json::array());
				if (const json* version = DefaultVersion(versions)) {
					std::string loaderVersion;
					ReadTargets(*version, pack.gameVersion, pack.loaders, loaderVersion);
					pack.version = StringOrEmpty(*version, "name");
				}
				pack.filesLoaded = false;
				entry.plays = detail.value("plays", 0ULL);
				entry.updated = detail.value("updated", 0LL);
				entry.featured = detail.value("featured", false);
				entry.searchText = ToLower(pack.name + "\n" + pack.description);
				pack.updated = IsoDateFromEpoch(entry.updated);
				for (const auto& tag : detail.value("tags", json::array())) {
					const std::string name = StringOrEmpty(tag, "name");
					entry.searchText += "\n" + ToLower(name);
					if (!name.empty())
						pack.categories.push_back(CategoryLabel(name));
				}
				std::lock_guard<std::mutex> lock(mutex);
				loaded[index] = true;
			}
			catch (...) {
				// malformed pack: leave it out
			}
		}
	};
	std::vector<std::thread> threads;
	for (int i = 0; i < kDetailThreads; ++i)
		threads.emplace_back(worker);
	for (std::thread& thread : threads)
		thread.join();
	if (cancel && cancel->load()) {
		if (err) *err = "Cancelled";
		return false;
	}

	out.clear();
	for (std::size_t i = 0; i < packs.size(); ++i)
		if (loaded[i])
			out.push_back(std::move(packs[i]));
	if (out.empty()) {
		if (err) *err = firstError.empty() ? "FTB returned no packs" : firstError;
		return false;
	}
	// Only cache a complete list, so a partial failure is retried on the next search.
	if (firstError.empty()) {
		std::lock_guard<std::mutex> lock(g_cacheMutex);
		g_cache = out;
		g_cacheLoaded = true;
	}
	return true;
}

bool SearchFtbModpacks(IHttpClient& http,
					   const std::string& query,
					   const SearchFilters& filters,
					   int limit,
					   SourceSearchResult& out,
					   std::string* err,
					   const std::atomic<bool>* cancel)
{
	out = {};
	std::vector<FtbPack> packs;
	if (!LoadAllPacks(http, packs, err, cancel))
		return false;

	// Every word of the query must appear in the name, synopsis or tags.
	std::vector<std::string> words;
	{
		std::string word;
		for (char c : ToLower(query) + " ") {
			if (std::isspace(static_cast<unsigned char>(c))) {
				if (!word.empty())
					words.push_back(word);
				word.clear();
			} else {
				word += c;
			}
		}
	}
	std::vector<const FtbPack*> matches;
	for (const FtbPack& entry : packs) {
		bool match = true;
		for (const std::string& word : words)
			match = match && entry.searchText.find(word) != std::string::npos;
		// A version filter of "1.20" matches 1.20 and 1.20.x.
		if (!filters.gameVersion.empty()) {
			const std::string& version = entry.pack.gameVersion;
			match = match && version.compare(0, filters.gameVersion.size(), filters.gameVersion) == 0 &&
				(version.size() == filters.gameVersion.size() || version[filters.gameVersion.size()] == '.');
		}
		if (!filters.loader.empty())
			match = match && entry.pack.loaders == filters.loader;
		if (match)
			matches.push_back(&entry);
	}

	// Sorts, in the order of the labels in PackSources.cpp.
	auto byInstalls = [](const FtbPack* a, const FtbPack* b) { return a->pack.downloads > b->pack.downloads; };
	switch (filters.sort) {
	case 1: // Most played
		std::stable_sort(matches.begin(), matches.end(), [](const FtbPack* a, const FtbPack* b) { return a->plays > b->plays; });
		break;
	case 2: // Recently updated
		std::stable_sort(matches.begin(), matches.end(), [](const FtbPack* a, const FtbPack* b) { return a->updated > b->updated; });
		break;
	case 3: // Name
		std::stable_sort(matches.begin(), matches.end(), [](const FtbPack* a, const FtbPack* b) { return ToLower(a->pack.name) < ToLower(b->pack.name); });
		break;
	default: { // Featured packs first, then most installed; for a text search, name matches come first
		std::stable_sort(matches.begin(), matches.end(), byInstalls);
		std::stable_sort(matches.begin(), matches.end(), [](const FtbPack* a, const FtbPack* b) { return a->featured && !b->featured; });
		auto nameMatches = [&words](const FtbPack* entry) {
			const std::string name = ToLower(entry->pack.name);
			for (const std::string& word : words)
				if (name.find(word) == std::string::npos)
					return false;
			return true;
		};
		if (!words.empty())
			std::stable_sort(matches.begin(), matches.end(), [&](const FtbPack* a, const FtbPack* b) { return nameMatches(a) && !nameMatches(b); });
		break;
	}
	}

	out.totalHits = matches.size();
	const std::size_t first = static_cast<std::size_t>(std::max(0, filters.offset));
	for (std::size_t i = first; i < matches.size() && i < first + static_cast<std::size_t>(limit); ++i)
		out.packs.push_back(matches[i]->pack);
	return true;
}

bool ListFtbVersions(IHttpClient& http,
		const Modpack& pack,
		std::vector<PackVersionInfo>& out,
		std::string* err,
		const std::atomic<bool>* cancel)
{
	out.clear();
	json detail;
	if (!GetJson(http, "/modpack/" + IHttpClient::UrlEncode(pack.sourceId), detail, err, cancel))
		return false;
	try {
		std::vector<std::pair<long long, PackVersionInfo>> versions;
		for (const auto& version : detail.value("versions", json::array())) {
			PackVersionInfo info;
			info.versionNumber = StringOrEmpty(version, "name");
			const long long id = version.value("id", 0LL);
			info.sourceVersionId = std::to_string(id);
			info.versionType = ToLower(StringOrEmpty(version, "type"));
			const std::time_t updated = static_cast<std::time_t>(version.value("updated", 0LL));
			if (updated > 0) {
				char date[16];
				std::tm utc{};
#ifdef _WIN32
				gmtime_s(&utc, &updated);
#else
				gmtime_r(&updated, &utc);
#endif
				if (std::strftime(date, sizeof(date), "%Y-%m-%d", &utc) > 0)
					info.published = date;
			}
			std::string loaderVersion;
			ReadTargets(version, info.gameVersion, info.loaders, loaderVersion);
			versions.emplace_back(id, std::move(info));
		}
		std::sort(versions.begin(), versions.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
		for (auto& [id, info] : versions)
			out.push_back(std::move(info));
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected FTB pack response: ") + ex.what();
		return false;
	}
	if (out.empty()) {
		if (err) *err = "This pack has no versions";
		return false;
	}
	return true;
}

bool FetchFtbVersion(IHttpClient& http,
					 const Modpack& pack,
					 PackVersionInfo& out,
					 std::string* err,
					 const std::atomic<bool>* cancel)
{
	out = {};
	json detail;
	if (!GetJson(http, "/modpack/" + IHttpClient::UrlEncode(pack.sourceId), detail, err, cancel))
		return false;
	try {
		const json versions = detail.value("versions", json::array());
		const json* version = DefaultVersion(versions);
		if (!version) {
			if (err) *err = "This pack has no versions";
			return false;
		}
		out.versionNumber = StringOrEmpty(*version, "name");
		out.sourceVersionId = std::to_string(version->value("id", 0LL));
		std::string loaderVersion;
		ReadTargets(*version, out.gameVersion, out.loaders, loaderVersion);
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected FTB pack response: ") + ex.what();
		return false;
	}
	return true;
}

bool BuildFtbPlan(IHttpClient& http,
				  const Modpack& pack,
				  InstallPlan& plan,
				  std::function<void(const std::string&)> status,
				  std::string* err,
				  const std::atomic<bool>* cancel)
{
	plan = {};
	if (status)
		status("Reading pack manifest");
	json manifest;
	if (!GetJson(http, "/modpack/" + IHttpClient::UrlEncode(pack.sourceId) + "/" + IHttpClient::UrlEncode(pack.sourceVersionId),
			manifest, err, cancel))
		return false;

	plan.source = PackSource::FTB;
	plan.sourceId = pack.sourceId;
	plan.sourceVersionId = pack.sourceVersionId;
	plan.versionName = StringOrEmpty(manifest, "name");
	ReadTargets(manifest, plan.minecraftVersion, plan.loader, plan.loaderVersion);
	// Prism notes that "FTB blocks multiple requests at a time", so files download one by one.
	plan.maxConcurrent = 1;

	std::vector<CurseForgeFileRef> curseForgeRefs;
	std::vector<std::size_t> curseForgeSlots;
	std::size_t serverOnly = 0;
	try {
		for (const auto& file : manifest.at("files")) {
			if (file.value("serveronly", false)) {
				++serverOnly;
				continue;
			}
			// Paths look like "./config" or "./mods"; the name goes inside.
			std::string folder = StringOrEmpty(file, "path");
			while (folder.rfind("./", 0) == 0)
				folder.erase(0, 2);
			if (folder == ".")
				folder.clear();
			if (!folder.empty() && folder.back() != '/')
				folder += '/';
			PlannedFile planned;
			planned.name = StringOrEmpty(file, "name");
			planned.relPath = folder + planned.name;
			planned.url = StringOrEmpty(file, "url");
			planned.sha1 = ToLower(StringOrEmpty(file, "sha1"));
			planned.size = file.value("size", 0ULL);
			if (planned.name.empty() || !IsSafeRelativePath(planned.relPath)) {
				if (err) *err = "Unsafe path in FTB manifest: " + planned.relPath;
				return false;
			}
			if (file.value("clientonly", false))
				plan.clientOnlyFiles.push_back(planned.relPath);
			if (planned.url.empty() && file.contains("curseforge") && file["curseforge"].is_object()) {
				const json& cf = file["curseforge"];
				CurseForgeFileRef ref;
				ref.projectId = cf.value("project", 0LL);
				ref.fileId = cf.value("file", 0LL);
				ref.relPath = planned.relPath;
				curseForgeRefs.push_back(ref);
				curseForgeSlots.push_back(plan.files.size());
				planned.manualPageUrl = "https://www.curseforge.com/projects/" + std::to_string(ref.projectId);
				planned.blockedReason = "Hosted on CurseForge; installing it needs a CurseForge API key (see README.md).";
			} else if (planned.url.empty()) {
				planned.blockedReason = "The FTB manifest has no download URL for this file.";
			}
			plan.files.push_back(std::move(planned));
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected FTB manifest: ") + ex.what();
		return false;
	}

	if (!curseForgeRefs.empty() && !CurseForgeApiKey().empty()) {
		if (status)
			status("Resolving CurseForge files");
		std::vector<PlannedFile> resolved;
		if (!ResolveCurseForgeFiles(http, curseForgeRefs, resolved, err, cancel))
			return false;
		for (std::size_t i = 0; i < resolved.size(); ++i)
			plan.files[curseForgeSlots[i]] = std::move(resolved[i]);
	}
	if (serverOnly > 0)
		plan.notes.push_back(std::to_string(serverOnly) + " server-only file(s) were skipped.");
	return true;
}

bool FetchFtbPackMods(IHttpClient& http,
					  const Modpack& pack,
					  std::vector<PackMod>& out,
					  std::string* err,
					  const std::atomic<bool>* cancel)
{
	out.clear();
	json version;
	if (!GetJson(http, "/modpack/" + IHttpClient::UrlEncode(pack.sourceId) + "/" + IHttpClient::UrlEncode(pack.sourceVersionId),
			version, err, cancel))
		return false;
	try {
		for (const auto& file : version.value("files", json::array())) {
			if (StringOrEmpty(file, "type") != "mod")
				continue;
			PackMod mod;
			mod.version = StringOrEmpty(file, "name");
			mod.name = mod.version;
			if (mod.name.size() > 4 && ToLower(mod.name.substr(mod.name.size() - 4)) == ".jar")
				mod.name.erase(mod.name.size() - 4);
			mod.clientOnly = file.value("clientonly", false);
			mod.serverOnly = file.value("serveronly", false);
			mod.optional = file.value("optional", false);
			if (file.contains("curseforge") && file["curseforge"].is_object()) {
				const json& curseforge = file["curseforge"];
				const std::string project = curseforge.contains("project") && curseforge["project"].is_number()
					? std::to_string(curseforge["project"].get<std::int64_t>()) : StringOrEmpty(curseforge, "project");
				if (!project.empty())
					mod.url = "https://www.curseforge.com/projects/" + project;
			}
			out.push_back(std::move(mod));
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected FTB version: ") + ex.what();
		return false;
	}
	return true;
}

bool FetchFtbDescription(IHttpClient& http,
						 const Modpack& pack,
						 std::string& out,
						 std::string* err,
						 const std::atomic<bool>* cancel)
{
	out.clear();
	json detail;
	if (!GetJson(http, "/modpack/" + IHttpClient::UrlEncode(pack.sourceId), detail, err, cancel))
		return false;
	out = StringOrEmpty(detail, "description");
	return true;
}

bool FetchFtbChangelog(IHttpClient& http,
					   const Modpack& pack,
					   std::string& out,
					   std::string* err,
					   const std::atomic<bool>* cancel)
{
	out.clear();
	json changelog;
	if (!GetJson(http, "/modpack/" + IHttpClient::UrlEncode(pack.sourceId) + "/" + IHttpClient::UrlEncode(pack.sourceVersionId) +
			"/changelog", changelog, err, cancel))
		return false;
	out = StringOrEmpty(changelog, "content");
	return true;
}
