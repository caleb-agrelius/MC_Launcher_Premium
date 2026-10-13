#include "CurseForge.h"
#include "HttpClient.h"
#include "Network/ModSearch.h"
#include "Util/Json.h"
#include "Util/Text.h"

#include "Storage/Installer.h"
#include "Util/Hash.h"
#include "Util/TracedFs.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

static const char* kApiBase = "https://api.curseforge.com/v1";
static constexpr int kMinecraftGameId = 432;
static constexpr int kModpackClassId = 4471;

static std::mutex g_settingsKeyMutex;
static std::string g_settingsKey;

void SetCurseForgeApiKey(const std::string& key)
{
	static const char* kSpace = " \t\r\n";
	const std::size_t first = key.find_first_not_of(kSpace);
	const std::size_t last = key.find_last_not_of(kSpace);
	std::lock_guard<std::mutex> lock(g_settingsKeyMutex);
	g_settingsKey = first == std::string::npos ? std::string() : key.substr(first, last - first + 1);
}

std::string CurseForgeEnvApiKey()
{
#ifdef _WIN32
	char* value = nullptr;
	std::size_t length = 0;
	if (_dupenv_s(&value, &length, "MCLP_CURSEFORGE_API_KEY") == 0 && value) {
		std::string key(value);
		std::free(value);
		if (!key.empty())
			return key;
	}
#else
	if (const char* value = std::getenv("MCLP_CURSEFORGE_API_KEY"); value && *value)
		return value;
#endif
	return {};
}

std::string CurseForgeApiKey()
{
	{
		std::lock_guard<std::mutex> lock(g_settingsKeyMutex);
		if (!g_settingsKey.empty())
			return g_settingsKey;
	}
	return CurseForgeEnvApiKey();
}

static std::vector<std::string> KeyHeader()
{
	return { "x-api-key: " + CurseForgeApiKey(), "Accept: application/json" };
}

// GET/POST with the key. A 403 means the key was rejected, which deserves a clearer message.
static bool ApiGet(IHttpClient& http, const std::string& path, json& out, std::string* err, const std::atomic<bool>* cancel)
{
	std::string body, error;
	if (!http.Get(std::string(kApiBase) + path, body, &error, cancel, KeyHeader())) {
		if (err) *err = error == "HTTP 403" ? "CurseForge rejected the API key (HTTP 403)" : error;
		return false;
	}
	try {
		out = json::parse(body);
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected CurseForge response: ") + ex.what();
		return false;
	}
	return true;
}

static bool ApiPost(IHttpClient& http, const std::string& path, const json& request, json& out, std::string* err, const std::atomic<bool>* cancel)
{
	std::string body, error;
	if (!http.Post(std::string(kApiBase) + path, request.dump(), body, &error, cancel, KeyHeader())) {
		if (err) *err = error == "HTTP 403" ? "CurseForge rejected the API key (HTTP 403)" : error;
		return false;
	}
	try {
		out = json::parse(body);
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected CurseForge response: ") + ex.what();
		return false;
	}
	return true;
}

// CurseForge's modLoaderType numbers.
static int LoaderType(const std::string& loader)
{
	if (loader == "forge") return 1;
	if (loader == "fabric") return 4;
	if (loader == "quilt") return 5;
	if (loader == "neoforge") return 6;
	return 0;
}

static const char* LoaderName(int type)
{
	switch (type) {
	case 1: return "forge";
	case 4: return "fabric";
	case 5: return "quilt";
	case 6: return "neoforge";
	default: return "";
	}
}

// A file's gameVersions mixes Minecraft versions ("1.20.1") with loaders ("Forge") and
// sides ("Client"). Splits out the newest-looking Minecraft version and the loaders.
static void SplitGameVersions(const json& file, std::string& gameVersion, std::string& loaders)
{
	gameVersion.clear();
	loaders.clear();
	for (const auto& entry : file.value("gameVersions", json::array())) {
		if (!entry.is_string())
			continue;
		const std::string value = entry.get<std::string>();
		const std::string lower = ToLower(value);
		if (!value.empty() && std::isdigit(static_cast<unsigned char>(value[0]))) {
			if (gameVersion.empty())
				gameVersion = value;
		} else if (lower == "forge" || lower == "neoforge" || lower == "fabric" || lower == "quilt") {
			if (!loaders.empty())
				loaders += ", ";
			loaders += lower;
		}
	}
}

