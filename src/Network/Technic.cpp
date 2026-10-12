#include "Technic.h"

#include "HttpClient.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <thread>
#include <vector>

using json = nlohmann::json;

static const char* kApiBase = "https://api.technicpack.net/";
// Technic rejects requests without a "build" parameter. Prism sends MultiMC's "multimc";
// any non-numeric identifier of our own is accepted.
static const char* kBuild = "mclauncherpremium";

static std::string StringOrEmpty(const json& object, const char* key)
{
	const auto it = object.find(key);
	return (it != object.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

// An image field shaped like {"url": "..."}.
static std::string ImageUrl(const json& object, const char* key)
{
	const auto it = object.find(key);
	return (it != object.end() && it->is_object()) ? StringOrEmpty(*it, "url") : std::string();
}

static bool GetJson(HttpClient& http, const std::string& url, json& out, std::string* err, const std::atomic<bool>* cancel)
{
	std::string body;
	if (!http.Get(url, body, err, cancel))
		return false;
	try {
		out = json::parse(body);
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected Technic response: ") + ex.what();
		return false;
	}
	if (out.is_object() && out.contains("error")) {
		if (err) *err = "Technic: " + (out["error"].is_string() ? out["error"].get<std::string>() : out["error"].dump());
		return false;
	}
	return true;
}

static std::string SolderBase(std::string url)
{
	while (!url.empty() && url.back() == '/')
		url.pop_back();
	return url;
}

// Fills a search result with the details the search endpoint leaves out.
static void ApplyDetails(Modpack& pack, const json& detail)
{
	pack.description = StringOrEmpty(detail, "description");
	pack.author = StringOrEmpty(detail, "user");
	pack.downloads = detail.value("installs", 0ULL);
	pack.gameVersion = StringOrEmpty(detail, "minecraft");
	pack.bannerUrl = ImageUrl(detail, "background");
	if (const std::string page = StringOrEmpty(detail, "platformUrl"); !page.empty())
		pack.websiteUrl = page;
}

// "1.12" matches 1.12 and 1.12.x.
static bool VersionMatches(const std::string& version, const std::string& filter)
{
	return version.compare(0, filter.size(), filter) == 0 &&
		(version.size() == filter.size() || version[filter.size()] == '.');
}

bool SearchTechnicModpacks(HttpClient& http,
						   const std::string& query,
						   const SearchFilters& filters,
						   int limit,
						   SourceSearchResult& out,
						   std::string* err,
						   const std::atomic<bool>* cancel)
{
	out = {};
	const std::string url = query.empty()
		? std::string(kApiBase) + "trending?build=" + kBuild
		: std::string(kApiBase) + "search?build=" + kBuild + "&q=" + HttpClient::UrlEncode(query);
	json response;
	if (!GetJson(http, url, response, err, cancel))
		return false;
	try {
		for (const auto& item : response.at("modpacks")) {
			const std::string slug = StringOrEmpty(item, "slug");
			if (slug.empty() || slug == "vanilla")
				continue;
			Modpack pack;
			pack.source = PackSource::Technic;
			pack.sourceId = slug;
			pack.id = "technic-" + slug;
			pack.name = StringOrEmpty(item, "name");
			pack.iconUrl = StringOrEmpty(item, "iconUrl");
			pack.websiteUrl = StringOrEmpty(item, "url");
			pack.filesLoaded = false;
			out.packs.push_back(std::move(pack));
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected Technic search response: ") + ex.what();
		return false;
	}
	// Filters and sorts need each pack's details, which the search response doesn't include.
	if (!filters.gameVersion.empty() || filters.sort == 1) {
		if (static_cast<int>(out.packs.size()) > limit * 2)
			out.packs.resize(static_cast<std::size_t>(limit) * 2); // bound the detail requests
		std::vector<bool> keep(out.packs.size(), false);
		std::mutex mutex;
		std::size_t next = 0;
		auto worker = [&]() {
			for (;;) {
				std::size_t index;
				{
					std::lock_guard<std::mutex> lock(mutex);
					if (next >= out.packs.size() || (cancel && cancel->load()))
						return;
					index = next++;
				}
				json detail;
				if (!GetJson(http, std::string(kApiBase) + "modpack/" + HttpClient::UrlEncode(out.packs[index].sourceId) + "?build=" + kBuild,
						detail, nullptr, cancel))
					continue; // drop packs whose details can't be read
				ApplyDetails(out.packs[index], detail);
				keep[index] = filters.gameVersion.empty() || VersionMatches(out.packs[index].gameVersion, filters.gameVersion);
			}
		};
		std::vector<std::thread> threads;
		for (int i = 0; i < 4; ++i)
			threads.emplace_back(worker);
		for (std::thread& thread : threads)
			thread.join();
		std::vector<Modpack> kept;
		for (std::size_t i = 0; i < out.packs.size(); ++i)
			if (keep[i])
				kept.push_back(std::move(out.packs[i]));
		out.packs = std::move(kept);
		if (filters.sort == 1)
			std::stable_sort(out.packs.begin(), out.packs.end(), [](const Modpack& a, const Modpack& b) { return a.downloads > b.downloads; });
	}

	out.totalHits = out.packs.size(); // the API returns every match at once
	if (static_cast<int>(out.packs.size()) > limit)
		out.packs.resize(static_cast<std::size_t>(limit));
	return true;
}

bool ListTechnicVersions(HttpClient& http,
		const Modpack& pack,
		std::vector<PackVersionInfo>& out,
		std::string* err,
		const std::atomic<bool>* cancel)
{
	out.clear();
	PackVersionInfo current;
	if (!FetchTechnicVersion(http, pack, current, err, cancel))
		return false;
	if (!current.sourceIsSolder) {
		current.versionType = "latest";
		out.push_back(std::move(current));
		return true;
	}
	json solderPack;
	if (!GetJson(http, current.sourceUrl + "/modpack/" + HttpClient::UrlEncode(pack.sourceId), solderPack, err, cancel))
		return false;
	const std::string recommended = StringOrEmpty(solderPack, "recommended");
	const std::string latest = StringOrEmpty(solderPack, "latest");
	std::vector<std::string> builds;
	for (const auto& build : solderPack.value("builds", json::array())) {
		if (build.is_string())
			builds.push_back(build.get<std::string>());
	}
	// Solder lists builds oldest first.
	std::reverse(builds.begin(), builds.end());
	constexpr std::size_t kDetailedBuilds = 10;
	for (std::size_t i = 0; i < builds.size(); ++i) {
		PackVersionInfo info;
		info.versionNumber = builds[i];
		info.sourceVersionId = builds[i];
		info.sourceUrl = current.sourceUrl;
		info.sourceIsSolder = true;
		info.versionType = builds[i] == recommended ? "recommended" : builds[i] == latest ? "latest" : "";
		if (i < kDetailedBuilds) {
			json detail;
			std::string ignored;
			if (GetJson(http, current.sourceUrl + "/modpack/" + HttpClient::UrlEncode(pack.sourceId) + "/" +
					HttpClient::UrlEncode(builds[i]), detail, &ignored, cancel))
				info.gameVersion = StringOrEmpty(detail, "minecraft");
		}
		out.push_back(std::move(info));
	}
	if (out.empty()) {
		if (err) *err = "The pack's Solder server lists no builds";
		return false;
	}
	return true;
}

bool FetchTechnicVersion(HttpClient& http,
						 const Modpack& pack,
						 PackVersionInfo& out,
						 std::string* err,
						 const std::atomic<bool>* cancel)
{
	out = {};
	json detail;
	if (!GetJson(http, std::string(kApiBase) + "modpack/" + HttpClient::UrlEncode(pack.sourceId) + "?build=" + kBuild,
			detail, err, cancel))
		return false;
	out.description = StringOrEmpty(detail, "description");
	out.author = StringOrEmpty(detail, "user");
	out.downloads = detail.value("installs", 0ULL);
	out.gameVersion = StringOrEmpty(detail, "minecraft");
	out.websiteUrl = StringOrEmpty(detail, "platformUrl");
	out.bannerUrl = ImageUrl(detail, "background");
	if (out.bannerUrl.empty())
		out.bannerUrl = ImageUrl(detail, "logo");

	// A string "url" means a single zip, even if "solder" is also set (as Prism does).
	const std::string zipUrl = StringOrEmpty(detail, "url");
	const std::string solder = SolderBase(StringOrEmpty(detail, "solder"));
	if (!zipUrl.empty()) {
		out.sourceUrl = zipUrl;
		out.sourceIsSolder = false;
		out.versionNumber = StringOrEmpty(detail, "version");
		out.sourceVersionId = out.versionNumber.empty() ? "latest" : out.versionNumber;
		return true;
	}
	if (solder.empty()) {
		if (err) *err = "This pack has no download (no zip URL or Solder server)";
		return false;
	}

	// Solder packs: the server's recommended build, else its latest.
	json solderPack;
	if (!GetJson(http, solder + "/modpack/" + HttpClient::UrlEncode(pack.sourceId), solderPack, err, cancel))
		return false;
	std::string build = StringOrEmpty(solderPack, "recommended");
	if (build.empty())
		build = StringOrEmpty(solderPack, "latest");
	if (build.empty()) {
		if (err) *err = "The pack's Solder server lists no builds";
		return false;
	}
	out.sourceUrl = solder;
	out.sourceIsSolder = true;
	out.versionNumber = build;
	out.sourceVersionId = build;
	return true;
}

bool BuildTechnicPlan(HttpClient& http,
					  const Modpack& pack,
					  InstallPlan& plan,
					  std::function<void(const std::string&)> status,
					  std::string* err,
					  const std::atomic<bool>* cancel)
{
	plan = {};
	plan.source = PackSource::Technic;
	plan.sourceId = pack.sourceId;
	plan.sourceVersionId = pack.sourceVersionId;
	plan.versionName = pack.version;
	plan.minecraftVersion = pack.gameVersion;
	if (pack.sourceUrl.empty()) {
		if (err) *err = "The pack's download location isn't known yet; reload it";
		return false;
	}

	if (!pack.sourceIsSolder) {
		// Single zip: the whole pack, unpacked into the game folder. There's no hash to check.
		PlannedFile zip;
		zip.name = pack.name + " archive";
		zip.url = pack.sourceUrl;
		zip.extract = true;
		plan.files.push_back(std::move(zip));
		plan.maxConcurrent = 1;
		plan.notes.push_back("Single-zip Technic packs don't declare their mod loader; check the pack's bin/ folder.");
		return true;
	}

	if (status)
		status("Reading Solder build");
	json build;
	if (!GetJson(http, pack.sourceUrl + "/modpack/" + HttpClient::UrlEncode(pack.sourceId) + "/" + HttpClient::UrlEncode(pack.sourceVersionId),
			build, err, cancel))
		return false;
	try {
		if (const std::string minecraft = StringOrEmpty(build, "minecraft"); !minecraft.empty())
			plan.minecraftVersion = minecraft;
		// Older builds carry the Forge version here; newer ones ship Forge as a mod zip.
		if (const std::string forge = StringOrEmpty(build, "forge"); !forge.empty()) {
			plan.loader = "forge";
			plan.loaderVersion = forge;
		}
		for (const auto& mod : build.at("mods")) {
			// Every Solder mod is a zip laid out like the game folder (mods/, config/, bin/...).
			PlannedFile file;
			file.name = StringOrEmpty(mod, "name") + " " + StringOrEmpty(mod, "version");
			file.url = StringOrEmpty(mod, "url");
			std::string md5 = StringOrEmpty(mod, "md5");
			std::transform(md5.begin(), md5.end(), md5.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			file.md5 = md5;
			file.size = mod.value("filesize", 0ULL);
			file.extract = true;
			if (file.url.empty())
				file.blockedReason = "The Solder server lists no download URL for this mod.";
			plan.files.push_back(std::move(file));
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected Solder build: ") + ex.what();
		return false;
	}
	if (plan.loader.empty())
		plan.notes.push_back("The Solder build doesn't name its mod loader; it ships as files in bin/ or mods/.");
	plan.maxConcurrent = 4;
	return true;
}
