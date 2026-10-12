#include "Modrinth.h"
#include "HttpClient.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static const char* kApiBase = "https://api.modrinth.com/v2";

static std::string JoinStrings(const json& array, const std::string& separator)
{
	std::string result;
	if (!array.is_array())
		return result;
	for (const auto& item : array) {
		if (!item.is_string())
			continue;
		if (!result.empty())
			result += separator;
		result += item.get<std::string>();
	}
	return result;
}

// Search hits use null for missing icons/gallery images, which json::value() would throw on.
static std::string StringOrEmpty(const json& object, const char* key)
{
	const auto it = object.find(key);
	return (it != object.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

bool SearchModrinthModpacks(HttpClient& http,
							const std::string& query,
							const ModrinthSearchOptions& options,
							int limit,
							ModrinthSearchResult& out,
							std::string* err,
							const std::atomic<bool>* cancel)
{
	out = {};
	// Facets are AND-ed groups: [["project_type:modpack"],["versions:1.20.1"],["categories:fabric"]].
	json facets = json::array({ json::array({ "project_type:modpack" }) });
	if (!options.gameVersion.empty())
		facets.push_back(json::array({ "versions:" + options.gameVersion }));
	if (!options.loader.empty())
		facets.push_back(json::array({ "categories:" + options.loader }));
	const std::string index = !options.index.empty() ? options.index : (query.empty() ? "downloads" : "relevance");
	std::string url = std::string(kApiBase) + "/search?facets=" +
		HttpClient::UrlEncode(facets.dump()) +
		"&limit=" + std::to_string(limit) +
		"&index=" + index;
	if (!query.empty())
		url += "&query=" + HttpClient::UrlEncode(query);

	std::string body;
	if (!http.Get(url, body, err, cancel))
		return false;

	try {
		const json j = json::parse(body);
		out.totalHits = j.value("total_hits", 0ULL);
		for (const auto& hit : j.at("hits")) {
			Modpack pack;
			pack.id = hit.value("slug", "");
			if (pack.id.empty())
				pack.id = hit.value("project_id", "");
			pack.name = hit.value("title", "");
			pack.description = hit.value("description", "");
			pack.author = hit.value("author", "");
			pack.downloads = hit.value("downloads", 0ULL);
			if (hit.contains("versions") && hit["versions"].is_array() && !hit["versions"].empty())
				pack.gameVersion = hit["versions"].back().get<std::string>();
			pack.iconUrl = StringOrEmpty(hit, "icon_url");
			pack.bannerUrl = StringOrEmpty(hit, "featured_gallery");
			if (pack.bannerUrl.empty() && hit.contains("gallery") && hit["gallery"].is_array() &&
				!hit["gallery"].empty() && hit["gallery"].front().is_string())
				pack.bannerUrl = hit["gallery"].front().get<std::string>();
			if (hit.contains("color") && hit["color"].is_number_unsigned())
				pack.color = hit["color"].get<std::uint32_t>() & 0xFFFFFFu;
			pack.source = PackSource::Modrinth;
			pack.sourceId = pack.id;
			pack.websiteUrl = "https://modrinth.com/modpack/" + pack.id;
			pack.filesLoaded = false;
			if (!pack.id.empty())
				out.packs.push_back(std::move(pack));
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected search response: ") + ex.what();
		return false;
	}
	return true;
}

bool FetchLatestModrinthVersion(HttpClient& http,
								const std::string& projectIdOrSlug,
								ModrinthVersion& out,
								std::string* err,
								const std::atomic<bool>* cancel)
{
	out = {};
	const std::string url = std::string(kApiBase) + "/project/" +
		HttpClient::UrlEncode(projectIdOrSlug) + "/version";

	std::string body;
	if (!http.Get(url, body, err, cancel))
		return false;

	try {
		const json versions = json::parse(body);
		if (!versions.is_array() || versions.empty()) {
			if (err) *err = "This pack has no published versions";
			return false;
		}

		// Versions come newest first; prefer the newest stable release.
		const json* chosen = &versions.front();
		for (const auto& version : versions) {
			if (version.value("version_type", "") == "release") {
				chosen = &version;
				break;
			}
		}

		out.versionNumber = chosen->value("version_number", "");
		if (chosen->contains("game_versions") && (*chosen)["game_versions"].is_array() &&
			!(*chosen)["game_versions"].empty())
			out.gameVersion = (*chosen)["game_versions"].back().get<std::string>();
		out.loaders = JoinStrings(chosen->value("loaders", json::array()), ", ");

		// Use the primary file; fall back to the first .mrpack.
		const json files = chosen->value("files", json::array());
		const json* file = nullptr;
		for (const auto& candidate : files) {
			if (candidate.value("primary", false)) {
				file = &candidate;
				break;
			}
		}
		if (!file) {
			for (const auto& candidate : files) {
				const std::string name = candidate.value("filename", "");
				if (name.size() > 7 && name.compare(name.size() - 7, 7, ".mrpack") == 0) {
					file = &candidate;
					break;
				}
			}
		}
		if (!file) {
			if (err) *err = "Latest version has no .mrpack file";
			return false;
		}

		PackFile packFile;
		packFile.name = file->value("filename", "");
		packFile.url = file->value("url", "");
		packFile.size = file->value("size", 0ULL);
		out.files.push_back(std::move(packFile));
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected version response: ") + ex.what();
		return false;
	}
	return true;
}

bool FetchModrinthBanner(HttpClient& http,
						 const std::string& projectIdOrSlug,
						 std::string& outUrl,
						 std::string* err,
						 const std::atomic<bool>* cancel)
{
	outUrl.clear();
	const std::string url = std::string(kApiBase) + "/project/" + HttpClient::UrlEncode(projectIdOrSlug);

	std::string body;
	if (!http.Get(url, body, err, cancel))
		return false;

	try {
		const json project = json::parse(body);
		const json gallery = project.value("gallery", json::array());
		// Prefer the featured image, then the gallery's first (lowest ordering) image.
		const json* chosen = nullptr;
		for (const auto& image : gallery) {
			if (image.value("featured", false)) {
				chosen = &image;
				break;
			}
		}
		if (!chosen && !gallery.empty())
			chosen = &gallery.front();
		if (chosen) {
			// raw_url is the original upload; url may be a 350px thumbnail.
			outUrl = StringOrEmpty(*chosen, "raw_url");
			if (outUrl.empty())
				outUrl = StringOrEmpty(*chosen, "url");
		}
	}
	catch (const std::exception& ex) {
		if (err) *err = std::string("Unexpected project response: ") + ex.what();
		return false;
	}
	return true;
}