static std::string Sha1Of(const json& file)
{
	for (const auto& hash : file.value("hashes", json::array())) {
		if (hash.value("algo", 0) == 1)
			return ToLower(StringOrEmpty(hash, "value"));
	}
	return {};
}

bool SearchCurseForgeModpacks(IHttpClient& http,
							  const std::string& query,
							  const SearchFilters& filters,
							  int limit,
							  SourceSearchResult& out,
							  std::string* err,
							  const std::atomic<bool>* cancel)
{
	out = {};
	// Sort labels are in PackSources.cpp; index 0 ("Best match") lets CurseForge rank
	// a text search, and shows the most downloaded packs when there's no text.
	static const int kSortFields[] = { 0, 2, 6, 3, 4 }; // -, Popularity, TotalDownloads, LastUpdated, Name
	int sortField = filters.sort > 0 && filters.sort < static_cast<int>(std::size(kSortFields)) ? kSortFields[filters.sort] : 0;
	if (sortField == 0 && query.empty())
		sortField = 6;

	std::string path = "/mods/search?gameId=" + std::to_string(kMinecraftGameId) +
		"&classId=" + std::to_string(kModpackClassId) +
		"&index=" + std::to_string(std::max(0, filters.offset)) + "&pageSize=" + std::to_string(std::clamp(limit, 1, 50));
	if (sortField != 0)
		path += "&sortField=" + std::to_string(sortField) + (sortField == 4 ? "&sortOrder=asc" : "&sortOrder=desc");
	if (!query.empty())
		path += "&searchFilter=" + IHttpClient::UrlEncode(query);
	if (!filters.gameVersion.empty())
		path += "&gameVersion=" + IHttpClient::UrlEncode(filters.gameVersion);
	if (const int loader = LoaderType(filters.loader); loader != 0)
		path += "&modLoaderType=" + std::to_string(loader);

	json response;
	if (!ApiGet(http, path, response, err, cancel))
		return false;
	try {
		out.totalHits = response.at("pagination").value("totalCount", 0ULL);
		for (const auto& item : response.at("data")) {
			Modpack pack;
			const std::int64_t id = item.at("id").get<std::int64_t>();
			pack.source = PackSource::CurseForge;
			pack.sourceId = std::to_string(id);
			pack.id = "cf-" + pack.sourceId;
			pack.name = StringOrEmpty(item, "name");
			pack.description = StringOrEmpty(item, "summary");
			pack.downloads = item.value("downloadCount", 0ULL);
			pack.updated = StringOrEmpty(item, "dateModified");
			for (const auto& category : item.value("categories", json::array())) {
				if (const std::string name = StringOrEmpty(category, "name"); !name.empty())
					pack.categories.push_back(name);
			}
			const json authors = item.value("authors", json::array());
			if (!authors.empty())
				pack.author = StringOrEmpty(authors.front(), "name");
			if (item.contains("logo") && item["logo"].is_object())
				pack.iconUrl = StringOrEmpty(item["logo"], "thumbnailUrl");
			const json screenshots = item.value("screenshots", json::array());
			if (!screenshots.empty() && screenshots.front().is_object())
				pack.bannerUrl = StringOrEmpty(screenshots.front(), "url");
			if (item.contains("links") && item["links"].is_object())
				pack.websiteUrl = StringOrEmpty(item["links"], "websiteUrl");
			// latestFilesIndexes lists the newest file per game version, newest version first.
			std::set<std::string> loaders;
			for (const auto& index : item.value("latestFilesIndexes", json::array())) {
				if (pack.gameVersion.empty())
					pack.gameVersion = StringOrEmpty(index, "gameVersion");
				if (index.contains("modLoader") && index["modLoader"].is_number_integer()) {
					if (const char* name = LoaderName(index["modLoader"].get<int>()); *name)
						loaders.insert(name);
				}
			}
			for (const std::string& loader : loaders)
				pack.loaders += (pack.loaders.empty() ? "" : ", ") + loader;
			pack.filesLoaded = false;
			out.packs.push_back(std::move(pack));
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected CurseForge search response: ") + ex.what();
		return false;
	}
	return true;
}

bool ListCurseForgeVersions(IHttpClient& http,
		const Modpack& pack,
		std::vector<PackVersionInfo>& out,
		std::string* err,
		const std::atomic<bool>* cancel)
{
	out.clear();
	constexpr int kPageSize = 50;
	constexpr int kMaxFiles = 500;
	try {
		for (int index = 0; index < kMaxFiles; index += kPageSize) {
			json response;
			if (!ApiGet(http, "/mods/" + IHttpClient::UrlEncode(pack.sourceId) + "/files?index=" + std::to_string(index) +
					"&pageSize=" + std::to_string(kPageSize), response, err, cancel))
				return false;
			const json files = response.value("data", json::array());
			for (const auto& file : files) {
				PackVersionInfo info;
				info.versionNumber = StringOrEmpty(file, "displayName");
				if (info.versionNumber.empty())
					info.versionNumber = StringOrEmpty(file, "fileName");
				info.sourceVersionId = std::to_string(file.value("id", 0LL));
				const int type = file.value("releaseType", 1);
				info.versionType = type == 1 ? "release" : type == 2 ? "beta" : "alpha";
				info.published = StringOrEmpty(file, "fileDate").substr(0, 10);
				SplitGameVersions(file, info.gameVersion, info.loaders);
				out.push_back(std::move(info));
			}
			const json pagination = response.value("pagination", json::object());
			if (files.size() < static_cast<std::size_t>(kPageSize) ||
				index + kPageSize >= pagination.value("totalCount", 0))
				break;
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected CurseForge files response: ") + ex.what();
		return false;
	}
	// File ids grow over time, so the biggest id is the newest file.
	std::sort(out.begin(), out.end(), [](const PackVersionInfo& a, const PackVersionInfo& b) {
		return std::stoll(a.sourceVersionId) > std::stoll(b.sourceVersionId);
	});
	if (out.empty()) {
		if (err) *err = "This pack has no published files";
		return false;
	}
	return true;
}

bool FetchCurseForgeVersion(IHttpClient& http,
							const Modpack& pack,
							PackVersionInfo& out,
							std::string* err,
							const std::atomic<bool>* cancel)
{
	out = {};
	json response;
	if (!ApiGet(http, "/mods/" + IHttpClient::UrlEncode(pack.sourceId), response, err, cancel))
		return false;
	try {
		const json& project = response.at("data");
		// latestFiles holds the newest files across versions; file ids grow over time.
		const json latestFiles = project.value("latestFiles", json::array());
		const json* chosen = nullptr;
		for (const bool releasesOnly : { true, false }) {
			for (const auto& file : latestFiles) {
				if (releasesOnly && file.value("releaseType", 0) != 1)
					continue;
				if (!chosen || file.value("id", 0LL) > chosen->value("id", 0LL))
					chosen = &file;
			}
			if (chosen)
				break;
		}
		if (!chosen) {
			if (err) *err = "This pack has no published files";
			return false;
		}
		out.versionNumber = StringOrEmpty(*chosen, "displayName");
		if (out.versionNumber.empty())
			out.versionNumber = StringOrEmpty(*chosen, "fileName");
		out.sourceVersionId = std::to_string(chosen->value("id", 0LL));
		SplitGameVersions(*chosen, out.gameVersion, out.loaders);
		if (project.contains("links") && project["links"].is_object())
			out.websiteUrl = StringOrEmpty(project["links"], "websiteUrl");
		out.downloads = project.value("downloadCount", 0ULL);
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected CurseForge project response: ") + ex.what();
		return false;
	}
	return true;
}

bool ParseCurseForgeManifest(const std::string& text, CurseForgeManifest& out, std::string* err)
{
	out = {};
	try {
		const json manifest = json::parse(text);
		if (manifest.value("manifestType", "") != "minecraftModpack" || manifest.value("manifestVersion", 0) != 1) {
			if (err) *err = "manifest.json isn't a version 1 CurseForge modpack manifest";
			return false;
		}
		out.name = manifest.value("name", "");
		out.version = manifest.value("version", "");
		out.overrides = manifest.value("overrides", "overrides");
		const json& minecraft = manifest.at("minecraft");
		out.minecraftVersion = minecraft.at("version").get<std::string>();
		// The primary loader, else the first. Ids look like "forge-47.2.0" or "fabric-0.15.7".
		const json loaders = minecraft.value("modLoaders", json::array());
		const json* loader = nullptr;
		for (const auto& candidate : loaders) {
			if (candidate.value("primary", false)) {
				loader = &candidate;
				break;
			}
		}
		if (!loader && !loaders.empty())
			loader = &loaders.front();
		if (loader) {
			const std::string id = loader->at("id").get<std::string>();
			const std::size_t dash = id.find('-');
			out.loader = ToLower(id.substr(0, dash));
			out.loaderVersion = dash == std::string::npos ? std::string() : id.substr(dash + 1);
		}
		for (const auto& file : manifest.value("files", json::array())) {
			CurseForgeManifest::File entry;
			entry.projectId = file.at("projectID").get<std::int64_t>();
			entry.fileId = file.at("fileID").get<std::int64_t>();
			entry.required = file.value("required", true);
			out.files.push_back(entry);
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Couldn't read manifest.json: ") + ex.what();
		return false;
	}
	return true;
}

// Folder for a project's CurseForge class.
static const char* FolderForClass(int classId)
{
	switch (classId) {
	case 12: return "resourcepacks";
	case 6552: return "shaderpacks";
	case 6945: return "datapacks";
	default: return "mods";
	}
}

int FillFromModrinthByHash(IHttpClient& http, std::vector<PlannedFile>& files, const std::atomic<bool>* cancel)
{
	json hashes = json::array();
	for (const PlannedFile& file : files) {
		if (file.url.empty() && !file.sha1.empty())
			hashes.push_back(file.sha1);
	}
	if (hashes.empty())
		return 0;
	std::string body, error;
	if (!http.Post("https://api.modrinth.com/v2/version_files", json({ { "hashes", hashes }, { "algorithm", "sha1" } }).dump(),
			body, &error, cancel))
		return 0;
	int found = 0;
	try {
		// The response maps each known hash to the Modrinth version containing that file.
		const json versions = json::parse(body);
		for (PlannedFile& file : files) {
			if (!file.url.empty() || file.sha1.empty())
				continue;
			const auto version = versions.find(file.sha1);
			if (version == versions.end())
				continue;
			for (const auto& candidate : version->value("files", json::array())) {
				if (candidate.contains("hashes") && ToLower(StringOrEmpty(candidate["hashes"], "sha1")) == file.sha1) {
					file.url = StringOrEmpty(candidate, "url");
					if (!file.url.empty()) {
						file.blockedReason.clear();
						++found;
					}
					break;
				}
			}
		}
	}
	catch (...) {
		// unreadable response: leave the files blocked
	}
	return found;
}

bool ResolveCurseForgeFiles(IHttpClient& http,
							const std::vector<CurseForgeFileRef>& refs,
							std::vector<PlannedFile>& out,
							std::string* err,
							const std::atomic<bool>* cancel)
{
	out.clear();
	if (refs.empty())
		return true;

	json fileIds = json::array();
	json modIds = json::array();
	for (const CurseForgeFileRef& ref : refs) {
		fileIds.push_back(ref.fileId);
		modIds.push_back(ref.projectId);
	}
	json filesResponse, modsResponse;
	if (!ApiPost(http, "/mods/files", { { "fileIds", fileIds } }, filesResponse, err, cancel))
		return false;
	if (!ApiPost(http, "/mods", { { "modIds", modIds } }, modsResponse, err, cancel))
		return false;

	try {
		std::map<std::int64_t, json> filesById;
		for (const auto& file : filesResponse.at("data"))
			filesById[file.at("id").get<std::int64_t>()] = file;
		std::map<std::int64_t, std::pair<int, std::string>> projects; // id -> class, website
		for (const auto& mod : modsResponse.at("data")) {
			std::string website;
			if (mod.contains("links") && mod["links"].is_object())
				website = StringOrEmpty(mod["links"], "websiteUrl");
			projects[mod.at("id").get<std::int64_t>()] = { mod.value("classId", 6), website };
		}

		for (const CurseForgeFileRef& ref : refs) {
			const auto file = filesById.find(ref.fileId);
			if (file == filesById.end()) {
				if (err) *err = "CurseForge has no file " + std::to_string(ref.fileId) + " (project " + std::to_string(ref.projectId) + ")";
				return false;
			}
			const auto project = projects.find(ref.projectId);
			PlannedFile planned;
			planned.name = StringOrEmpty(file->second, "fileName");
			planned.url = StringOrEmpty(file->second, "downloadUrl");
			planned.sha1 = Sha1Of(file->second);
			planned.size = file->second.value("fileLength", 0ULL);
			const int classId = project != projects.end() ? project->second.first : 6;
			planned.relPath = !ref.relPath.empty() ? ref.relPath : std::string(FolderForClass(classId)) + "/" + planned.name;
			if (planned.url.empty()) {
				planned.blockedReason = "Its author only allows downloads from the CurseForge website.";
				if (project != projects.end() && !project->second.second.empty())
					planned.manualPageUrl = project->second.second + "/download/" + std::to_string(ref.fileId);
			}
			out.push_back(std::move(planned));
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected CurseForge file response: ") + ex.what();
		return false;
	}

	FillFromModrinthByHash(http, out, cancel);
	return true;
}

// Downloads the pack's zip (pack.sourceVersionId) into workDir, unless a copy with the
// right SHA-1 is already there, and reads its manifest.json.
static bool DownloadPackManifest(IHttpClient& http, const Modpack& pack, const std::string& workDir, std::string& zipPath,
	CurseForgeManifest& manifest, std::string* err, const std::atomic<bool>* cancel)
{
	json fileResponse;
	if (!ApiGet(http, "/mods/" + IHttpClient::UrlEncode(pack.sourceId) + "/files/" + IHttpClient::UrlEncode(pack.sourceVersionId),
			fileResponse, err, cancel))
		return false;
	std::string zipUrl, zipName, zipSha1;
	try {
		const json& file = fileResponse.at("data");
		zipUrl = StringOrEmpty(file, "downloadUrl");
		zipName = StringOrEmpty(file, "fileName");
		zipSha1 = Sha1Of(file);
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected CurseForge file response: ") + ex.what();
		return false;
	}
	if (zipUrl.empty()) {
		if (err) *err = "This pack's author only allows downloads from the CurseForge website";
		return false;
	}
	if (zipName.empty() || !IsSafeRelativePath(zipName) || fs::path(zipName).has_parent_path())
		zipName = "pack-" + pack.sourceVersionId + ".zip";
	zipPath = (fs::path(workDir) / zipName).string();
	std::error_code ec;
	TracedFs::CreateDirectories(workDir, ec);
	const bool cached = fs::is_regular_file(zipPath, ec) && !zipSha1.empty() && Sha1HexOfFile(zipPath) == zipSha1;
	if (!cached) {
		std::string error;
		if (!http.DownloadToFile(zipUrl, zipPath, nullptr, &error, cancel)) {
			if (err) *err = "Pack download failed: " + error;
			return false;
		}
		if (!zipSha1.empty() && Sha1HexOfFile(zipPath) != zipSha1) {
			TracedFs::Remove(zipPath, ec);
			if (err) *err = "Pack download failed: SHA-1 mismatch";
			return false;
		}
	}

	std::string manifestText;
	if (!ReadZipEntry(zipPath, "manifest.json", manifestText, err))
		return false;
	return ParseCurseForgeManifest(manifestText, manifest, err);
}

bool BuildCurseForgePlan(IHttpClient& http,
						 const Modpack& pack,
						 const std::string& workDir,
						 InstallPlan& plan,
						 std::function<void(const std::string&)> status,
						 std::string* err,
						 const std::atomic<bool>* cancel)
{
	plan = {};
	auto report = [&](const char* text) {
		if (status)
			status(text);
	};

	// 1. The pack's own zip and its manifest.
	report("Downloading pack archive");
	std::string zipPath;
	CurseForgeManifest manifest;
	if (!DownloadPackManifest(http, pack, workDir, zipPath, manifest, err, cancel))
		return false;

	// 2. Every required file.
	report("Resolving mod files");
	std::vector<CurseForgeFileRef> refs;
	std::size_t optional = 0;
	for (const CurseForgeManifest::File& file : manifest.files) {
		if (!file.required) {
			++optional;
			continue;
		}
		refs.push_back({ file.projectId, file.fileId, {} });
	}
	if (!ResolveCurseForgeFiles(http, refs, plan.files, err, cancel))
		return false;

	plan.source = PackSource::CurseForge;
	plan.sourceId = pack.sourceId;
	plan.sourceVersionId = pack.sourceVersionId;
	plan.versionName = manifest.version.empty() ? pack.version : manifest.version;
	plan.minecraftVersion = manifest.minecraftVersion;
	plan.loader = manifest.loader;
	plan.loaderVersion = manifest.loaderVersion;
	plan.overrides.push_back({ zipPath, manifest.overrides.empty() ? std::string() : manifest.overrides + "/" });
	plan.maxConcurrent = 6;
	if (optional > 0)
		plan.notes.push_back(std::to_string(optional) + " optional file(s) in the manifest were skipped.");
	return true;
}

bool FetchCurseForgePackMods(IHttpClient& http,
							 const Modpack& pack,
							 const std::string& workDir,
							 std::vector<PackMod>& out,
							 std::string* err,
							 const std::atomic<bool>* cancel)
{
	out.clear();
	std::string zipPath;
	CurseForgeManifest manifest;
	if (!DownloadPackManifest(http, pack, workDir, zipPath, manifest, err, cancel))
		return false;
	if (manifest.files.empty())
		return true;
	json request = { { "modIds", json::array() } };
	std::map<std::int64_t, bool> required;
	for (const CurseForgeManifest::File& file : manifest.files) {
		request["modIds"].push_back(file.projectId);
		required[file.projectId] = required[file.projectId] || file.required;
	}
	json response;
	if (!ApiPost(http, "/mods", request, response, err, cancel))
		return false;
	try {
		for (const auto& project : response.at("data")) {
			PackMod mod;
			mod.name = StringOrEmpty(project, "name");
			mod.summary = StringOrEmpty(project, "summary");
			if (project.contains("links") && project["links"].is_object())
				mod.url = StringOrEmpty(project["links"], "websiteUrl");
			// classId 6 is mods; resource packs (12) and shaders (6552) are labelled.
			const int classId = project.value("classId", 6);
			if (classId == 12)
				mod.version = "Resource pack";
			else if (classId == 6552)
				mod.version = "Shader pack";
			const auto it = required.find(project.value("id", std::int64_t{ 0 }));
			mod.optional = it != required.end() && !it->second;
			out.push_back(std::move(mod));
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected CurseForge mods response: ") + ex.what();
		return false;
	}
	return true;
}

bool FetchCurseForgeDescription(IHttpClient& http,
								const Modpack& pack,
								std::string& out,
								std::string* err,
								const std::atomic<bool>* cancel)
{
	out.clear();
	json response;
	if (!ApiGet(http, "/mods/" + IHttpClient::UrlEncode(pack.sourceId) + "/description", response, err, cancel))
		return false;
	out = StringOrEmpty(response, "data");
	return true;
}

bool FetchCurseForgeChangelog(IHttpClient& http,
							  const Modpack& pack,
							  std::string& out,
							  std::string* err,
							  const std::atomic<bool>* cancel)
{
	out.clear();
	json response;
	if (!ApiGet(http, "/mods/" + IHttpClient::UrlEncode(pack.sourceId) + "/files/" + IHttpClient::UrlEncode(pack.sourceVersionId) +
			"/changelog", response, err, cancel))
		return false;
	out = StringOrEmpty(response, "data");
	return true;
}

// ---- Mods, resource packs and shaders for Manage > Content > Browse ----

namespace {

constexpr int kModClassId = 6;
constexpr int kResourcePackClassId = 12;
constexpr int kShaderClassId = 6552;
constexpr int kRequiredDependency = 3; // File.dependencies[].relationType
constexpr const char* kProjectPrefix = "cf-"; // keeps CurseForge ids apart from Modrinth's in the browser

int ClassIdFor(ContentKind kind)
{
	switch (kind) {
	case ContentKind::ResourcePacks: return kResourcePackClassId;
	case ContentKind::ShaderPacks: return kShaderClassId;
	default: return kModClassId;
	}
}

// The loaders a pack's mods may use, as CurseForge numbers them, best first. Quilt runs Fabric mods
// too. Resource packs and shaders have no loader: a single 0 means "don't filter".
std::vector<int> LoaderTypesFor(const ContentSearch& search)
{
	if (search.kind != ContentKind::Mods)
		return { 0 };
	if (search.loader == "quilt")
		return { LoaderType("quilt"), LoaderType("fabric") };
	return { LoaderType(search.loader) };
}

std::string ProjectId(std::int64_t id)
{
	return kProjectPrefix + std::to_string(id);
}

// The number in a project id the browser holds ("cf-238222"); empty if it isn't one of ours.
std::string NumberOf(const std::string& projectId)
{
	const std::string prefix = kProjectPrefix;
	if (projectId.compare(0, prefix.size(), prefix) != 0 || projectId.size() == prefix.size())
		return {};
	const std::string number = projectId.substr(prefix.size());
	return std::all_of(number.begin(), number.end(), [](unsigned char c) { return std::isdigit(c) != 0; }) ? number : std::string();
}

bool RequireKey(std::string* err)
{
	if (!CurseForgeApiKey().empty())
		return true;
	if (err)
		*err = "CurseForge needs an API key. Add one in Settings > Downloads, or set the MCLP_CURSEFORGE_API_KEY environment variable.";
	return false;
}

ContentProject ProjectFrom(const json& item)
{
	ContentProject project;
	project.id = ProjectId(item.value("id", std::int64_t{ 0 }));
	project.name = StringOrEmpty(item, "name");
	project.summary = StringOrEmpty(item, "summary");
	project.downloads = item.value("downloadCount", 0ULL);
	const json authors = item.value("authors", json::array());
	if (!authors.empty() && authors.front().is_object())
		project.author = StringOrEmpty(authors.front(), "name");
	if (item.contains("logo") && item["logo"].is_object())
		project.iconUrl = StringOrEmpty(item["logo"], "thumbnailUrl");
	if (item.contains("links") && item["links"].is_object())
		project.pageUrl = StringOrEmpty(item["links"], "websiteUrl");
	return project;
}

// A file that came back without a download URL because its author turned off third-party downloads:
// the same file on Modrinth, found by its SHA-1, or an error that says where to get it by hand.
bool ResolveBlockedFile(IHttpClient& http, const json& project, const json& file, ContentDownload& out, std::string* err,
	const std::atomic<bool>* cancel)
{
	if (!out.sha1.empty()) {
		std::vector<PlannedFile> files(1);
		files[0].sha1 = out.sha1;
		if (FillFromModrinthByHash(http, files, cancel) > 0 && !files[0].url.empty()) {
			out.url = files[0].url;
			return true;
		}
	}
	std::string page = project.is_object() && project.contains("links") && project["links"].is_object()
		? StringOrEmpty(project["links"], "websiteUrl") : std::string();
	if (!page.empty())
		page += "/download/" + std::to_string(file.value("id", std::int64_t{ 0 }));
	if (err) {
		*err = "The author of " + out.fileName + " only allows downloads from the CurseForge website" +
			(page.empty() ? std::string(".") : ": " + page);
	}
	return false;
}

}

bool SearchCurseForgeContent(IHttpClient& http, const ContentSearch& search, int limit, std::vector<ContentProject>& out,
	std::string* err, const std::atomic<bool>* cancel)
{
	out.clear();
	if (!RequireKey(err))
		return false;
	// With no text, the most downloaded; with text, CurseForge's own ranking.
	const std::string sort = search.query.empty() ? "&sortField=6&sortOrder=desc" : std::string();
	const std::vector<int> loaders = LoaderTypesFor(search);
	std::set<std::string> seen;
	for (const int loader : loaders) {
		std::string path = "/mods/search?gameId=" + std::to_string(kMinecraftGameId) + "&classId=" + std::to_string(ClassIdFor(search.kind)) +
			"&pageSize=" + std::to_string(std::clamp(limit, 1, 50)) + sort;
		if (!search.query.empty())
			path += "&searchFilter=" + IHttpClient::UrlEncode(search.query);
		if (!search.gameVersion.empty())
			path += "&gameVersion=" + IHttpClient::UrlEncode(search.gameVersion);
		if (loader != 0)
			path += "&modLoaderType=" + std::to_string(loader);
		json response;
		if (!ApiGet(http, path, response, err, cancel))
			return false;
		try {
			for (const auto& item : response.at("data")) {
				ContentProject project = ProjectFrom(item);
				if (project.id != ProjectId(0) && seen.insert(project.id).second)
					out.push_back(std::move(project));
			}
		}
		catch (const std::exception& ex) {
			if (err) *err = std::string("Unexpected CurseForge search response: ") + ex.what();
			return false;
		}
	}
	// Results for more than one loader come in separate lists; put the most downloaded first.
	if (loaders.size() > 1) {
		std::stable_sort(out.begin(), out.end(), [](const ContentProject& a, const ContentProject& b) { return a.downloads > b.downloads; });
		if (out.size() > static_cast<std::size_t>(limit))
			out.resize(static_cast<std::size_t>(limit));
	}
	return true;
}

bool FindCurseForgeContentDownload(IHttpClient& http, const ContentSearch& search, const std::string& projectId,
	ContentDownload& out, std::string* err, const std::atomic<bool>* cancel)
{
	out = ContentDownload{};
	if (!RequireKey(err))
		return false;
	const std::string number = NumberOf(projectId);
	if (number.empty()) {
		if (err) *err = "That isn't a CurseForge project.";
		return false;
	}
	for (const int loader : LoaderTypesFor(search)) {
		std::string path = "/mods/" + number + "/files?pageSize=50";
		if (!search.gameVersion.empty())
			path += "&gameVersion=" + IHttpClient::UrlEncode(search.gameVersion);
		if (loader != 0)
			path += "&modLoaderType=" + std::to_string(loader);
		json response;
		if (!ApiGet(http, path, response, err, cancel))
			return false;
		try {
			// The newest release (file ids grow over time), else the newest file of any kind.
			const json* chosen = nullptr;
			for (const bool releasesOnly : { true, false }) {
				for (const auto& file : response.at("data")) {
					if (releasesOnly && file.value("releaseType", 0) != 1)
						continue;
					if (!chosen || file.value("id", std::int64_t{ 0 }) > chosen->value("id", std::int64_t{ 0 }))
						chosen = &file;
				}
				if (chosen)
					break;
			}
			if (!chosen)
				continue; // no file for this loader; the next one may have one
			out.projectId = projectId;
			out.fileName = StringOrEmpty(*chosen, "fileName");
			out.url = StringOrEmpty(*chosen, "downloadUrl");
			out.size = chosen->value("fileLength", 0ULL);
			out.sha1 = Sha1Of(*chosen);
			for (const auto& dependency : chosen->value("dependencies", json::array())) {
				const std::int64_t modId = dependency.value("modId", std::int64_t{ 0 });
				if (dependency.value("relationType", 0) == kRequiredDependency && modId != 0)
					out.requiredProjects.push_back(ProjectId(modId));
			}
			if (out.fileName.empty()) {
				if (err) *err = "That version's file has no name.";
				return false;
			}
			if (!out.url.empty())
				return true;
			json project; // for the page to send the player to; the file may still be found elsewhere without it
			const bool havePage = ApiGet(http, "/mods/" + number, project, nullptr, cancel) && project.is_object();
			return ResolveBlockedFile(http, havePage ? project.value("data", json::object()) : json::object(), *chosen, out, err, cancel);
		}
		catch (const std::exception& ex) {
			if (err) *err = std::string("Unexpected CurseForge files response: ") + ex.what();
			return false;
		}
	}
	std::string what = search.gameVersion.empty() ? std::string() : "Minecraft " + search.gameVersion;
	if (!search.loader.empty() && search.kind == ContentKind::Mods)
		what += (what.empty() ? "" : " with ") + search.loader;
	if (err) *err = "No version for " + (what.empty() ? std::string("this pack") : what) + ".";
	return false;
}

bool FindCurseForgeInstalledProjects(IHttpClient& http, const fs::path& folder, std::set<std::string>& projectIds,
	std::string* err, const std::atomic<bool>* cancel)
{
	if (!RequireKey(err))
		return false;
	// Turned-off copies count too: the project is still in the pack.
	std::vector<std::uint32_t> fingerprints;
	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(folder, ec)) {
		std::uint32_t fingerprint = 0;
		if (entry.is_regular_file(ec) && CurseForgeFingerprintOfFile(entry.path().string(), fingerprint))
			fingerprints.push_back(fingerprint);
		if (cancel && cancel->load())
			return false;
	}
	constexpr std::size_t kBatch = 500;
	for (std::size_t start = 0; start < fingerprints.size(); start += kBatch) {
		const std::size_t end = std::min(fingerprints.size(), start + kBatch);
		const json request = { { "fingerprints", std::vector<std::uint32_t>(fingerprints.begin() + static_cast<std::ptrdiff_t>(start),
			fingerprints.begin() + static_cast<std::ptrdiff_t>(end)) } };
		json response;
		if (!ApiPost(http, "/fingerprints/" + std::to_string(kMinecraftGameId), request, response, err, cancel))
			return false;
		try {
			for (const auto& match : response.at("data").value("exactMatches", json::array())) {
				std::int64_t id = match.value("id", std::int64_t{ 0 });
				if (id == 0 && match.contains("file") && match["file"].is_object())
					id = match["file"].value("modId", std::int64_t{ 0 });
				if (id != 0)
					projectIds.insert(ProjectId(id));
			}
		}
		catch (const std::exception& ex) {
			if (err) *err = std::string("Unexpected CurseForge fingerprints response: ") + ex.what();
			return false;
		}
	}
	return true;
}
