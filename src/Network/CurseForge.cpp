#include "CurseForge.h"
#include "HttpClient.h"

#include "Storage/Installer.h"
#include "Util/Hash.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <map>
#include <mutex>
#include <set>

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
static bool ApiGet(HttpClient& http, const std::string& path, json& out, std::string* err, const std::atomic<bool>* cancel)
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

static bool ApiPost(HttpClient& http, const std::string& path, const json& request, json& out, std::string* err, const std::atomic<bool>* cancel)
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

static std::string StringOrEmpty(const json& object, const char* key)
{
	const auto it = object.find(key);
	return (it != object.end() && it->is_string()) ? it->get<std::string>() : std::string();
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

static std::string ToLower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
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

bool SearchCurseForgeModpacks(HttpClient& http,
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
		"&index=0&pageSize=" + std::to_string(std::clamp(limit, 1, 50));
	if (sortField != 0)
		path += "&sortField=" + std::to_string(sortField) + (sortField == 4 ? "&sortOrder=asc" : "&sortOrder=desc");
	if (!query.empty())
		path += "&searchFilter=" + HttpClient::UrlEncode(query);
	if (!filters.gameVersion.empty())
		path += "&gameVersion=" + HttpClient::UrlEncode(filters.gameVersion);
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

bool FetchCurseForgeVersion(HttpClient& http,
							const Modpack& pack,
							PackVersionInfo& out,
							std::string* err,
							const std::atomic<bool>* cancel)
{
	out = {};
	json response;
	if (!ApiGet(http, "/mods/" + HttpClient::UrlEncode(pack.sourceId), response, err, cancel))
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

int FillFromModrinthByHash(HttpClient& http, std::vector<PlannedFile>& files, const std::atomic<bool>* cancel)
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

bool ResolveCurseForgeFiles(HttpClient& http,
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
static bool DownloadPackManifest(HttpClient& http, const Modpack& pack, const std::string& workDir, std::string& zipPath,
	CurseForgeManifest& manifest, std::string* err, const std::atomic<bool>* cancel)
{
	json fileResponse;
	if (!ApiGet(http, "/mods/" + HttpClient::UrlEncode(pack.sourceId) + "/files/" + HttpClient::UrlEncode(pack.sourceVersionId),
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
	fs::create_directories(workDir, ec);
	const bool cached = fs::is_regular_file(zipPath, ec) && !zipSha1.empty() && Sha1HexOfFile(zipPath) == zipSha1;
	if (!cached) {
		std::string error;
		if (!http.DownloadToFile(zipUrl, zipPath, nullptr, &error, cancel)) {
			if (err) *err = "Pack download failed: " + error;
			return false;
		}
		if (!zipSha1.empty() && Sha1HexOfFile(zipPath) != zipSha1) {
			fs::remove(zipPath, ec);
			if (err) *err = "Pack download failed: SHA-1 mismatch";
			return false;
		}
	}

	std::string manifestText;
	if (!ReadZipEntry(zipPath, "manifest.json", manifestText, err))
		return false;
	return ParseCurseForgeManifest(manifestText, manifest, err);
}

bool BuildCurseForgePlan(HttpClient& http,
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

bool FetchCurseForgePackMods(HttpClient& http,
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

bool FetchCurseForgeDescription(HttpClient& http,
								const Modpack& pack,
								std::string& out,
								std::string* err,
								const std::atomic<bool>* cancel)
{
	out.clear();
	json response;
	if (!ApiGet(http, "/mods/" + HttpClient::UrlEncode(pack.sourceId) + "/description", response, err, cancel))
		return false;
	out = StringOrEmpty(response, "data");
	return true;
}
