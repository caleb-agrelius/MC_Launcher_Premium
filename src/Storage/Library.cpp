#include "Library.h"

#include <nlohmann/json.hpp>

#include <fstream>

using json = nlohmann::json;

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
	if (item.contains("files") && item["files"].is_array()) {
		for (const auto& f : item["files"]) {
			PackFile file;
			file.name = f.value("name", "");
			file.url = f.value("url", "");
			file.size = f.value("size", 0ULL);
			pack.files.push_back(std::move(file));
		}
	}
	return pack;
}

static json PackToJson(const Modpack& pack)
{
	json files = json::array();
	for (const PackFile& file : pack.files)
		files.push_back({ { "name", file.name }, { "url", file.url }, { "size", file.size } });
	return {
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
		{ "files", files },
	};
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
	std::ofstream ofs(path, std::ios::trunc);
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
			if (!pack.id.empty())
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
