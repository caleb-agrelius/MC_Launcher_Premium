#include "Library.h"
#include "Util/TracedFs.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;

static std::vector<PackFile> FilesFromJson(const json& item)
{
	std::vector<PackFile> files;
	if (item.contains("files") && item["files"].is_array()) {
		for (const auto& f : item["files"]) {
			PackFile file;
			file.name = f.value("name", "");
			file.url = f.value("url", "");
			file.size = f.value("size", 0ULL);
			files.push_back(std::move(file));
		}
	}
	return files;
}

static json FilesToJson(const std::vector<PackFile>& files)
{
	json out = json::array();
	for (const PackFile& file : files)
		out.push_back({ { "name", file.name }, { "url", file.url }, { "size", file.size } });
	return out;
}

static Modpack PackFromJson(const json& item)
{
	Modpack pack;
	pack.id = item.value("id", "");
	pack.name = item.value("name", "");
	pack.description = item.value("description", "");
	pack.version = item.value("version", "");
	pack.author = item.value("author", "");
	pack.gameVersion = item.value("gameVersion", "");
	pack.loaders = item.value("loaders", "");
	pack.iconUrl = item.value("iconUrl", "");
	pack.color = item.value("color", 0u);
	pack.websiteUrl = item.value("websiteUrl", "");
	// Files written before multiple sources existed only have "fromModrinth".
	if (item.contains("source"))
		pack.source = PackSourceFromKey(item.value("source", ""));
	else if (item.value("fromModrinth", false))
		pack.source = PackSource::Modrinth;
	pack.sourceId = item.value("sourceId", "");
	pack.sourceVersionId = item.value("sourceVersionId", "");
	pack.sourceUrl = item.value("sourceUrl", "");
	pack.sourceIsSolder = item.value("sourceIsSolder", false);
	pack.optionalMods = item.value("optionalMods", std::vector<std::string>());
	pack.files = FilesFromJson(item);
	for (const auto& v : item.value("installedVersions", json::array())) {
		InstalledVersion version;
		version.key = v.value("key", "");
		version.version = v.value("version", "");
		version.sourceVersionId = v.value("sourceVersionId", "");
		version.gameVersion = v.value("gameVersion", "");
		version.loaders = v.value("loaders", "");
		version.files = FilesFromJson(v);
		version.optionalMods = v.value("optionalMods", std::vector<std::string>());
		version.installDir = v.value("installDir", "");
		version.gameDir = v.value("gameDir", "");
		if (v.contains("contentChanges") && v["contentChanges"].is_object()) {
			for (const auto& [path, change] : v["contentChanges"].items()) {
				if (change.is_string())
					version.contentChanges[path] = change.get<std::string>();
			}
		}
		if (!version.key.empty() && !version.installDir.empty() && !version.gameDir.empty())
			pack.installedVersions.push_back(std::move(version));
	}
	pack.activeVersion = item.value("activeVersion", "");
	return pack;
}

static json PackToJson(const Modpack& pack)
{
	json versions = json::array();
	for (const InstalledVersion& v : pack.installedVersions) {
		json entry = {
			{ "key", v.key },
			{ "version", v.version },
			{ "sourceVersionId", v.sourceVersionId },
			{ "gameVersion", v.gameVersion },
			{ "loaders", v.loaders },
			{ "files", FilesToJson(v.files) },
			{ "optionalMods", v.optionalMods },
			{ "installDir", v.installDir },
			{ "gameDir", v.gameDir },
		};
		if (!v.contentChanges.empty())
			entry["contentChanges"] = v.contentChanges;
		versions.push_back(std::move(entry));
	}
	json out = {
		{ "id", pack.id },
		{ "name", pack.name },
		{ "description", pack.description },
		{ "version", pack.version },
		{ "author", pack.author },
		{ "gameVersion", pack.gameVersion },
		{ "loaders", pack.loaders },
		{ "iconUrl", pack.iconUrl },
		{ "color", pack.color },
		{ "websiteUrl", pack.websiteUrl },
		{ "source", PackSourceKey(pack.source) },
		{ "sourceId", pack.sourceId },
		{ "sourceVersionId", pack.sourceVersionId },
		{ "sourceUrl", pack.sourceUrl },
		{ "sourceIsSolder", pack.sourceIsSolder },
		{ "optionalMods", pack.optionalMods },
		{ "files", FilesToJson(pack.files) },
	};
	if (!versions.empty()) {
		out["installedVersions"] = versions;
		out["activeVersion"] = pack.activeVersion;
	}
	return out;
}

