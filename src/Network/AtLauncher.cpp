#include "AtLauncher.h"

#include "HttpClient.h"
#include "Util/Json.h"
#include "Util/Text.h"
#include "Storage/Installer.h"
#include "Util/Hash.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

static const char* kServer = "https://download.nodecdn.net/containers/atl/";

static std::string SafeName(const std::string& name)
{
	std::string safe;
	for (char c : name)
		if (std::isalnum(static_cast<unsigned char>(c)))
			safe += c;
	return safe;
}

static bool GetJson(IHttpClient& http, const std::string& url, json& out, std::string* err, const std::atomic<bool>* cancel)
{
	std::string body;
	if (!http.Get(url, body, err, cancel))
		return false;
	try {
		out = json::parse(body);
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected ATLauncher response: ") + ex.what();
		return false;
	}
	return true;
}

struct AtlPack {
	Modpack pack;
	std::vector<std::pair<std::string, std::string>> versions; // version, Minecraft version; newest first
	int position = 0;
	bool featured = false;
	std::string searchText;
};

static std::mutex g_cacheMutex;
static std::vector<AtlPack> g_cache;

void ClearAtLauncherPackCache()
{
	std::lock_guard<std::mutex> lock(g_cacheMutex);
	g_cache.clear();
}

static bool LoadPacks(IHttpClient& http, std::vector<AtlPack>& out, std::string* err, const std::atomic<bool>* cancel)
{
	{
		std::lock_guard<std::mutex> lock(g_cacheMutex);
		if (!g_cache.empty()) {
			out = g_cache;
			return true;
		}
	}
	json list;
	if (!GetJson(http, std::string(kServer) + "launcher/json/packsnew.json", list, err, cancel))
		return false;
	out.clear();
	try {
		for (const auto& item : list) {
			const json versions = item.value("versions", json::array());
			if (item.value("type", "") != "public" || versions.empty())
				continue;
			AtlPack entry;
			Modpack& pack = entry.pack;
			pack.name = StringOrEmpty(item, "name");
			pack.sourceId = SafeName(pack.name);
			if (pack.sourceId.empty())
				continue;
			pack.source = PackSource::ATLauncher;
			pack.id = "atl-" + pack.sourceId;
			pack.description = StringOrEmpty(item, "description");
			pack.author = "ATLauncher";
			pack.iconUrl = std::string(kServer) + "launcher/images/" + ToLower(pack.sourceId) + ".png";
			pack.websiteUrl = StringOrEmpty(item, "websiteURL");
			if (pack.websiteUrl.empty())
				pack.websiteUrl = "https://atlauncher.com/pack/" + pack.sourceId;
			// Versions are listed newest first.
			pack.version = StringOrEmpty(versions.front(), "version");
			pack.gameVersion = StringOrEmpty(versions.front(), "minecraft");
			pack.filesLoaded = false;
			for (const auto& version : versions)
				entry.versions.emplace_back(StringOrEmpty(version, "version"), StringOrEmpty(version, "minecraft"));
			entry.position = item.value("position", 0);
			entry.featured = item.value("featured", false);
			entry.searchText = ToLower(pack.name + "\n" + pack.description);
			out.push_back(std::move(entry));
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected ATLauncher pack list: ") + ex.what();
		return false;
	}
	std::lock_guard<std::mutex> lock(g_cacheMutex);
	g_cache = out;
	return true;
}

bool SearchAtLauncherModpacks(IHttpClient& http,
							  const std::string& query,
							  const SearchFilters& filters,
							  int limit,
							  SourceSearchResult& out,
							  std::string* err,
							  const std::atomic<bool>* cancel)
{
	out = {};
	std::vector<AtlPack> packs;
	if (!LoadPacks(http, packs, err, cancel))
		return false;
	const std::string lowerQuery = ToLower(query);
	std::vector<const AtlPack*> matches;
	for (const AtlPack& entry : packs) {
		if (!lowerQuery.empty() && entry.searchText.find(lowerQuery) == std::string::npos)
			continue;
		const std::string& version = entry.pack.gameVersion;
		if (!filters.gameVersion.empty() && !(version.compare(0, filters.gameVersion.size(), filters.gameVersion) == 0 &&
				(version.size() == filters.gameVersion.size() || version[filters.gameVersion.size()] == '.')))
			continue;
		matches.push_back(&entry);
	}
	if (filters.sort == 1) { // Name
		std::stable_sort(matches.begin(), matches.end(), [](const AtlPack* a, const AtlPack* b) { return ToLower(a->pack.name) < ToLower(b->pack.name); });
	} else { // Featured first, then ATLauncher's own ordering; name matches first for a text search
		std::stable_sort(matches.begin(), matches.end(), [](const AtlPack* a, const AtlPack* b) { return a->position < b->position; });
		std::stable_sort(matches.begin(), matches.end(), [](const AtlPack* a, const AtlPack* b) { return a->featured && !b->featured; });
		if (!lowerQuery.empty())
			std::stable_sort(matches.begin(), matches.end(), [&](const AtlPack* a, const AtlPack* b) {
				return ToLower(a->pack.name).find(lowerQuery) != std::string::npos && ToLower(b->pack.name).find(lowerQuery) == std::string::npos;
			});
	}
	out.totalHits = matches.size();
	const std::size_t first = static_cast<std::size_t>(std::max(0, filters.offset));
	for (std::size_t i = first; i < matches.size() && i < first + static_cast<std::size_t>(limit); ++i)
		out.packs.push_back(matches[i]->pack);
	return true;
}

static std::string ConfigsUrl(const Modpack& pack, const std::string& version, const char* file)
{
	return std::string(kServer) + "packs/" + pack.sourceId + "/versions/" + IHttpClient::UrlEncode(version) + "/" + file;
}

bool ListAtLauncherVersions(IHttpClient& http,
		const Modpack& pack,
		std::vector<PackVersionInfo>& out,
		std::string* err,
		const std::atomic<bool>* cancel)
{
	out.clear();
	std::vector<AtlPack> packs;
	if (!LoadPacks(http, packs, err, cancel))
		return false;
	for (const AtlPack& entry : packs) {
		if (entry.pack.sourceId != pack.sourceId)
			continue;
		for (const auto& [version, minecraft] : entry.versions) {
			if (version.empty())
				continue;
			PackVersionInfo info;
			info.versionNumber = version;
			info.sourceVersionId = version;
			info.gameVersion = minecraft;
			out.push_back(std::move(info));
		}
		break;
	}
	if (out.empty()) {
		if (err) *err = "This pack has no versions";
		return false;
	}
	return true;
}

bool FetchAtLauncherVersion(IHttpClient& http,
							const Modpack& pack,
							PackVersionInfo& out,
							std::string* err,
							const std::atomic<bool>* cancel)
{
	out = {};
	if (pack.version.empty()) {
		if (err) *err = "This pack has no versions";
		return false;
	}
	json configs;
	if (!GetJson(http, ConfigsUrl(pack, pack.version, "Configs.json"), configs, err, cancel))
		return false;
	out.versionNumber = pack.version;
	out.sourceVersionId = pack.version;
	out.gameVersion = StringOrEmpty(configs, "minecraft");
	if (configs.contains("loader") && configs["loader"].is_object())
		out.loaders = ToLower(StringOrEmpty(configs["loader"], "type"));
	return true;
}

// Game-folder subfolder for an ATLauncher mod type; null = not installed (with a note).
static const char* FolderForType(const std::string& type, const std::string& mcFolder, std::string& scratch)
{
	if (type == "root") return "";
	if (type == "mods") return "mods";
	if (type == "flan") return "Flan";
	if (type == "dependency" || type == "depandency") { scratch = "mods/" + mcFolder; return scratch.c_str(); }
	if (type == "ic2lib") return "mods/ic2";
	if (type == "denlib") return "mods/denlib";
	if (type == "coremods") return "coremods";
	if (type == "plugins") return "plugins";
	if (type == "texturepack") return "texturepacks";
	if (type == "resourcepack") return "resourcepacks";
	if (type == "shaderpack") return "shaderpacks";
	if (type == "jar" || type == "forge") return "jarmods"; // jar mods and old Forge builds, patched into the game jar at Play
	return nullptr; // mcpc, millenaire, decomp, unknown
}

bool FetchAtLauncherOptionalMods(IHttpClient& http,
								 const Modpack& pack,
								 std::vector<OptionalMod>& out,
								 std::string* err,
								 const std::atomic<bool>* cancel)
{
	out.clear();
	json configs;
	if (!GetJson(http, ConfigsUrl(pack, pack.sourceVersionId, "Configs.json"), configs, err, cancel))
		return false;
	try {
		for (const auto& mod : configs.value("mods", json::array())) {
			if (!mod.value("client", true) || !mod.value("optional", false))
				continue;
			OptionalMod item;
			item.name = StringOrEmpty(mod, "name");
			if (item.name.empty())
				continue;
			item.description = StringOrEmpty(mod, "description");
			item.group = StringOrEmpty(mod, "group");
			item.selected = mod.value("selected", false) || mod.value("recommended", false);
			if (mod.contains("depends") && mod["depends"].is_array()) {
				for (const auto& dep : mod["depends"]) {
					if (dep.is_string())
						item.depends.push_back(dep.get<std::string>());
				}
			}
			out.push_back(std::move(item));
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected ATLauncher manifest: ") + ex.what();
		return false;
	}
	return true;
}

bool BuildAtLauncherPlan(IHttpClient& http,
						 const Modpack& pack,
						 const std::string& workDir,
						 InstallPlan& plan,
						 std::function<void(const std::string&)> status,
						 std::string* err,
						 const std::atomic<bool>* cancel)
{
	plan = {};
	if (status)
		status("Reading pack manifest");
	json configs;
	if (!GetJson(http, ConfigsUrl(pack, pack.sourceVersionId, "Configs.json"), configs, err, cancel))
		return false;

	plan.source = PackSource::ATLauncher;
	plan.sourceId = pack.sourceId;
	plan.sourceVersionId = pack.sourceVersionId;
	plan.versionName = pack.sourceVersionId;
	plan.minecraftVersion = StringOrEmpty(configs, "minecraft");
	plan.maxConcurrent = 4;
	if (configs.contains("loader") && configs["loader"].is_object()) {
		const json& loader = configs["loader"];
		plan.loader = ToLower(StringOrEmpty(loader, "type"));
		if (loader.contains("metadata") && loader["metadata"].is_object()) {
			plan.loaderVersion = StringOrEmpty(loader["metadata"], "version");
			if (plan.loaderVersion.empty())
				plan.loaderVersion = StringOrEmpty(loader["metadata"], "loader");
		}
	}

	// The pack's config files, merged in after the mods.
	if (!configs.value("noConfigs", false)) {
		if (status)
			status("Downloading pack configs");
		const std::string zipPath = (fs::path(workDir) / ("Configs-" + SafeName(pack.sourceVersionId) + ".zip")).string();
		std::string sha1;
		if (configs.contains("configs") && configs["configs"].is_object())
			sha1 = ToLower(StringOrEmpty(configs["configs"], "sha1"));
		std::string error;
		if (!http.DownloadToFile(ConfigsUrl(pack, pack.sourceVersionId, "Configs.zip"), zipPath, nullptr, &error, cancel)) {
			if (err) *err = "Configs.zip download failed: " + error;
			return false;
		}
		if (!sha1.empty() && Sha1HexOfFile(zipPath) != sha1) {
			if (err) *err = "Configs.zip download failed: SHA-1 mismatch";
			return false;
		}
		plan.overrides.push_back({ zipPath, "" });
	}

	std::size_t optional = 0, unsupported = 0;
	try {
		for (const auto& mod : configs.value("mods", json::array())) {
			if (!mod.value("client", true))
				continue;
			if (mod.value("optional", false) &&
				std::find(pack.optionalMods.begin(), pack.optionalMods.end(), StringOrEmpty(mod, "name")) == pack.optionalMods.end()) {
				++optional;
				continue;
			}
			const std::string type = StringOrEmpty(mod, "type");
			const std::string download = StringOrEmpty(mod, "download");
			PlannedFile file;
			file.name = StringOrEmpty(mod, "name");
			const std::string url = StringOrEmpty(mod, "url");
			file.md5 = ToLower(StringOrEmpty(mod, "md5"));
			file.size = mod.value("filesize", 0ULL);
			std::string scratch;
			if (type == "extract" || type == "texturepackextract" || type == "resourcepackextract") {
				file.extract = true;
				if (type == "extract") {
					const char* folder = FolderForType(StringOrEmpty(mod, "extractTo"), plan.minecraftVersion, scratch);
					if (!folder) {
						++unsupported;
						continue;
					}
					file.relPath = folder;
					// extractFolder: the zip folder to take, with ATLauncher's "%s%" meaning "/".
					std::string prefix = StringOrEmpty(mod, "extractFolder");
					for (std::size_t at; (at = prefix.find("%s%")) != std::string::npos;)
						prefix.replace(at, 3, "/");
					while (!prefix.empty() && prefix.front() == '/')
						prefix.erase(0, 1);
					if (!prefix.empty() && prefix.back() != '/')
						prefix += '/';
					file.extractPrefix = prefix;
				} else {
					file.relPath = type == "texturepackextract" ? "texturepacks/extracted" : "resourcepacks/extracted";
				}
			} else {
				const char* folder = FolderForType(type, plan.minecraftVersion, scratch);
				const std::string fileName = StringOrEmpty(mod, "file");
				if (!folder || fileName.empty()) {
					++unsupported;
					continue;
				}
				file.relPath = std::string(folder) + (*folder ? "/" : "") + fileName;
			}
			if (!IsSafeRelativePath(file.relPath)) {
				if (err) *err = "Unsafe path in ATLauncher manifest: " + file.relPath;
				return false;
			}
			if (!file.extract && !mod.value("server", true))
				plan.clientOnlyFiles.push_back(file.relPath);
			if (download == "server")
				file.url = std::string(kServer) + url;
			else if (download == "direct")
				file.url = url;
			else {
				// "browser": the author only allows downloading it from their page.
				file.manualPageUrl = url;
				file.blockedReason = "This mod can only be downloaded from its website.";
			}
			plan.files.push_back(std::move(file));
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected ATLauncher manifest: ") + ex.what();
		return false;
	}
	if (optional > 0)
		plan.notes.push_back(std::to_string(optional) + " optional mod(s) weren't chosen and were skipped.");
	if (unsupported > 0)
		plan.notes.push_back(std::to_string(unsupported) + " mod(s) of unsupported types (mcpc, decomp, millenaire) were skipped.");
	return true;
}

bool FetchAtLauncherPackMods(IHttpClient& http,
							 const Modpack& pack,
							 std::vector<PackMod>& out,
							 std::string* err,
							 const std::atomic<bool>* cancel)
{
	out.clear();
	json configs;
	if (!GetJson(http, ConfigsUrl(pack, pack.sourceVersionId, "Configs.json"), configs, err, cancel))
		return false;
	try {
		for (const auto& item : configs.value("mods", json::array())) {
			// The loader itself and hidden library files aren't mods the player would recognize.
			const std::string type = ToLower(StringOrEmpty(item, "type"));
			if (type == "forge" || type == "mcpc" || item.value("library", false) || item.value("hidden", false))
				continue;
			PackMod mod;
			mod.name = StringOrEmpty(item, "name");
			mod.version = StringOrEmpty(item, "version");
			mod.summary = StringOrEmpty(item, "description");
			mod.url = StringOrEmpty(item, "website");
			const bool client = item.value("client", true);
			const bool server = item.value("server", true);
			mod.clientOnly = client && !server;
			mod.serverOnly = server && !client;
			mod.optional = item.value("optional", false);
			out.push_back(std::move(mod));
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected Configs.json: ") + ex.what();
		return false;
	}
	return true;
}
