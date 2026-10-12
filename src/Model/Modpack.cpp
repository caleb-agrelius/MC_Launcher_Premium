#include "Modpack.h"
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

const char* PackSourceName(PackSource source)
{
	switch (source) {
	case PackSource::Modrinth: return "Modrinth";
	case PackSource::CurseForge: return "CurseForge";
	case PackSource::FTB: return "FTB";
	case PackSource::Technic: return "Technic";
	case PackSource::ATLauncher: return "ATLauncher";
	case PackSource::LocalIndex: break;
	}
	return "Local";
}

const char* PackSourceKey(PackSource source)
{
	switch (source) {
	case PackSource::Modrinth: return "modrinth";
	case PackSource::CurseForge: return "curseforge";
	case PackSource::FTB: return "ftb";
	case PackSource::Technic: return "technic";
	case PackSource::ATLauncher: return "atlauncher";
	case PackSource::LocalIndex: break;
	}
	return "local";
}

PackSource PackSourceFromKey(const std::string& key)
{
	for (PackSource source : { PackSource::Modrinth, PackSource::CurseForge, PackSource::FTB,
			 PackSource::Technic, PackSource::ATLauncher }) {
		if (key == PackSourceKey(source))
			return source;
	}
	return PackSource::LocalIndex;
}

std::vector<Modpack> LoadIndex(const std::string& path)
{
	std::vector<Modpack> out;
	std::ifstream ifs(path);
	if (!ifs.is_open())
		return out;

	json j;
	try {
		ifs >> j;
		if (!j.is_array())
			return out;

		for (const auto& item : j)
		{
			Modpack m;
			m.id = item.value("id", "");
			m.name = item.value("name", "");
			m.description = item.value("description", "");
			m.version = item.value("version", "");
			if (item.contains("files") && item["files"].is_array()) {
				for (const auto& f : item["files"]) {
					PackFile pf;
					pf.name = f.value("name", "");
					pf.url = f.value("url", "");
					pf.size = f.value("size", 0ULL);
					m.files.push_back(std::move(pf));
				}
			}
			out.push_back(std::move(m));
		}
	}
	catch (...) {
		// on parse error return what we have (likely empty)
	}

	return out;
}
