#include "PackCreator.h"

#include "Network/HttpClient.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

bool FetchMinecraftReleases(IHttpClient& http, std::vector<std::string>& out, std::string* err, const std::atomic<bool>* cancel)
{
	out.clear();
	std::string body;
	if (!http.Get("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json", body, err, cancel))
		return false;
	try {
		const json manifest = json::parse(body); // the loop refers into it, so it must outlive the loop
		for (const auto& version : manifest.at("versions")) { // newest first
			if (version.value("type", "") == "release")
				out.push_back(version.value("id", ""));
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected version manifest: ") + ex.what();
		return false;
	}
	return true;
}

// Every <version> in a Maven metadata file, in file order (oldest first).
static std::vector<std::string> MavenVersions(const std::string& xml)
{
	std::vector<std::string> versions;
	const std::string open = "<version>", close = "</version>";
	for (std::size_t pos = xml.find(open); pos != std::string::npos; pos = xml.find(open, pos)) {
		pos += open.size();
		const std::size_t end = xml.find(close, pos);
		if (end == std::string::npos)
			break;
		versions.push_back(xml.substr(pos, end - pos));
		pos = end;
	}
	return versions;
}

std::string NeoForgeVersionPrefix(const std::string& minecraftVersion)
{
	if (minecraftVersion.rfind("1.", 0) != 0)
		return minecraftVersion + ".";
	const std::string rest = minecraftVersion.substr(2); // "21.1" or "21"
	return rest.find('.') == std::string::npos ? rest + ".0." : rest + ".";
}

bool FetchLoaderVersions(IHttpClient& http, const std::string& loader, const std::string& minecraftVersion,
						 std::vector<std::string>& out, std::string* err, const std::atomic<bool>* cancel)
{
	out.clear();
	std::string body;
	if (loader == "fabric" || loader == "quilt") {
		// Both metas list the loaders for a Minecraft version, newest first.
		const std::string url = loader == "fabric"
			? "https://meta.fabricmc.net/v2/versions/loader/" + IHttpClient::UrlEncode(minecraftVersion)
			: "https://meta.quiltmc.org/v3/versions/loader/" + IHttpClient::UrlEncode(minecraftVersion);
		std::string error;
		if (!http.Get(url, body, &error, cancel)) {
			if (error == "HTTP 404" || error == "HTTP 400")
				return true; // the loader doesn't support this Minecraft version
			if (err) *err = error;
			return false;
		}
		try {
			std::vector<std::string> unstable;
			for (const auto& entry : json::parse(body)) {
				const json loaderInfo = entry.value("loader", json::object());
				const std::string version = loaderInfo.value("version", "");
				if (version.empty())
					continue;
				// Fabric marks betas unstable; Quilt puts "-beta" in the name. Stable ones go first.
				const bool stable = loaderInfo.value("stable", version.find('-') == std::string::npos);
				(stable ? out : unstable).push_back(version);
			}
			out.insert(out.end(), unstable.begin(), unstable.end());
		}
		catch (const std::exception& ex) {
			if (err) *err = std::string("Unexpected loader list: ") + ex.what();
			return false;
		}
		return true;
	}
	if (loader == "forge") {
		// Versions are "<minecraft>-<forge>", oldest first.
		if (!http.Get("https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml", body, err, cancel))
			return false;
		const std::string prefix = minecraftVersion + "-";
		for (const std::string& version : MavenVersions(body)) {
			if (version.rfind(prefix, 0) == 0) {
				std::string forge = version.substr(prefix.size());
				// Some old builds repeat the Minecraft version at the end ("10.13.4.1614-1.7.10").
				const std::size_t suffix = forge.find("-" + minecraftVersion);
				if (suffix != std::string::npos)
					forge.erase(suffix);
				out.push_back(forge);
			}
		}
		std::reverse(out.begin(), out.end());
		return true;
	}
	if (loader == "neoforge") {
		if (!http.Get("https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml", body, err, cancel))
			return false;
		const std::string prefix = NeoForgeVersionPrefix(minecraftVersion);
		std::vector<std::string> betas;
		for (const std::string& version : MavenVersions(body)) {
			if (version.rfind(prefix, 0) == 0)
				(version.find('-') == std::string::npos ? out : betas).push_back(version);
		}
		std::reverse(out.begin(), out.end());
		std::reverse(betas.begin(), betas.end());
		out.insert(out.end(), betas.begin(), betas.end());
		return true;
	}
	if (err) *err = "Unknown mod loader: " + loader;
	return false;
}

std::string CustomPackId(const std::string& name, const std::vector<std::string>& taken)
{
	std::string slug;
	for (unsigned char c : name) {
		if (std::isalnum(c))
			slug += static_cast<char>(std::tolower(c));
		else if (!slug.empty() && slug.back() != '-')
			slug += '-';
	}
	while (!slug.empty() && slug.back() == '-')
		slug.pop_back();
	if (slug.size() > 40)
		slug.resize(40);
	const std::string base = "custom-" + (slug.empty() ? std::string("pack") : slug);
	std::string id = base;
	for (int n = 2; std::find(taken.begin(), taken.end(), id) != taken.end(); ++n)
		id = base + "-" + std::to_string(n);
	return id;
}

bool WriteCustomPack(const fs::path& installDir, const std::string& version, const std::string& minecraftVersion,
					 const std::string& loader, const std::string& loaderVersion, std::string* err)
{
	std::error_code ec;
	for (const char* folder : { "mods", "resourcepacks", "shaderpacks" })
		fs::create_directories(installDir / "minecraft" / folder, ec);
	if (ec) {
		if (err) *err = "Couldn't create " + installDir.u8string() + ": " + ec.message();
		return false;
	}
	const json pack = {
		{ "source", "custom" },
		{ "version", version },
		{ "minecraft", minecraftVersion },
		{ "loader", loader },
		{ "loaderVersion", loaderVersion },
		{ "files", json::array() },
		{ "notes", json::array() },
		{ "clientOnlyFiles", json::array() },
	};
	std::ofstream out(installDir / "pack.json", std::ios::trunc);
	out << pack.dump(2) << "\n";
	if (!out.good()) {
		if (err) *err = "Couldn't write " + (installDir / "pack.json").u8string();
		return false;
	}
	return true;
}