// Reads a JSON array from path, or returns null if the file is missing or not an array.
static json ReadJsonArray(const std::string& path)
{
	std::ifstream ifs(path);
	if (!ifs.is_open())
		return nullptr;
	try {
		json j;
		ifs >> j;
		if (j.is_array())
			return j;
	}
	catch (...) {
		// corrupt file: treat as missing
	}
	return nullptr;
}

static bool WriteJson(const std::string& path, const json& j)
{
	auto ofs = TracedFs::OpenWrite(path, std::ios::trunc);
	if (!ofs.is_open())
		return false;
	ofs << j.dump(2) << "\n";
	return ofs.good();
}

Library LoadPackLibrary(const std::string& path)
{
	Library library;
	const json j = ReadJsonArray(path);
	if (j.is_null())
		return library;
	try {
		for (const auto& item : j) {
			Modpack pack = PackFromJson(item);
			if (pack.id.empty())
				continue;
			if (pack.installedVersions.empty()) {
				// Saved before side-by-side versions: one version in the original folders.
				InstalledVersion version;
				version.key = VersionKey(pack);
				version.installDir = "installs/" + pack.id;
				std::error_code ec;
				const bool wholePack = std::filesystem::is_regular_file(std::filesystem::path(version.installDir) / "pack.json", ec);
				version.gameDir = wholePack ? version.installDir + "/minecraft" : "instances/" + pack.id;
				version.version = pack.version;
				version.sourceVersionId = pack.sourceVersionId;
				version.gameVersion = pack.gameVersion;
				version.loaders = pack.loaders;
				version.files = pack.files;
				version.optionalMods = pack.optionalMods;
				pack.installedVersions.push_back(version);
				pack.activeVersion = version.key;
			}
			SelectInstalledVersion(pack, pack.activeVersion);
			library[pack.id] = std::move(pack);
		}
	}
	catch (...) {
		// malformed entry: keep what was read
	}
	return library;
}

bool SavePackLibrary(const std::string& path, const Library& library)
{
	json j = json::array();
	for (const auto& [id, pack] : library)
		j.push_back(PackToJson(pack));
	return WriteJson(path, j);
}

std::vector<SavedDownload> LoadSavedDownloads(const std::string& path)
{
	std::vector<SavedDownload> downloads;
	const json j = ReadJsonArray(path);
	if (j.is_null())
		return downloads;
	try {
		for (const auto& item : j) {
			SavedDownload download;
			download.url = item.value("url", "");
			download.fileName = item.value("fileName", "");
			download.outPath = item.value("outPath", "");
			if (item.contains("pack") && item["pack"].is_object())
				download.pack = PackFromJson(item["pack"]);
			if (!download.url.empty() && !download.outPath.empty())
				downloads.push_back(std::move(download));
		}
	}
	catch (...) {
		// malformed entry: keep what was read
	}
	return downloads;
}

bool SaveDownloads(const std::string& path, const std::vector<SavedDownload>& downloads)
{
	json j = json::array();
	for (const SavedDownload& download : downloads) {
		j.push_back({
			{ "url", download.url },
			{ "fileName", download.fileName },
			{ "outPath", download.outPath },
			{ "pack", PackToJson(download.pack) },
		});
	}
	return WriteJson(path, j);
}

std::string VersionKey(const Modpack& pack)
{
	// The version name keeps folders readable and matches packs saved before versions had ids.
	std::string key = pack.version.empty() ? pack.sourceVersionId : pack.version;
	for (char& c : key) {
		if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '_')
			c = '_';
	}
	while (!key.empty() && key.back() == '.')
		key.pop_back(); // Windows drops trailing dots from folder names
	return key.empty() || key == "." ? std::string("default") : key;
}

const InstalledVersion* FindInstalledVersion(const Modpack& libraryPack, const std::string& key)
{
	for (const InstalledVersion& v : libraryPack.installedVersions) {
		if (v.key == key)
			return &v;
	}
	return nullptr;
}

const InstalledVersion* ActiveInstalledVersion(const Modpack& libraryPack)
{
	const InstalledVersion* active = FindInstalledVersion(libraryPack, libraryPack.activeVersion);
	if (!active && !libraryPack.installedVersions.empty())
		active = &libraryPack.installedVersions.front();
	return active;
}

InstalledVersion PlanInstalledVersion(const Library& library, const Modpack& pack, bool wholePack)
{
	InstalledVersion version;
	version.key = VersionKey(pack);
	const auto it = library.find(pack.id);
	const InstalledVersion* existing = it != library.end() ? FindInstalledVersion(it->second, version.key) : nullptr;
	if (existing) {
		version.installDir = existing->installDir;
		version.gameDir = existing->gameDir;
		version.contentChanges = existing->contentChanges; // reinstalling keeps the player's edits
	} else {
		version.installDir = "installs/" + pack.id + "@" + version.key;
		version.gameDir = wholePack ? version.installDir + "/minecraft" : "instances/" + pack.id + "@" + version.key;
	}
	version.version = pack.version;
	version.sourceVersionId = pack.sourceVersionId;
	version.gameVersion = pack.gameVersion;
	version.loaders = pack.loaders;
	version.files = pack.files;
	version.optionalMods = pack.optionalMods;
	return version;
}

void AddInstalledVersion(Library& library, const Modpack& pack, const InstalledVersion& version)
{
	Modpack& entry = library[pack.id];
	std::vector<InstalledVersion> versions = std::move(entry.installedVersions);
	entry = pack; // the newest details (name, icon, description, ...)
	entry.installedVersions = std::move(versions);
	bool replaced = false;
	for (InstalledVersion& v : entry.installedVersions) {
		if (v.key == version.key) {
			// The player's edits stay with the version, including any made while it reinstalled.
			std::map<std::string, std::string> changes = std::move(v.contentChanges);
			v = version;
			v.contentChanges = std::move(changes);
			replaced = true;
		}
	}
	if (!replaced)
		entry.installedVersions.push_back(version);
	SelectInstalledVersion(entry, version.key);
}

void SelectInstalledVersion(Modpack& libraryPack, const std::string& key)
{
	const InstalledVersion* chosen = FindInstalledVersion(libraryPack, key);
	if (!chosen)
		chosen = ActiveInstalledVersion(libraryPack);
	if (!chosen)
		return;
	const InstalledVersion v = *chosen;
	libraryPack.activeVersion = v.key;
	libraryPack.version = v.version;
	libraryPack.sourceVersionId = v.sourceVersionId;
	libraryPack.gameVersion = v.gameVersion;
	libraryPack.loaders = v.loaders;
	libraryPack.files = v.files;
	libraryPack.optionalMods = v.optionalMods;
}

bool RemoveInstalledVersion(Modpack& libraryPack, const std::string& key)
{
	auto& versions = libraryPack.installedVersions;
	versions.erase(std::remove_if(versions.begin(), versions.end(), [&](const InstalledVersion& v) { return v.key == key; }),
		versions.end());
	if (versions.empty())
		return false;
	if (libraryPack.activeVersion == key)
		SelectInstalledVersion(libraryPack, versions.back().key);
	return true;
}
